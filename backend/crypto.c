/* OpenSSL primitives used by identity, pairing and TLS pinning. GPLv3. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

void *mlxr_x509_from_pem(const char *pem)
{
    if (!pem) return NULL;
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    X509 *x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    BIO_free_all(b);
    return x;
}

void *mlxr_pkey_from_pem(const char *pem)
{
    if (!pem) return NULL;
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *k = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free_all(b);
    return k;
}

static char *b64(const uint8_t *in, size_t n)
{
    char *out = malloc(4 * ((n + 2) / 3) + 1);
    if (!out) return NULL;
    int l = EVP_EncodeBlock((unsigned char *)out, in, (int)n);
    out[l] = 0;
    return out;
}

char *mlxr_spki_pin(const char *cert_pem)
{
    X509 *x = mlxr_x509_from_pem(cert_pem);
    if (!x) return NULL;
    unsigned char *der = NULL;
    int dl = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(x), &der);
    X509_free(x);
    if (dl <= 0) return NULL;
    uint8_t h[32];
    SHA256(der, (size_t)dl, h);
    OPENSSL_free(der);
    char *e = b64(h, 32);
    if (!e) return NULL;
    char *out = malloc(strlen(e) + 16);
    if (out) sprintf(out, "sha256//%s", e);
    free(e);
    return out;
}

mlxr_status mlxr_cert_fingerprint(const char *cert_pem, char out[65])
{
    X509 *x = mlxr_x509_from_pem(cert_pem);
    if (!x) return MLXR_E_CRYPTO;
    uint8_t h[32];
    unsigned int hl = 0;
    int ok = X509_digest(x, EVP_sha256(), h, &hl);
    X509_free(x);
    if (!ok || hl != 32) return MLXR_E_CRYPTO;
    mlxr_hex_encode(h, 32, out);
    return MLXR_OK;
}

int mlxr_parse_major_version(const char *quad)
{
    return quad ? atoi(quad) : 0;
}

size_t mlxr_pair_hash(int server_major, const uint8_t *in, size_t len, uint8_t out[32])
{
    /* Gen 7+ uses SHA-256, earlier SHA-1 (padded to 32 bytes by the caller). */
    if (server_major >= 7) {
        SHA256(in, len, out);
        return 32;
    }
    SHA1(in, len, out);
    return 20;
}

void mlxr_pair_derive_key(int server_major, const uint8_t salt[16], const char *pin, uint8_t key[16])
{
    uint8_t buf[16 + 64], h[32];
    size_t pl = strlen(pin);
    if (pl > 64) pl = 64;
    memcpy(buf, salt, 16);
    memcpy(buf + 16, pin, pl);
    mlxr_pair_hash(server_major, buf, 16 + pl, h);
    memcpy(key, h, 16);
    OPENSSL_cleanse(buf, sizeof buf);
    OPENSSL_cleanse(h, sizeof h);
}

mlxr_status mlxr_aes128_ecb(bool encrypt, const uint8_t key[16], const uint8_t *in, size_t len, uint8_t *out)
{
    if (len == 0 || (len % 16)) return MLXR_E_ARG;
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    if (!c) return MLXR_E_NOMEM;
    int ol = 0, ok;
    ok = EVP_CipherInit_ex(c, EVP_aes_128_ecb(), NULL, key, NULL, encrypt ? 1 : 0);
    if (ok) {
        EVP_CIPHER_CTX_set_padding(c, 0);
        ok = EVP_CipherUpdate(c, out, &ol, in, (int)len) && (size_t)ol == len;
    }
    EVP_CIPHER_CTX_free(c);
    return ok ? MLXR_OK : MLXR_E_CRYPTO;
}

int mlxr_cert_signature(const char *cert_pem, uint8_t *out, size_t cap)
{
    X509 *x = mlxr_x509_from_pem(cert_pem);
    if (!x) return -1;
    const ASN1_BIT_STRING *sig = NULL;
    X509_get0_signature(&sig, NULL, x);
    int n = -1;
    if (sig && (size_t)ASN1_STRING_length(sig) <= cap) {
        n = ASN1_STRING_length(sig);
        memcpy(out, ASN1_STRING_get0_data(sig), (size_t)n);
    }
    X509_free(x);
    return n;
}

int mlxr_sign_sha256(const char *key_pem, const uint8_t *msg, size_t len, uint8_t *sig, size_t cap)
{
    EVP_PKEY *k = mlxr_pkey_from_pem(key_pem);
    if (!k) return -1;
    int ret = -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t sl = 0;
    if (ctx && EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, k) == 1 &&
        EVP_DigestSignUpdate(ctx, msg, len) == 1 && EVP_DigestSignFinal(ctx, NULL, &sl) == 1 && sl <= cap &&
        EVP_DigestSignFinal(ctx, sig, &sl) == 1)
        ret = (int)sl;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(k);
    return ret;
}

bool mlxr_verify_sha256(const char *cert_pem, const uint8_t *msg, size_t len, const uint8_t *sig, size_t siglen)
{
    X509 *x = mlxr_x509_from_pem(cert_pem);
    if (!x) return false;
    EVP_PKEY *pk = X509_get_pubkey(x);
    bool ok = false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (pk && ctx && EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pk) == 1 &&
        EVP_DigestVerifyUpdate(ctx, msg, len) == 1 && EVP_DigestVerifyFinal(ctx, sig, siglen) == 1)
        ok = true;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pk);
    X509_free(x);
    return ok;
}
