/* HTTP/HTTPS requests to a GameStream-compatible host (nvhttp.cpp). GPLv3.
 *
 * HTTPS uses the client certificate and trusts exactly one server certificate:
 * the one pinned during pairing. It is installed as the sole trust anchor and,
 * on top of that, its public key is pinned, so a different cert (even one
 * chained to a public CA) is rejected. Hostname checks are off because the
 * cert is self-signed and addressed by IP. */
#define _GNU_SOURCE
#include "server.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <openssl/crypto.h>

#define FAST_FAIL_TIMEOUT_MS 2000
#define REQUEST_TIMEOUT_MS   5000
#define LAUNCH_TIMEOUT_MS    120000
#define RESUME_TIMEOUT_MS    30000
#define QUIT_TIMEOUT_MS      30000
#define MAX_BODY             (8u << 20)

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static void curl_init_once(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

mlxr_server *mlxr_server_new(const mlxr_identity *id, const char *address, uint16_t http_port)
{
    if (!id || !id->cert_pem || !id->key_pem || !address || !*address) return NULL;
    mlxr_server *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    mlxr_strlcpy(s->address, address, sizeof s->address);
    s->http_port = http_port ? http_port : MLXR_DEFAULT_HTTP_PORT;
    s->https_port = 0;
    mlxr_strlcpy(s->unique_id, id->unique_id, sizeof s->unique_id);
    s->client_cert = strdup(id->cert_pem);
    s->client_key = strdup(id->key_pem);
    if (!s->client_cert || !s->client_key) {
        mlxr_server_free(s);
        return NULL;
    }
    return s;
}

void mlxr_server_free(mlxr_server *s)
{
    if (!s) return;
    if (s->client_key) {
        OPENSSL_cleanse(s->client_key, strlen(s->client_key));
        free(s->client_key);
    }
    free(s->client_cert);
    free(s->pinned_pem);
    free(s->pinned_pin);
    free(s);
}

void mlxr_server_set_https_port(mlxr_server *s, uint16_t port) { s->https_port = port; }
uint16_t mlxr_server_https_port(const mlxr_server *s) { return s->https_port; }
const char *mlxr_server_pinned_cert(const mlxr_server *s) { return s->pinned_pem; }
void mlxr_server_set_gfe(mlxr_server *s, bool is_gfe) { s->is_gfe = is_gfe; }

mlxr_status mlxr_server_set_pinned_cert(mlxr_server *s, const char *pem, mlxr_error *err)
{
    if (!pem) {
        free(s->pinned_pem);
        free(s->pinned_pin);
        s->pinned_pem = s->pinned_pin = NULL;
        return MLXR_OK;
    }
    char *pin = mlxr_spki_pin(pem);
    if (!pin) return mlxr_fail(err, MLXR_E_CRYPTO, 0, "pinned certificate is not a readable PEM X.509 certificate");
    char *dup = strdup(pem);
    if (!dup) { free(pin); return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); }
    free(s->pinned_pem);
    free(s->pinned_pin);
    s->pinned_pem = dup;
    s->pinned_pin = pin;
    return MLXR_OK;
}

/* ---- XML response checks ---- */

mlxr_status mlxr_check_response(const char *body, mlxr_error *err)
{
    mlxr_xml *x = mlxr_xml_parse(body, body ? strlen(body) : 0);
    int code = 0;
    char msg[160] = "";
    if (!x || !mlxr_xml_status(x, &code, msg, sizeof msg)) {
        mlxr_xml_free(x);
        return mlxr_fail(err, MLXR_E_XML, -1, "Malformed XML (missing root element)");
    }
    mlxr_xml_free(x);
    if (code == 200) return MLXR_OK;
    if (code == -1 && strcmp(msg, "Invalid") == 0) {
        /* GFE audio capture failure with no useful message */
        code = 418;
        mlxr_strlcpy(msg, "Missing audio capture device. Reinstalling GeForce Experience should resolve this error.", sizeof msg);
    }
    return mlxr_fail(err, MLXR_E_HTTP, code, "host returned status %d: %s", code, msg);
}

