/* Client identity: RSA-2048 self-signed cert, key and unique id (identitymanager.cpp). GPLv3. */
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

char *mlxr_config_dir(mlxr_error *err)
{
    char path[1024];
    const char *o = getenv("MLXR_CONFIG_DIR");
    if (o && *o) {
        snprintf(path, sizeof path, "%s", o);
    } else {
        const char *home = getenv("HOME");
        if (!home || !*home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : NULL;
        }
        if (!home) {
            mlxr_fail(err, MLXR_E_IO, 0, "no HOME and no MLXR_CONFIG_DIR");
            return NULL;
        }
        snprintf(path, sizeof path, "%s/.config/moonlight-xr", home);
    }
    if (mlxr_mkdir_p(path, 0700, err) != MLXR_OK) return NULL;
    return strdup(path);
}

static char *bio_to_str(BIO *b)
{
    char *p = NULL;
    long n = BIO_get_mem_data(b, &p);
    if (n <= 0) return NULL;
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    memcpy(s, p, (size_t)n);
    s[n] = 0;
    return s;
}

static void new_unique_id(char out[17])
{
    uint8_t r[8];
    mlxr_random(r, sizeof r);
    mlxr_hex_encode(r, 8, out);
}

mlxr_status mlxr_identity_generate(mlxr_identity *out, mlxr_error *err)
{
    memset(out, 0, sizeof *out);
    EVP_PKEY *pk = EVP_RSA_gen(2048);
    X509 *cert = X509_new();
    X509_NAME *name = X509_NAME_new();
    BIO *bk = BIO_new(BIO_s_mem()), *bc = BIO_new(BIO_s_mem());
    mlxr_status st = MLXR_E_CRYPTO;

    if (!pk || !cert || !name || !bk || !bc) goto done;
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 365 * 20);
    X509_set_pubkey(cert, pk);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"NVIDIA GameStream Client", -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    if (!X509_sign(cert, pk, EVP_sha256())) goto done;
    if (!PEM_write_bio_PrivateKey(bk, pk, NULL, NULL, 0, NULL, NULL) || !PEM_write_bio_X509(bc, cert)) goto done;
    out->key_pem = bio_to_str(bk);
    out->cert_pem = bio_to_str(bc);
    if (!out->key_pem || !out->cert_pem) goto done;
    new_unique_id(out->unique_id);
    st = MLXR_OK;
done:
    BIO_free(bk);
    BIO_free(bc);
    X509_NAME_free(name);
    X509_free(cert);
    EVP_PKEY_free(pk);
    if (st != MLXR_OK) {
        mlxr_identity_free(out);
        return mlxr_fail(err, st, 0, "identity generation failed");
    }
    return MLXR_OK;
}

void mlxr_identity_free(mlxr_identity *id)
{
    if (!id) return;
    if (id->key_pem) {
        OPENSSL_cleanse(id->key_pem, strlen(id->key_pem));
        free(id->key_pem);
    }
    free(id->cert_pem);
    memset(id, 0, sizeof *id);
}

static bool valid_uid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 16) return false;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static bool key_matches_cert(const char *cert_pem, const char *key_pem)
{
    X509 *x = mlxr_x509_from_pem(cert_pem);
    EVP_PKEY *k = mlxr_pkey_from_pem(key_pem);
    bool ok = x && k && X509_check_private_key(x, k) == 1;
    X509_free(x);
    EVP_PKEY_free(k);
    return ok;
}

mlxr_status mlxr_identity_load_or_create(const char *dir, mlxr_identity *out, bool *created, mlxr_error *err)
{
    char pc[1100], pk[1100], pu[1100];
    memset(out, 0, sizeof *out);
    if (created) *created = false;
    if (!dir) return mlxr_fail(err, MLXR_E_ARG, 0, "no config dir");
    mlxr_status st = mlxr_mkdir_p(dir, 0700, err);
    if (st != MLXR_OK) return st;
    snprintf(pc, sizeof pc, "%s/client.pem", dir);
    snprintf(pk, sizeof pk, "%s/client.key", dir);
    snprintf(pu, sizeof pu, "%s/uniqueid", dir);

    char *cert = NULL, *key = NULL, *uid = NULL;
    mlxr_read_file(pc, &cert, NULL, NULL);
    mlxr_read_file(pk, &key, NULL, NULL);
    mlxr_read_file(pu, &uid, NULL, NULL);

    if (!cert || !key || !key_matches_cert(cert, key)) {
        free(cert);
        free(key);
        mlxr_identity fresh;
        st = mlxr_identity_generate(&fresh, err);
        if (st != MLXR_OK) { free(uid); return st; }
        st = mlxr_write_file(pk, fresh.key_pem, strlen(fresh.key_pem), 0600, err);
        if (st == MLXR_OK) st = mlxr_write_file(pc, fresh.cert_pem, strlen(fresh.cert_pem), 0644, err);
        if (st != MLXR_OK) { mlxr_identity_free(&fresh); free(uid); return st; }
        cert = fresh.cert_pem;
        key = fresh.key_pem;
        if (created) *created = true;
        /* a new keypair gets a new unique id too */
        free(uid);
        uid = NULL;
    }

    if (uid) {
        mlxr_trim(uid);
        if (!valid_uid(uid)) { free(uid); uid = NULL; }
    }
    if (!uid) {
        uid = malloc(17);
        if (!uid) { free(cert); free(key); return mlxr_fail(err, MLXR_E_NOMEM, 0, "oom"); }
        new_unique_id(uid);
        char line[32];
        snprintf(line, sizeof line, "%s\n", uid);
        st = mlxr_write_file(pu, line, strlen(line), 0600, err);
        if (st != MLXR_OK) { free(cert); free(key); free(uid); return st; }
    }

    out->cert_pem = cert;
    out->key_pem = key;
    mlxr_strlcpy(out->unique_id, uid, sizeof out->unique_id);
    free(uid);
    return MLXR_OK;
}
