/* GPLv3 */
#define _GNU_SOURCE
#include "fake_host.h"
#include "internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

struct fake_host {
    fake_opts o;
    mlxr_identity ident;      /* server cert + key */
    int lfd_http, lfd_https;
    uint16_t p_http, p_https;
    pthread_t th;
    volatile bool stop;
    SSL_CTX *ctx;
    pthread_mutex_t mu;
    char pin_buf[16];
    volatile int pin_ready;

    /* pairing state */
    char *pending_client_pem;
    uint8_t key[16];
    uint8_t server_secret[16], server_challenge[16];
    uint8_t client_hash[32];
    bool have_client_hash;
    char *paired_client_pem;
    bool paired;

    int current_game;
    char last_launch[1024];
    char last_uid[64];
    int counts[16];
    const char *cmds_seen[16];
    /* Native (lane artemis-settings): Apollo */
    bool streaming;
    char clipboard[4096];
};

void fake_host_set_streaming(fake_host *h, bool on) { pthread_mutex_lock(&h->mu); h->streaming = on; pthread_mutex_unlock(&h->mu); }
void fake_host_set_clipboard(fake_host *h, const char *text)
{
    pthread_mutex_lock(&h->mu);
    snprintf(h->clipboard, sizeof h->clipboard, "%s", text);
    pthread_mutex_unlock(&h->mu);
}
void fake_host_clipboard(fake_host *h, char *out, int cap)
{
    pthread_mutex_lock(&h->mu);
    snprintf(out, (size_t)cap, "%s", h->clipboard);
    pthread_mutex_unlock(&h->mu);
}

static int listen_local(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 8) != 0) { close(fd); return -1; }
    socklen_t l = sizeof a;
    getsockname(fd, (struct sockaddr *)&a, &l);
    *port = ntohs(a.sin_port);
    return fd;
}

static int verify_any(int ok, X509_STORE_CTX *c) { return 1; }

/* ---- tiny query parsing ---- */
static const char *qget(const char *q, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key);
    for (const char *p = q; *p;) {
        const char *amp = strchr(p, '&');
        size_t n = amp ? (size_t)(amp - p) : strlen(p);
        if (n > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            size_t vl = n - kl - 1;
            if (vl >= cap) vl = cap - 1;
            memcpy(out, p + kl + 1, vl);
            out[vl] = 0;
            return out;
        }
        if (!amp) break;
        p = amp + 1;
    }
    out[0] = 0;
    return NULL;
}

static int count_idx(const char *cmd)
{
    static const char *const names[] = {"serverinfo", "pair", "unpair", "applist", "launch", "resume", "cancel"};
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(cmd, names[i])) return (int)i;
    return 15;
}

int fake_host_count(const fake_host *h, const char *command) { return h->counts[count_idx(command)]; }

static char *xml_reply(int code, const char *msg, const char *inner)
{
    char *r = NULL;
    if (asprintf(&r, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root status_code=\"%d\"%s%s%s>%s</root>\n", code,
                 msg ? " status_message=\"" : "", msg ? msg : "", msg ? "\"" : "", inner ? inner : "") < 0)
        return NULL;
    return r;
}

static size_t hashlen(fake_host *h) { return h->o.gen >= 7 ? 32 : 20; }