/* ---- transport ---- */

struct buf {
    char *p;
    size_t n, cap;
    bool overflow;
};

static size_t write_cb(char *d, size_t sz, size_t nm, void *ud)
{
    struct buf *b = ud;
    size_t n = sz * nm;
    if (b->n + n + 1 > MAX_BODY) {
        b->overflow = true;
        return 0;
    }
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + n + 1) nc *= 2;
        char *np = realloc(b->p, nc);
        if (!np) return 0;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
    return n;
}

static mlxr_status map_curl(CURLcode rc, const char *what, const char *errbuf, mlxr_error *err)
{
    switch (rc) {
    case CURLE_OPERATION_TIMEDOUT:
        return mlxr_fail(err, MLXR_E_TIMEOUT, rc, "%s: request timed out", what);
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
    case CURLE_SSL_ENGINE_NOTFOUND:
        return mlxr_fail(err, MLXR_E_TLS, rc, "%s: TLS failure / server certificate mismatch (%s)", what,
                         errbuf && *errbuf ? errbuf : curl_easy_strerror(rc));
    default:
        return mlxr_fail(err, MLXR_E_NET, rc, "%s: %s", what, errbuf && *errbuf ? errbuf : curl_easy_strerror(rc));
    }
}

mlxr_status mlxr_http_get(mlxr_server *s, bool https, const char *command, const char *args, long timeout_ms,
                          char **body, mlxr_error *err)
{
    pthread_once(&curl_once, curl_init_once);
    *body = NULL;

    if (https && (!s->pinned_pem || !s->https_port))
        return mlxr_fail(err, MLXR_E_ARG, 0, "HTTPS requested without a pinned server certificate / HTTPS port");

    uint8_t u[16];
    char uuid[33];
    mlxr_random(u, sizeof u);
    mlxr_hex_encode(u, sizeof u, uuid);

    /* Real GFE lets every client quit anyone's game via a shared placeholder uid. */
    const char *uid = s->is_gfe ? "0123456789ABCDEF" : s->unique_id;
    bool v6 = strchr(s->address, ':') != NULL;
    unsigned port = https ? s->https_port : s->http_port;

    size_t cap = 256 + strlen(s->address) + (args ? strlen(args) : 0);
    char *url = malloc(cap);
    if (!url) return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom");
    snprintf(url, cap, "%s://%s%s%s:%u/%s?uniqueid=%s&uuid=%s%s%s", https ? "https" : "http", v6 ? "[" : "",
             s->address, v6 ? "]" : "", port, command, uid, uuid, args && *args ? "&" : "", args ? args : "");

    CURL *c = curl_easy_init();
    if (!c) { free(url); return mlxr_fail(err, MLXR_E_NOMEM, 0, "curl_easy_init failed"); }
    struct buf b = {0};
    char errbuf[CURL_ERROR_SIZE] = "";

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "*");              /* never use a proxy */
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1); /* GFE 3.22 dislikes h2 */
    curl_easy_setopt(c, CURLOPT_FORBID_REUSE, 1L);          /* GFE dislikes persistent connections */
    curl_easy_setopt(c, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, https ? "https" : "http");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms ? (timeout_ms < 5000 ? timeout_ms : 5000L) : 5000L);
    if (timeout_ms) curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, timeout_ms);

    if (https) {
        struct curl_blob cert = {s->client_cert, strlen(s->client_cert), CURL_BLOB_COPY};
        struct curl_blob key = {s->client_key, strlen(s->client_key), CURL_BLOB_COPY};
        struct curl_blob ca = {s->pinned_pem, strlen(s->pinned_pem), CURL_BLOB_COPY};
        curl_easy_setopt(c, CURLOPT_SSLCERTTYPE, "PEM");
        curl_easy_setopt(c, CURLOPT_SSLCERT_BLOB, &cert);
        curl_easy_setopt(c, CURLOPT_SSLKEYTYPE, "PEM");
        curl_easy_setopt(c, CURLOPT_SSLKEY_BLOB, &key);
        curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);       /* sole trust anchor: the pinned cert */
        curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);     /* self-signed, addressed by IP */
        curl_easy_setopt(c, CURLOPT_PINNEDPUBLICKEY, s->pinned_pin);
        curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    }

    mlxr_log(2, "GET %s://<host>:%u/%s", https ? "https" : "http", port, command);
    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    free(url);

    if (rc != CURLE_OK) {
        mlxr_status st;
        if (b.overflow)
            st = mlxr_fail(err, MLXR_E_XML, 0, "%s: response too large", command);
        else
            st = map_curl(rc, command, errbuf, err);
        free(b.p);
        return st;
    }
    if (!b.p || b.n == 0) {
        free(b.p);
        return mlxr_fail(err, MLXR_E_XML, (int)http, "%s: empty response (HTTP %ld)", command, http);
    }
    *body = b.p;
    return MLXR_OK;
}

