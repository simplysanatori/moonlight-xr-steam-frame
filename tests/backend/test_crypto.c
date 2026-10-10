/* Pairing crypto: key derivation vectors, AES-ECB (FIPS-197), signatures, and the
 * four hash/encrypt steps with a client and a server implemented independently here. GPLv3. */
#include "backend.h"
#include "check.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

static void unhex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void check_hex(const uint8_t *b, size_t n, const char *expect)
{
    char h[200];
    for (size_t i = 0; i < n; i++) sprintf(h + 2 * i, "%02x", b[i]);
    CHECK_EQ_STR(h, expect);
}

static void test_key_derivation(void)
{
    uint8_t salt[16], key[16];
    unhex("00112233445566778899aabbccddeeff", salt, 16);
    /* vectors from python3 hashlib: sha256/sha1(salt || b"1234")[:16] */
    mlxr_pair_derive_key(7, salt, "1234", key);
    check_hex(key, 16, "8c8fb15e510c23192b9ecceb9cc163a6");
    mlxr_pair_derive_key(9, salt, "1234", key);
    check_hex(key, 16, "8c8fb15e510c23192b9ecceb9cc163a6");
    mlxr_pair_derive_key(6, salt, "1234", key);
    check_hex(key, 16, "cfa2bc7964cf795aaae376489c5a09b7");
    mlxr_pair_derive_key(7, salt, "0000", key);
    check_hex(key, 16, "25bec9800c43d60d49dffa02b3ccd493");   /* leading zeros are part of the PIN */
    mlxr_pair_derive_key(0, salt, "1234", key);              /* unknown generation == pre-7 == SHA-1 */
    check_hex(key, 16, "cfa2bc7964cf795aaae376489c5a09b7");
}