static char *do_pair(fake_host *h, const char *q, bool https, X509 *peer)
{
    char big[8192], tmp[8192];
    if (qget(q, "phrase", tmp, sizeof tmp) && !strcmp(tmp, "getservercert")) {
        if (h->o.busy_pairing) return xml_reply(200, NULL, "<paired>1</paired><plaincert></plaincert>");
        qget(q, "salt", tmp, sizeof tmp);
        size_t sl;
        uint8_t *salt = mlxr_hex_decode_alloc(tmp, &sl);
        qget(q, "clientcert", big, sizeof big);
        size_t cl;
        uint8_t *cpem = mlxr_hex_decode_alloc(big, &cl);
        if (!salt || sl != 16 || !cpem) { free(salt); free(cpem); return xml_reply(400, "bad getservercert", NULL); }
        free(h->pending_client_pem);
        h->pending_client_pem = calloc(1, cl + 1);
        memcpy(h->pending_client_pem, cpem, cl);
        if (h->o.wait_for_pin) {
            while (!__atomic_load_n(&h->pin_ready, __ATOMIC_ACQUIRE) && !h->stop) usleep(20000);
            mlxr_pair_derive_key(h->o.gen, salt, h->pin_buf, h->key);
        } else {
            mlxr_pair_derive_key(h->o.gen, salt, h->o.pin, h->key);
        }
        free(salt); free(cpem);
        h->have_client_hash = false;
        char *hex = mlxr_hex_encode_alloc((const uint8_t *)h->ident.cert_pem, strlen(h->ident.cert_pem));
        char *inner = NULL;
        asprintf(&inner, "<paired>1</paired><plaincert>%s</plaincert>", hex);
        free(hex);
        char *r = xml_reply(200, NULL, inner);
        free(inner);
        return r;
    }
    if (qget(q, "clientchallenge", big, sizeof big)) {
        size_t n;
        uint8_t *enc = mlxr_hex_decode_alloc(big, &n);
        uint8_t dec[64], out[64];
        if (!enc || n != 16 || mlxr_aes128_ecb(false, h->key, enc, 16, dec) != MLXR_OK) { free(enc); return xml_reply(400, "bad challenge", NULL); }
        free(enc);
        mlxr_random(h->server_secret, 16);
        mlxr_random(h->server_challenge, 16);
        uint8_t sig[1024], cat[16 + 1024 + 16], hh[32] = {0};
        int sg = mlxr_cert_signature(h->ident.cert_pem, sig, sizeof sig);
        memcpy(cat, dec, 16);
        memcpy(cat + 16, sig, (size_t)sg);
        memcpy(cat + 16 + sg, h->server_secret, 16);
        mlxr_pair_hash(h->o.gen, cat, 32 + (size_t)sg, hh);
        size_t hl = hashlen(h);
        memset(out, 0, sizeof out);
        memcpy(out, hh, hl);
        memcpy(out + hl, h->server_challenge, 16);
        size_t tot = (hl + 16 + 15) / 16 * 16;
        uint8_t encout[64];
        mlxr_aes128_ecb(true, h->key, out, tot, encout);
        char hex[200];
        mlxr_hex_encode(encout, tot, hex);
        char inner[300];
        snprintf(inner, sizeof inner, "<paired>1</paired><challengeresponse>%s</challengeresponse>", hex);
        return xml_reply(200, NULL, inner);
    }
    if (qget(q, "serverchallengeresp", big, sizeof big)) {
        size_t n;
        uint8_t *enc = mlxr_hex_decode_alloc(big, &n);
        if (!enc || n != 32 || mlxr_aes128_ecb(false, h->key, enc, 32, h->client_hash) != MLXR_OK) { free(enc); return xml_reply(400, "bad resp", NULL); }
        free(enc);
        h->have_client_hash = true;
        uint8_t ps[16 + 512];
        memcpy(ps, h->server_secret, 16);
        int sl = mlxr_sign_sha256(h->ident.key_pem, h->server_secret, 16, ps + 16, sizeof ps - 16);
        if (h->o.bad_secret_signature) ps[16 + 3] ^= 0x55;
        char *hex = mlxr_hex_encode_alloc(ps, 16 + (size_t)sl);
        char *inner = NULL;
        asprintf(&inner, "<paired>1</paired><pairingsecret>%s</pairingsecret>", hex);
        free(hex);
        char *r = xml_reply(200, NULL, inner);
        free(inner);
        return r;
    }
    if (qget(q, "clientpairingsecret", big, sizeof big)) {
        size_t n;
        uint8_t *cps = mlxr_hex_decode_alloc(big, &n);
        bool ok = cps && n > 16 && h->have_client_hash && h->pending_client_pem;
        if (ok) ok = mlxr_verify_sha256(h->pending_client_pem, cps, 16, cps + 16, n - 16);
        if (ok) {
            uint8_t sig[1024], cat[16 + 1024 + 16], hh[32] = {0};
            int sg = mlxr_cert_signature(h->pending_client_pem, sig, sizeof sig);
            memcpy(cat, h->server_challenge, 16);
            memcpy(cat + 16, sig, (size_t)sg);
            memcpy(cat + 16 + sg, cps, 16);
            mlxr_pair_hash(h->o.gen, cat, 32 + (size_t)sg, hh);
            ok = memcmp(hh, h->client_hash, hashlen(h)) == 0;
        }
        free(cps);
        if (!ok) return xml_reply(200, NULL, "<paired>0</paired>");
        free(h->paired_client_pem);
        h->paired_client_pem = strdup(h->pending_client_pem);
        h->paired = true;
        return xml_reply(200, NULL, "<paired>1</paired>");
    }
    if (qget(q, "phrase", tmp, sizeof tmp) && !strcmp(tmp, "pairchallenge")) {
        bool ok = https && peer && h->paired_client_pem;
        if (ok) {
            X509 *pc = mlxr_x509_from_pem(h->paired_client_pem);
            ok = pc && X509_cmp(pc, peer) == 0;
            X509_free(pc);
        }
        return xml_reply(200, NULL, ok ? "<paired>1</paired>" : "<paired>0</paired>");
    }
    return xml_reply(400, "unknown pair step", NULL);
}