/* ---- serverinfo ---- */

static mlxr_status get_info_inner(mlxr_server *s, bool fast_fail, bool allow_retry, mlxr_computer *out,
                                  char **raw, mlxr_error *err)
{
    long to = fast_fail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS;
    char *body = NULL;
    mlxr_status st;

    if (s->pinned_pem && s->https_port) {
        /* Always try HTTPS first: it is the only channel that reports PairStatus properly. */
        st = mlxr_http_get(s, true, "serverinfo", NULL, to, &body, err);
        if (st == MLXR_OK) {
            st = mlxr_check_response(body, err);
            if (st == MLXR_OK) {
                st = mlxr_computer_parse(body, strlen(body), s->http_port, out, err);
                if (st == MLXR_OK) out->via_https = true;
                if (st == MLXR_OK && raw) { *raw = body; body = NULL; }
                free(body);
                return st;
            }
        }
        free(body);
        body = NULL;
        bool fallback = st == MLXR_E_TLS || (st == MLXR_E_HTTP && err && err->status == 401);
        if (!fallback) return st;
        /* cert validation error (host re-keyed?): report over HTTP so the caller sees PairStatus 0 */
        mlxr_log(1, "serverinfo over HTTPS failed (%s), falling back to HTTP", mlxr_status_name(st));
    }

    st = mlxr_http_get(s, false, "serverinfo", NULL, to, &body, err);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    if (st == MLXR_OK) st = mlxr_computer_parse(body, strlen(body), s->http_port, out, err);
    if (st != MLXR_OK) { free(body); return st; }

    bool learned_port = !s->https_port;
    if (learned_port) s->https_port = out->https_port ? out->https_port : MLXR_DEFAULT_HTTPS_PORT;
    if (s->pinned_pem && allow_retry && learned_port) {
        /* we only needed the HTTPS port: ask again over the authenticated channel */
        mlxr_computer again;
        char *raw2 = NULL;
        mlxr_status st2 = get_info_inner(s, fast_fail, false, &again, &raw2, NULL);
        if (st2 == MLXR_OK && again.via_https) {
            *out = again;
            free(body);
            body = raw2;
        } else {
            free(raw2);
        }
    }
    if (raw) *raw = body;
    else free(body);
    return MLXR_OK;
}

mlxr_status mlxr_server_get_info(mlxr_server *s, bool fast_fail, mlxr_computer *out, char **raw_xml, mlxr_error *err)
{
    if (raw_xml) *raw_xml = NULL;
    return get_info_inner(s, fast_fail, true, out, raw_xml, err);
}

/* ---- applist ---- */

static int cmp_app(const void *a, const void *b)
{
    const char *x = ((const mlxr_app *)a)->name, *y = ((const mlxr_app *)b)->name;
    return strcasecmp(x, y);
}

