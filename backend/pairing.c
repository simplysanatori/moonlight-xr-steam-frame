/* The GameStream pairing handshake (nvpairingmanager.cpp). GPLv3.
 *
 *  1  getservercert   salt + client cert        -> server cert (blocks until the PIN is entered on the host)
 *  2  clientchallenge AES(key, rand16)          -> AES(key, H(rand16||srvsig||srvsecret) || srvchallenge16)
 *  3  serverchallengeresp AES(key, H(srvchallenge||clientsig||clientsecret)) -> srvsecret || RSA-sign(srvsecret)
 *  4  clientpairingsecret clientsecret || RSA-sign(clientsecret)
 *  5  pairchallenge   over HTTPS with the client cert, server cert now pinned
 * key = first 16 bytes of H(salt||PIN); H = SHA-256 (server gen >= 7) or SHA-1. */
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>

#define STEP_TIMEOUT_MS 5000
#define DEFAULT_PIN_TIMEOUT_MS (5 * 60 * 1000)

mlxr_status mlxr_pair_generate_pin(char pin[5])
{
    uint32_t v;
    do {   /* rejection sampling: no modulo bias */
        if (!mlxr_random(&v, sizeof v)) return MLXR_E_CRYPTO;
    } while (v >= 4294960000u);
    snprintf(pin, 5, "%04u", v % 10000u);
    return MLXR_OK;
}

/* One pairing request over HTTP; returns the parsed doc only when status 200 and paired=1. */
static mlxr_status pair_step(mlxr_server *s, bool https, const char *args, long to, int stage, mlxr_xml **doc,
                             mlxr_error *err)
{
    char *body;
    *doc = NULL;
    char full[8192];
    if (snprintf(full, sizeof full, "devicename=roth&updateState=1&%s", args) >= (int)sizeof full)
        return mlxr_fail(err, MLXR_E_ARG, 0, "pair request too long");
    mlxr_status st = mlxr_http_get(s, https, "pair", full, to, &body, err);
    if (st != MLXR_OK) return st;
    st = mlxr_check_response(body, err);
    if (st != MLXR_OK) { free(body); return st; }
    mlxr_xml *x = mlxr_xml_parse(body, strlen(body));
    free(body);
    char *paired = mlxr_xml_get(x, "paired");
    bool ok = paired && strcmp(paired, "1") == 0;
    free(paired);
    if (!ok) {
        mlxr_xml_free(x);
        return mlxr_fail(err, MLXR_E_PAIR_FAILED, stage, "pairing refused by the host at stage %d", stage);
    }
    *doc = x;
    return MLXR_OK;
}

/* hex field of a response -> malloc'd bytes */
static uint8_t *xml_hex(const mlxr_xml *x, const char *tag, size_t *len)
{
    char *h = mlxr_xml_get(x, tag);
    uint8_t *b = h ? mlxr_hex_decode_alloc(h, len) : NULL;
    free(h);
    return b;
}

static void abort_pairing(mlxr_server *s)
{
    mlxr_server_unpair(s, NULL);
}