static char *handle(fake_host *h, const char *cmd, const char *q, bool https, X509 *peer)
{
    pthread_mutex_lock(&h->mu);
    h->counts[count_idx(cmd)]++;
    char uid[64];
    qget(q, "uniqueid", uid, sizeof uid);
    snprintf(h->last_uid, sizeof h->last_uid, "%s", uid);
    char *r = NULL;

    bool authed = false;
    if (https && peer && h->paired_client_pem) {
        X509 *pc = mlxr_x509_from_pem(h->paired_client_pem);
        authed = pc && X509_cmp(pc, peer) == 0;
        X509_free(pc);
    }

    if (!strcmp(cmd, "serverinfo")) {
        char inner[2048];
        char apollo_inner[512];
        /* Apollo lists server commands only to a client holding the Server Command permission */
        snprintf(apollo_inner, sizeof apollo_inner,
                 "<VirtualDisplayCapable>true</VirtualDisplayCapable><VirtualDisplayDriverReady>true"
                 "</VirtualDisplayDriverReady>%s<Permission>%d</Permission>",
                 h->o.cmd_denied ? ""
                 : h->o.more_cmds ? "<ServerCommand>Bubbles</ServerCommand><ServerCommand>Sleep PC</ServerCommand>"
                                    "<ServerCommand>Lock PC</ServerCommand>"
                                  : "<ServerCommand>Bubbles</ServerCommand>",
                 h->o.cmd_denied ? 117645056 : 118693632);
        snprintf(inner, sizeof inner,
                 "<hostname>fake-host</hostname><appversion>%d.1.431.-1</appversion>%s"
                 "<uniqueid>FAKE-HOST-UUID-0001</uniqueid><HttpsPort>%u</HttpsPort><ExternalPort>%u</ExternalPort>"
                 "<MaxLumaPixelsHEVC>1869449984</MaxLumaPixelsHEVC><mac>%s</mac><LocalIP>127.0.0.1</LocalIP>"
                 "<ServerCodecModeSupport>259</ServerCodecModeSupport><SupportedDisplayMode>"
                 "<DisplayMode><Width>1920</Width><Height>1080</Height><RefreshRate>60</RefreshRate></DisplayMode>"
                 "<DisplayMode><Width>1280</Width><Height>720</Height><RefreshRate>60</RefreshRate></DisplayMode>"
                 "</SupportedDisplayMode><PairStatus>%d</PairStatus><currentgame>%d</currentgame><state>%s_SERVER_%s</state>%s",
                 h->o.gen, h->o.gfe ? "<GfeVersion>3.23.0.74</GfeVersion>" : "", h->p_https, h->p_http,
                 h->o.zero_mac ? "00:00:00:00:00:00" : "aa:bb:cc:dd:ee:ff", authed ? 1 : 0, h->current_game,
                 h->o.gfe ? "MJOLNIR" : "SUNSHINE", h->current_game ? "BUSY" : "FREE",
                 h->o.apollo ? apollo_inner : "");
        r = xml_reply(200, NULL, inner);
    } else if (!strcmp(cmd, "pair")) {
        r = do_pair(h, q, https, peer);
    } else if (!strcmp(cmd, "unpair")) {
        h->paired = false;
        free(h->paired_client_pem);
        h->paired_client_pem = NULL;
        r = xml_reply(200, NULL, NULL);
    } else if (!https || !authed) {
        r = xml_reply(401, "The client is not authorized. Certificate verification failed.", NULL);
    } else if (!strcmp(cmd, "applist")) {
        char inner[2048];
        snprintf(inner, sizeof inner,
                 "<App><IsHdrSupported>1</IsHdrSupported><AppTitle>Steam Big Picture</AppTitle><ID>1093255277</ID></App>"
                 "<App><IsHdrSupported>0</IsHdrSupported><AppTitle>Desktop</AppTitle><ID>881448767</ID></App>"
                 "%s%s%s",
                 h->o.app_title_extra ? "<App><AppTitle>" : "", h->o.app_title_extra ? h->o.app_title_extra : "",
                 h->o.app_title_extra ? "</AppTitle><ID>-1234567</ID></App>" : "");
        r = xml_reply(200, NULL, inner);
    } else if (!strcmp(cmd, "launch") || !strcmp(cmd, "resume")) {
        char rikey[64], rid[32], mode[64], appid[32];
        qget(q, "rikey", rikey, sizeof rikey);
        qget(q, "rikeyid", rid, sizeof rid);
        qget(q, "mode", mode, sizeof mode);
        qget(q, "appid", appid, sizeof appid);
        bool resume = cmd[0] == 'r';
        if (strlen(rikey) != 32 || !rid[0] || !mode[0]) {
            r = xml_reply(400, "missing launch parameters", NULL);
        } else if (resume && !h->current_game) {
            r = xml_reply(400, "no game to resume", NULL);
        } else {
            snprintf(h->last_launch, sizeof h->last_launch, "%s", q);
            if (!resume) h->current_game = atoi(appid);
            r = xml_reply(200, NULL, "<gamesession>1</gamesession><sessionUrl0>rtspenc://127.0.0.1:48010</sessionUrl0>");
        }
    } else if (!strcmp(cmd, "cancel")) {
        if (!h->o.cancel_is_noop) h->current_game = 0;
        r = xml_reply(200, NULL, "<cancel>1</cancel>");
    } else {
        r = xml_reply(404, "not found", NULL);
    }
    pthread_mutex_unlock(&h->mu);
    return r;
}