mlxr_status mlxr_parse_applist(const char *xml, size_t len, mlxr_app **apps, size_t *n, mlxr_error *err)
{
    *apps = NULL;
    *n = 0;
    mlxr_xml *x = mlxr_xml_parse(xml, len);
    if (!x) return mlxr_fail(err, MLXR_E_XML, -1, "Malformed XML (missing root element)");
    size_t cnt = mlxr_xml_count(x, "App");
    mlxr_app *v = calloc(cnt ? cnt : 1, sizeof *v);
    if (!v) { mlxr_xml_free(x); return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); }
    for (size_t i = 0; i < cnt; i++) {
        char *t = mlxr_xml_get_nth(x, "App", i, "AppTitle");   /* Sunshine may send <AppTitle/> */
        char *id = mlxr_xml_get_nth(x, "App", i, "ID");
        char *hdr = mlxr_xml_get_nth(x, "App", i, "IsHdrSupported");
        char *col = mlxr_xml_get_nth(x, "App", i, "IsAppCollectorGame");
        if (!t || !id) {
            /* "We must have a valid app before advancing to the next one" */
            free(t); free(id); free(hdr); free(col);
            mlxr_apps_free(v, i);
            mlxr_xml_free(x);
            return mlxr_fail(err, MLXR_E_XML, 0, "Invalid applist XML (app %zu lacks AppTitle or ID)", i);
        }
        v[i].name = t;
        v[i].id = (int)strtoll(id, NULL, 10);
        v[i].hdr_supported = hdr && strcmp(hdr, "1") == 0;
        v[i].app_collector_game = col && strcmp(col, "1") == 0;
        free(id); free(hdr); free(col);
    }
    mlxr_xml_free(x);
    qsort(v, cnt, sizeof *v, cmp_app);
    *apps = v;
    *n = cnt;
    return MLXR_OK;
}

mlxr_status mlxr_server_get_apps(mlxr_server *s, mlxr_app **apps, size_t *n, mlxr_error *err)
{
    char *body;
    mlxr_status st = mlxr_http_get(s, true, "applist", NULL, REQUEST_TIMEOUT_MS, &body, err);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    if (st == MLXR_OK) st = mlxr_parse_applist(body, strlen(body), apps, n, err);
    free(body);
    return st;
}

void mlxr_apps_free(mlxr_app *apps, size_t n)
{
    if (!apps) return;
    for (size_t i = 0; i < n; i++) free(apps[i].name);
    free(apps);
}

/* ---- launch / resume / quit ---- */

mlxr_status mlxr_launch_params_init(mlxr_launch_params *p)
{
    memset(p, 0, sizeof *p);
    p->width = 1920;
    p->height = 1080;
    p->fps = 60;
    p->sops = true;
    p->audio_configuration = MLXR_AUDIO_STEREO;
    if (!mlxr_random(p->ri_key, sizeof p->ri_key) || !mlxr_random(p->ri_iv, 4)) return MLXR_E_CRYPTO;
    return MLXR_OK;
}

mlxr_status mlxr_parse_mode(const char *s, int *w, int *h, int *fps)
{
    int a, b, c;
    char tail;
    if (!s || sscanf(s, "%dx%dx%d%c", &a, &b, &c, &tail) != 3) return MLXR_E_ARG;
    if (a < 64 || a > 16384 || b < 64 || b > 16384 || c < 1 || c > 1000) return MLXR_E_ARG;
    *w = a; *h = b; *fps = c;
    return MLXR_OK;
}

int32_t mlxr_launch_rikeyid(const mlxr_launch_params *p)
{
    uint32_t v = (uint32_t)p->ri_iv[0] << 24 | (uint32_t)p->ri_iv[1] << 16 | (uint32_t)p->ri_iv[2] << 8 | p->ri_iv[3];
    return (int32_t)v;
}