mlxr_status mlxr_pair(mlxr_server *s, const char *app_version, const char *pin, unsigned timeout_ms,
                      char **server_cert_pem, mlxr_error *err)
{
    mlxr_status st;
    mlxr_xml *x = NULL;
    uint8_t *tmpb = NULL;
    char *srv_pem = NULL, *old_pem = NULL;
    char *argbuf = NULL;
    uint8_t key[16];
    *server_cert_pem = NULL;

    if (!pin || strlen(pin) < 1 || strlen(pin) > 8) return mlxr_fail(err, MLXR_E_ARG, 0, "bad PIN");
    int major = mlxr_parse_major_version(app_version);
    mlxr_log(1, "pairing with server generation %d", major);

    uint8_t salt[16], rand_challenge[16], client_secret[16];
    if (!mlxr_random(salt, 16) || !mlxr_random(rand_challenge, 16) || !mlxr_random(client_secret, 16))
        return mlxr_fail(err, MLXR_E_CRYPTO, 0, "RNG failure");
    mlxr_pair_derive_key(major, salt, pin, key);
    const size_t hash_len = major >= 7 ? 32 : 20;

    char *cert_hex = mlxr_hex_encode_alloc((const uint8_t *)s->client_cert, strlen(s->client_cert));
    argbuf = malloc(strlen(cert_hex) + 128);
    if (!cert_hex || !argbuf) { free(cert_hex); free(argbuf); return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); }
    char salt_hex[33];
    mlxr_hex_encode(salt, 16, salt_hex);
    sprintf(argbuf, "phrase=getservercert&salt=%s&clientcert=%s", salt_hex, cert_hex);
    free(cert_hex);

    /* ---- stage 1 ---- */
    st = pair_step(s, false, argbuf, timeout_ms ? (long)timeout_ms : DEFAULT_PIN_TIMEOUT_MS, 1, &x, err);
    free(argbuf);
    argbuf = NULL;
    if (st != MLXR_OK) goto out;

    size_t srv_len = 0;
    tmpb = xml_hex(x, "plaincert", &srv_len);
    mlxr_xml_free(x);
    x = NULL;
    if (!tmpb) {
        /* an empty plaincert means the host is already pairing with someone */
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_PAIR_BUSY, 1, "server likely already pairing; try again");
        goto out;
    }
    srv_pem = malloc(srv_len + 1);
    if (!srv_pem) { st = mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); goto out; }
    memcpy(srv_pem, tmpb, srv_len);
    srv_pem[srv_len] = 0;
    free(tmpb);
    tmpb = NULL;

    uint8_t srv_sig[1024], cli_sig[1024];
    int srv_sig_len = mlxr_cert_signature(srv_pem, srv_sig, sizeof srv_sig);
    int cli_sig_len = mlxr_cert_signature(s->client_cert, cli_sig, sizeof cli_sig);
    if (srv_sig_len < 0 || cli_sig_len < 0) {
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_PAIR_FAILED, 1, "failed to parse the server certificate (plaincert)");
        goto out;
    }

    /* Pin this cert for TLS until pairing completes; restored on failure. */
    old_pem = s->pinned_pem ? strdup(s->pinned_pem) : NULL;
    st = mlxr_server_set_pinned_cert(s, srv_pem, err);
    if (st != MLXR_OK) { abort_pairing(s); goto out; }

    /* ---- stage 2: client challenge ---- */
    uint8_t enc[48];
    char hex[2 * 64 + 1], args[2 * 64 + 64];
    if (mlxr_aes128_ecb(true, key, rand_challenge, 16, enc) != MLXR_OK) { st = mlxr_fail(err, MLXR_E_CRYPTO, 0, "aes"); goto restore; }
    mlxr_hex_encode(enc, 16, hex);
    snprintf(args, sizeof args, "clientchallenge=%s", hex);
    st = pair_step(s, false, args, STEP_TIMEOUT_MS, 2, &x, err);
    if (st != MLXR_OK) { if (st == MLXR_E_PAIR_FAILED) abort_pairing(s); goto restore; }

    size_t cr_len = 0;
    uint8_t *cr = xml_hex(x, "challengeresponse", &cr_len);
    mlxr_xml_free(x);
    x = NULL;
    uint8_t dec[256];
    if (!cr || cr_len % 16 || cr_len > sizeof dec || mlxr_aes128_ecb(false, key, cr, cr_len, dec) != MLXR_OK ||
        cr_len < hash_len + 16) {
        free(cr);
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_PAIR_FAILED, 2, "invalid challengeresponse at stage 2");
        goto restore;
    }
    free(cr);

    uint8_t server_response[32];
    memcpy(server_response, dec, hash_len);

    /* ---- stage 3: server challenge response ---- */
    uint8_t cat[16 + 1024 + 16], h[32];
    size_t cl = 0;
    memcpy(cat + cl, dec + hash_len, 16); cl += 16;                  /* server challenge */
    memcpy(cat + cl, cli_sig, (size_t)cli_sig_len); cl += (size_t)cli_sig_len;
    memcpy(cat + cl, client_secret, 16); cl += 16;
    memset(h, 0, sizeof h);                                           /* SHA-1 digests are zero padded to 32 */
    mlxr_pair_hash(major, cat, cl, h);
    if (mlxr_aes128_ecb(true, key, h, 32, enc) != MLXR_OK) { st = mlxr_fail(err, MLXR_E_CRYPTO, 0, "aes"); goto restore; }
    mlxr_hex_encode(enc, 32, hex);
    snprintf(args, sizeof args, "serverchallengeresp=%s", hex);
    st = pair_step(s, false, args, STEP_TIMEOUT_MS, 3, &x, err);
    if (st != MLXR_OK) { if (st == MLXR_E_PAIR_FAILED) abort_pairing(s); goto restore; }

    size_t ps_len = 0;
    uint8_t *ps = xml_hex(x, "pairingsecret", &ps_len);
    mlxr_xml_free(x);
    x = NULL;
    if (!ps || ps_len <= 16) {
        free(ps);
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_PAIR_FAILED, 3, "invalid pairingsecret at stage 3");
        goto restore;
    }
    if (!mlxr_verify_sha256(srv_pem, ps, 16, ps + 16, ps_len - 16)) {
        free(ps);
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_MITM, 3, "server secret signature invalid: possible man-in-the-middle");
        goto restore;
    }

    /* does the server know our PIN?  H(rand_challenge || server cert sig || server secret) == server_response */
    cl = 0;
    memcpy(cat + cl, rand_challenge, 16); cl += 16;
    memcpy(cat + cl, srv_sig, (size_t)srv_sig_len); cl += (size_t)srv_sig_len;
    memcpy(cat + cl, ps, 16); cl += 16;
    free(ps);
    uint8_t expect[32] = {0};
    mlxr_pair_hash(major, cat, cl, expect);
    if (CRYPTO_memcmp(expect, server_response, hash_len) != 0) {
        abort_pairing(s);
        st = mlxr_fail(err, MLXR_E_PIN_WRONG, 3, "incorrect PIN");
        goto restore;
    }

    /* ---- stage 4: client pairing secret ---- */
    uint8_t cps[16 + 512];
    memcpy(cps, client_secret, 16);
    int sl = mlxr_sign_sha256(s->client_key, client_secret, 16, cps + 16, sizeof cps - 16);
    if (sl < 0) { abort_pairing(s); st = mlxr_fail(err, MLXR_E_CRYPTO, 4, "signing failed"); goto restore; }
    char *cps_hex = mlxr_hex_encode_alloc(cps, 16 + (size_t)sl);
    argbuf = malloc(strlen(cps_hex) + 32);
    if (!cps_hex || !argbuf) { free(cps_hex); st = mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); goto restore; }
    sprintf(argbuf, "clientpairingsecret=%s", cps_hex);
    free(cps_hex);
    st = pair_step(s, false, argbuf, STEP_TIMEOUT_MS, 4, &x, err);
    free(argbuf);
    argbuf = NULL;
    mlxr_xml_free(x);
    x = NULL;
    if (st != MLXR_OK) { if (st == MLXR_E_PAIR_FAILED) abort_pairing(s); goto restore; }

    /* ---- stage 5: prove the paired identity over HTTPS ---- */
    if (!s->https_port) s->https_port = MLXR_DEFAULT_HTTPS_PORT;
    st = pair_step(s, true, "phrase=pairchallenge", STEP_TIMEOUT_MS, 5, &x, err);
    mlxr_xml_free(x);
    x = NULL;
    if (st != MLXR_OK) { if (st == MLXR_E_PAIR_FAILED) abort_pairing(s); goto restore; }

    *server_cert_pem = srv_pem;
    srv_pem = NULL;
    st = MLXR_OK;
    goto out;

restore:
    mlxr_server_set_pinned_cert(s, old_pem, NULL);
out:
    mlxr_xml_free(x);
    free(tmpb);
    free(argbuf);
    free(srv_pem);
    free(old_pem);
    OPENSSL_cleanse(key, sizeof key);
    OPENSSL_cleanse(client_secret, sizeof client_secret);
    return st;
}