static void serve_one(fake_host *h, int fd, bool https)
{
    SSL *ssl = NULL;
    X509 *peer = NULL;
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (https) {
        ssl = SSL_new(h->ctx);
        SSL_set_fd(ssl, fd);
        if (SSL_accept(ssl) != 1) { ERR_clear_error(); SSL_free(ssl); return; }
        peer = SSL_get1_peer_certificate(ssl);
    }
    char req[16384];
    size_t n = 0;
    while (n < sizeof req - 1) {
        int r = https ? SSL_read(ssl, req + n, (int)(sizeof req - 1 - n)) : (int)read(fd, req + n, sizeof req - 1 - n);
        if (r <= 0) break;
        n += (size_t)r;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[n] = 0;
    char *resp = NULL;
    int status = 200;
    const char *ctype = "text/xml";
    /* Native (lane artemis-settings): Apollo's actions/clipboard, GET and POST (raw text) */
    if (h->o.apollo && https && (!strncmp(req, "GET /actions/clipboard", 22) || !strncmp(req, "POST /actions/clipboard", 23))) {
        bool post = req[0] == 'P';
        char *hdr_end = strstr(req, "\r\n\r\n");
        const char *cl = strcasestr(req, "Content-Length:");
        size_t want = cl ? (size_t)atol(cl + 15) : 0;
        size_t have = hdr_end ? n - (size_t)(hdr_end + 4 - req) : 0;
        while (post && hdr_end && have < want && n < sizeof req - 1) {
            int r = SSL_read(ssl, req + n, (int)(sizeof req - 1 - n));
            if (r <= 0) break;
            n += (size_t)r;
            have += (size_t)r;
            req[n] = 0;
        }
        bool authed = false;
        if (peer && h->paired_client_pem) {
            X509 *pc = mlxr_x509_from_pem(h->paired_client_pem);
            authed = pc && X509_cmp(pc, peer) == 0;
            X509_free(pc);
        }
        pthread_mutex_lock(&h->mu);
        ctype = "text/plain";
        if (h->o.clipboard_404) status = 404;
        else if (!authed || h->o.clipboard_denied) status = 401;
        else if (!strstr(req, "type=text")) status = 400;
        else if (!h->streaming) status = 403;
        if (status != 200) resp = strdup("");
        else if (post) {
            snprintf(h->clipboard, sizeof h->clipboard, "%.*s", (int)(want < have ? want : have), hdr_end + 4);
            resp = strdup("");
        } else resp = strdup(h->clipboard);
        pthread_mutex_unlock(&h->mu);
    } else if (!strncmp(req, "GET /", 5)) {
        char *sp = strchr(req + 5, ' ');
        if (sp) {
            *sp = 0;
            char *qm = strchr(req + 5, '?');
            const char *q = "";
            if (qm) { *qm = 0; q = qm + 1; }
            resp = handle(h, req + 5, q, https, peer);
        }
    }
    if (!resp) resp = xml_reply(400, "bad request", NULL);
    char head[256];
    int hl = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                      status, status == 200 ? "OK" : "Error", ctype, strlen(resp));
    if (https) {
        SSL_write(ssl, head, hl);
        SSL_write(ssl, resp, (int)strlen(resp));
        SSL_shutdown(ssl);
    } else {
        if (write(fd, head, (size_t)hl) < 0 || write(fd, resp, strlen(resp)) < 0) {}
    }
    free(resp);
    X509_free(peer);
    SSL_free(ssl);
}