mlxr_status mlxr_launch_query(int appid, const mlxr_launch_params *p, char *out, size_t cap)
{
    char rikey[33];
    mlxr_hex_encode(p->ri_key, 16, rikey);
    int ac = p->audio_configuration ? p->audio_configuration : MLXR_AUDIO_STEREO;
    int count = (ac >> 8) & 0xFF, mask = (ac >> 16) & 0xFFFF;
    int surround = (mask << 16) | count;
    /* Using an FPS value over 60 makes GFE's SOPS fall back to 720p60; Sunshine does not need the hack. */
    int fps = (p->fps > 60 && p->is_gfe) ? 0 : p->fps;
    int n = snprintf(out, cap,
                     "appid=%d&mode=%dx%dx%d&additionalStates=1&sops=%d&rikey=%s&rikeyid=%d%s"
                     "&localAudioPlayMode=%d&surroundAudioInfo=%d&remoteControllersBitmap=%d&gcmap=%d&gcpersist=%d%s",
                     appid, p->width, p->height, fps, p->sops ? 1 : 0, rikey, (int)mlxr_launch_rikeyid(p),
                     p->hdr ? "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0"
                              "&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0"
                            : "",
                     p->local_audio ? 1 : 0, surround, p->gamepad_mask, p->gamepad_mask, p->persist_gamepads ? 1 : 0,
                     p->extra_query ? p->extra_query : "&corever=1");
    OPENSSL_cleanse(rikey, sizeof rikey);
    return (n > 0 && (size_t)n < cap) ? MLXR_OK : MLXR_E_ARG;
}

mlxr_status mlxr_server_start_app(mlxr_server *s, bool resume, int appid, const mlxr_launch_params *p,
                                  char **rtsp_url, mlxr_error *err)
{
    char q[1024];
    if (rtsp_url) *rtsp_url = NULL;
    if (mlxr_launch_query(appid, p, q, sizeof q) != MLXR_OK) return mlxr_fail(err, MLXR_E_ARG, 0, "launch query too long");
    char *body;
    mlxr_status st = mlxr_http_get(s, true, resume ? "resume" : "launch", q,
                                   resume ? RESUME_TIMEOUT_MS : LAUNCH_TIMEOUT_MS, &body, err);
    OPENSSL_cleanse(q, sizeof q);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    if (st == MLXR_OK) {
        mlxr_xml *x = mlxr_xml_parse(body, strlen(body));
        char *url = mlxr_xml_get(x, "sessionUrl0");
        mlxr_xml_free(x);
        if (!url || !*url) {
            free(url);
            st = mlxr_fail(err, MLXR_E_XML, 0, "host accepted the request but sent no sessionUrl0");
        } else if (rtsp_url) {
            *rtsp_url = url;
        } else {
            free(url);
        }
    }
    free(body);
    return st;
}

mlxr_status mlxr_server_launch_or_resume(mlxr_server *s, const mlxr_computer *comp, int appid,
                                         const mlxr_launch_params *p, char **rtsp_url, mlxr_error *err)
{
    return mlxr_server_start_app(s, comp && comp->current_game != 0, appid, p, rtsp_url, err);
}

mlxr_status mlxr_server_quit_app(mlxr_server *s, mlxr_error *err)
{
    char *body;
    mlxr_status st = mlxr_http_get(s, true, "cancel", NULL, QUIT_TIMEOUT_MS, &body, err);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    free(body);
    if (st != MLXR_OK) return st;
    /* Newer GFE returns success even when we are not the owner: verify. */
    mlxr_computer c;
    st = mlxr_server_get_info(s, false, &c, NULL, err);
    if (st != MLXR_OK) return st;
    if (c.current_game != 0)
        return mlxr_fail(err, MLXR_E_BUSY_OTHER, 599, "the running session belongs to another client and cannot be quit");
    return MLXR_OK;
}

mlxr_status mlxr_server_unpair(mlxr_server *s, mlxr_error *err)
{
    char *body;
    mlxr_status st = mlxr_http_get(s, false, "unpair", NULL, REQUEST_TIMEOUT_MS, &body, err);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    free(body);
    return st;
}