static void test_aes(void)
{
    /* FIPS-197 appendix C.1 */
    uint8_t key[16], pt[16], ct[16], back[16];
    unhex("000102030405060708090a0b0c0d0e0f", key, 16);
    unhex("00112233445566778899aabbccddeeff", pt, 16);
    CHECK_OK(mlxr_aes128_ecb(true, key, pt, 16, ct));
    check_hex(ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a");
    CHECK_OK(mlxr_aes128_ecb(false, key, ct, 16, back));
    CHECK(memcmp(back, pt, 16) == 0);
    /* ECB: identical blocks encrypt identically; in-place works; bad length rejected */
    uint8_t two[32];
    memcpy(two, pt, 16); memcpy(two + 16, pt, 16);
    CHECK_OK(mlxr_aes128_ecb(true, key, two, 32, two));
    CHECK(memcmp(two, ct, 16) == 0 && memcmp(two + 16, ct, 16) == 0);
    CHECK_EQ_INT(mlxr_aes128_ecb(true, key, pt, 15, ct), MLXR_E_ARG);
    CHECK_EQ_INT(mlxr_aes128_ecb(true, key, pt, 0, ct), MLXR_E_ARG);
}

static void test_sign(void)
{
    mlxr_identity a, b;
    CHECK_OK(mlxr_identity_generate(&a, NULL));
    CHECK_OK(mlxr_identity_generate(&b, NULL));
    uint8_t msg[16] = "sixteen byte msg", sig[512];
    int sl = mlxr_sign_sha256(a.key_pem, msg, 16, sig, sizeof sig);
    CHECK_EQ_INT(sl, 256);
    CHECK(mlxr_verify_sha256(a.cert_pem, msg, 16, sig, (size_t)sl));
    CHECK(!mlxr_verify_sha256(b.cert_pem, msg, 16, sig, (size_t)sl));   /* wrong cert */
    msg[0] ^= 1;
    CHECK(!mlxr_verify_sha256(a.cert_pem, msg, 16, sig, (size_t)sl));   /* tampered */
    msg[0] ^= 1;
    sig[10] ^= 1;
    CHECK(!mlxr_verify_sha256(a.cert_pem, msg, 16, sig, (size_t)sl));
    CHECK_EQ_INT(mlxr_sign_sha256(a.key_pem, msg, 16, sig, 100), -1);   /* buffer too small */
    uint8_t cs[512];
    CHECK_EQ_INT(mlxr_cert_signature(a.cert_pem, cs, sizeof cs), 256);
    CHECK_EQ_INT(mlxr_cert_signature("not a cert", cs, sizeof cs), -1);
    char fp[65];
    CHECK_OK(mlxr_cert_fingerprint(a.cert_pem, fp));
    CHECK_EQ_INT(strlen(fp), 64);
    mlxr_identity_free(&a);
    mlxr_identity_free(&b);
}

/* One complete handshake of the cryptographic messages, client and server both
 * written here from the protocol description (not via nvhttp/pairing.c). */
static int run_handshake(int gen, const char *client_pin, const char *server_pin, bool *pin_ok, bool *mitm_ok)
{
    mlxr_identity cli, srv;
    CHECK_OK(mlxr_identity_generate(&cli, NULL));
    CHECK_OK(mlxr_identity_generate(&srv, NULL));
    size_t hl = gen >= 7 ? 32 : 20;
    uint8_t salt[16], ckey[16], skey[16];
    RAND_bytes(salt, 16);
    mlxr_pair_derive_key(gen, salt, client_pin, ckey);
    mlxr_pair_derive_key(gen, salt, server_pin, skey);

    uint8_t cli_sig[512], srv_sig[512];
    int cls = mlxr_cert_signature(cli.cert_pem, cli_sig, sizeof cli_sig);
    int sls = mlxr_cert_signature(srv.cert_pem, srv_sig, sizeof srv_sig);

    /* client: random challenge, AES-encrypted */
    uint8_t chal[16], enc[16], dec[16];
    RAND_bytes(chal, 16);
    mlxr_aes128_ecb(true, ckey, chal, 16, enc);
    /* server: decrypt with ITS key, answer H(chal || srvsig || srvsecret) || srvchallenge */
    mlxr_aes128_ecb(false, skey, enc, 16, dec);
    uint8_t ssecret[16], schal[16];
    RAND_bytes(ssecret, 16);
    RAND_bytes(schal, 16);
    uint8_t cat[16 + 512 + 16], h[32] = {0}, resp[48] = {0}, encresp[48], decresp[48];
    size_t n = 0;
    memcpy(cat, dec, 16); n += 16;
    memcpy(cat + n, srv_sig, (size_t)sls); n += (size_t)sls;
    memcpy(cat + n, ssecret, 16); n += 16;
    extern size_t mlxr_pair_hash(int, const uint8_t *, size_t, uint8_t *);
    mlxr_pair_hash(gen, cat, n, h);
    memcpy(resp, h, hl);
    memcpy(resp + hl, schal, 16);
    size_t tot = (hl + 16 + 15) / 16 * 16;
    mlxr_aes128_ecb(true, skey, resp, tot, encresp);
    /* client: decrypt, split */
    mlxr_aes128_ecb(false, ckey, encresp, tot, decresp);
    uint8_t srv_response[32];
    memcpy(srv_response, decresp, hl);
    /* client: answer H(srvchal || clientsig || clientsecret) padded to 32, encrypted */
    uint8_t csecret[16], ch[32] = {0}, ench[32], dech[32];
    RAND_bytes(csecret, 16);
    n = 0;
    memcpy(cat, decresp + hl, 16); n += 16;
    memcpy(cat + n, cli_sig, (size_t)cls); n += (size_t)cls;
    memcpy(cat + n, csecret, 16); n += 16;
    mlxr_pair_hash(gen, cat, n, ch);
    mlxr_aes128_ecb(true, ckey, ch, 32, ench);
    /* server: decrypt, remember; sends srvsecret + signature */
    mlxr_aes128_ecb(false, skey, ench, 32, dech);
    uint8_t ssig[256];
    int ssl_ = mlxr_sign_sha256(srv.key_pem, ssecret, 16, ssig, sizeof ssig);
    /* client: check the signature (MITM) and the PIN proof */
    *mitm_ok = mlxr_verify_sha256(srv.cert_pem, ssecret, 16, ssig, (size_t)ssl_);
    n = 0;
    memcpy(cat, chal, 16); n += 16;
    memcpy(cat + n, srv_sig, (size_t)sls); n += (size_t)sls;
    memcpy(cat + n, ssecret, 16); n += 16;
    uint8_t expect[32] = {0};
    mlxr_pair_hash(gen, cat, n, expect);
    *pin_ok = memcmp(expect, srv_response, hl) == 0;
    /* client sends csecret + signature; server verifies both signature and the hash it stored */
    uint8_t csig[256];
    int csl = mlxr_sign_sha256(cli.key_pem, csecret, 16, csig, sizeof csig);
    bool sig_ok = mlxr_verify_sha256(cli.cert_pem, csecret, 16, csig, (size_t)csl);
    n = 0;
    memcpy(cat, schal, 16); n += 16;
    memcpy(cat + n, cli_sig, (size_t)cls); n += (size_t)cls;
    memcpy(cat + n, csecret, 16); n += 16;
    uint8_t sexpect[32] = {0};
    mlxr_pair_hash(gen, cat, n, sexpect);
    bool server_accepts = sig_ok && memcmp(sexpect, dech, hl) == 0;
    mlxr_identity_free(&cli);
    mlxr_identity_free(&srv);
    return server_accepts;
}

static void test_handshake(void)
{
    bool pin_ok, mitm_ok;
    for (int gen = 6; gen <= 7; gen++) {
        CHECK(run_handshake(gen, "4321", "4321", &pin_ok, &mitm_ok));
        CHECK(pin_ok && mitm_ok);
        /* wrong PIN: client notices (server proof does not match) and the server rejects the client hash */
        CHECK(!run_handshake(gen, "4321", "4322", &pin_ok, &mitm_ok));
        CHECK(!pin_ok);
        CHECK(mitm_ok);   /* signature of the secret is independent of the PIN */
    }
}

static void test_pin(void)
{
    char pin[5];
    int seen[10] = {0};
    for (int i = 0; i < 500; i++) {
        CHECK_OK(mlxr_pair_generate_pin(pin));
        CHECK_EQ_INT(strlen(pin), 4);
        for (int j = 0; j < 4; j++) {
            CHECK(pin[j] >= '0' && pin[j] <= '9');
            seen[pin[j] - '0']++;
        }
    }
    for (int d = 0; d < 10; d++) CHECK(seen[d] > 0);
}

int main(void)
{
    test_key_derivation();
    test_aes();
    test_sign();
    test_handshake();
    test_pin();
    TEST_DONE();
}