static void *loop(void *arg)
{
    fake_host *h = arg;
    while (!h->stop) {
        struct pollfd p[2] = {{h->lfd_http, POLLIN, 0}, {h->lfd_https, POLLIN, 0}};
        if (poll(p, 2, 50) <= 0) continue;
        for (int i = 0; i < 2; i++) {
            if (!(p[i].revents & POLLIN)) continue;
            int fd = accept(p[i].fd, NULL, NULL);
            if (fd < 0) continue;
            serve_one(h, fd, i == 1);
            close(fd);
        }
    }
    return NULL;
}

fake_host *fake_host_start(const fake_opts *o)
{
    fake_host *h = calloc(1, sizeof *h);
    if (o) h->o = *o;
    if (!h->o.pin) h->o.pin = "1234";
    if (!h->o.gen) h->o.gen = 7;
    pthread_mutex_init(&h->mu, NULL);
    if (mlxr_identity_generate(&h->ident, NULL) != MLXR_OK) { free(h); return NULL; }
    if (h->o.expired_cert) {
        /* re-issue the same key with a validity window in the past */
        X509 *x = mlxr_x509_from_pem(h->ident.cert_pem);
        EVP_PKEY *k = mlxr_pkey_from_pem(h->ident.key_pem);
        X509_gmtime_adj(X509_getm_notBefore(x), -2L * 365 * 24 * 3600);
        X509_gmtime_adj(X509_getm_notAfter(x), -365L * 24 * 3600);
        X509_sign(x, k, EVP_sha256());
        BIO *bo = BIO_new(BIO_s_mem());
        PEM_write_bio_X509(bo, x);
        char *pp = NULL;
        long pl = BIO_get_mem_data(bo, &pp);
        free(h->ident.cert_pem);
        h->ident.cert_pem = strndup(pp, (size_t)pl);
        BIO_free(bo); X509_free(x); EVP_PKEY_free(k);
    }
    h->ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(h->ctx, TLS1_2_VERSION);
    BIO *bc = BIO_new_mem_buf(h->ident.cert_pem, -1), *bk = BIO_new_mem_buf(h->ident.key_pem, -1);
    X509 *x = PEM_read_bio_X509(bc, NULL, NULL, NULL);
    EVP_PKEY *k = PEM_read_bio_PrivateKey(bk, NULL, NULL, NULL);
    SSL_CTX_use_certificate(h->ctx, x);
    SSL_CTX_use_PrivateKey(h->ctx, k);
    X509_free(x); EVP_PKEY_free(k); BIO_free(bc); BIO_free(bk);
    SSL_CTX_set_verify(h->ctx, SSL_VERIFY_PEER, verify_any);
    h->lfd_http = listen_local(&h->p_http);
    h->lfd_https = listen_local(&h->p_https);
    if (h->lfd_http < 0 || h->lfd_https < 0) return NULL;
    pthread_create(&h->th, NULL, loop, h);
    return h;
}

void fake_host_stop(fake_host *h)
{
    if (!h) return;
    h->stop = true;
    pthread_join(h->th, NULL);
    close(h->lfd_http);
    close(h->lfd_https);
    SSL_CTX_free(h->ctx);
    mlxr_identity_free(&h->ident);
    free(h->pending_client_pem);
    free(h->paired_client_pem);
    free(h);
}

void fake_host_set_pin(fake_host *h, const char *pin)
{
    snprintf(h->pin_buf, sizeof h->pin_buf, "%s", pin);
    __atomic_store_n(&h->pin_ready, 1, __ATOMIC_RELEASE);
}

uint16_t fake_host_http_port(const fake_host *h) { return h->p_http; }
uint16_t fake_host_https_port(const fake_host *h) { return h->p_https; }
const char *fake_host_cert_pem(const fake_host *h) { return h->ident.cert_pem; }
bool fake_host_is_paired(const fake_host *h) { return h->paired; }
int fake_host_current_game(const fake_host *h) { return h->current_game; }
void fake_host_last_launch(const fake_host *h, char *out, int cap) { snprintf(out, (size_t)cap, "%s", h->last_launch); }
void fake_host_last_uniqueid(const fake_host *h, char *out, int cap) { snprintf(out, (size_t)cap, "%s", h->last_uid); }
