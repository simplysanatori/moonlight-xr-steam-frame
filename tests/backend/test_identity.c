/* Identity creation / reload and the hosts.ini store. GPLv3. */
#include "backend.h"
#include "check.h"

#include <ctype.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

static void test_identity(void)
{
    const char *dir = make_tmpdir();
    char sub[600], p[700];
    snprintf(sub, sizeof sub, "%s/new/nested", dir);   /* created recursively, mode 0700 */
    mlxr_identity a, b;
    bool created;
    mlxr_error e;
    CHECK_OK(mlxr_identity_load_or_create(sub, &a, &created, &e));
    CHECK(created);
    CHECK(strstr(a.cert_pem, "BEGIN CERTIFICATE") != NULL);
    CHECK(strstr(a.key_pem, "BEGIN PRIVATE KEY") != NULL);
    CHECK_EQ_INT(strlen(a.unique_id), 16);
    for (int i = 0; i < 16; i++) CHECK(isxdigit((unsigned char)a.unique_id[i]));

    struct stat st;
    snprintf(p, sizeof p, "%s/client.key", sub);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(stat(sub, &st) == 0 && (st.st_mode & 0777) == 0700);
    snprintf(p, sizeof p, "%s/uniqueid", sub);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0077) == 0);

    /* certificate properties mirror IdentityManager */
    X509 *x = NULL;
    BIO *bio = BIO_new_mem_buf(a.cert_pem, -1);
    x = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    CHECK(x != NULL);
    CHECK_EQ_INT(X509_get_version(x), 2);                         /* v3 */
    char cn[128] = "";
    X509_NAME_get_text_by_NID(X509_get_subject_name(x), NID_commonName, cn, sizeof cn);
    CHECK_EQ_STR(cn, "NVIDIA GameStream Client");
    EVP_PKEY *pk = X509_get_pubkey(x);
    CHECK_EQ_INT(EVP_PKEY_get_bits(pk), 2048);
    CHECK_EQ_INT(X509_self_signed(x, 1), 1);
    CHECK_EQ_INT(X509_NAME_cmp(X509_get_subject_name(x), X509_get_issuer_name(x)), 0);
    int days, secs;
    ASN1_TIME_diff(&days, &secs, NULL, X509_get0_notAfter(x));
    CHECK(days > 365 * 19 && days < 365 * 21);
    EVP_PKEY_free(pk);
    X509_free(x);

    /* reload is byte-identical */
    CHECK_OK(mlxr_identity_load_or_create(sub, &b, &created, &e));
    CHECK(!created);
    CHECK_EQ_STR(a.cert_pem, b.cert_pem);
    CHECK_EQ_STR(a.key_pem, b.key_pem);
    CHECK_EQ_STR(a.unique_id, b.unique_id);
    mlxr_identity_free(&b);

    /* missing uniqueid is regenerated, keypair kept */
    unlink(p);
    CHECK_OK(mlxr_identity_load_or_create(sub, &b, &created, &e));
    CHECK(!created);
    CHECK_EQ_STR(a.cert_pem, b.cert_pem);
    CHECK_EQ_INT(strlen(b.unique_id), 16);
    mlxr_identity_free(&b);

    /* garbage key -> a fresh identity replaces it */
    snprintf(p, sizeof p, "%s/client.key", sub);
    FILE *f = fopen(p, "w");
    fputs("garbage", f);
    fclose(f);
    CHECK_OK(mlxr_identity_load_or_create(sub, &b, &created, &e));
    CHECK(created);
    CHECK(strcmp(a.cert_pem, b.cert_pem) != 0);
    mlxr_identity_free(&b);

    /* mismatched cert/key (cert from another identity) -> regenerated */
    mlxr_identity other;
    CHECK_OK(mlxr_identity_generate(&other, &e));
    snprintf(p, sizeof p, "%s/client.pem", sub);
    f = fopen(p, "w");
    fputs(other.cert_pem, f);
    fclose(f);
    CHECK_OK(mlxr_identity_load_or_create(sub, &b, &created, &e));
    CHECK(created);
    CHECK(strcmp(other.cert_pem, b.cert_pem) != 0);
    mlxr_identity_free(&b);
    mlxr_identity_free(&other);
    mlxr_identity_free(&a);

    /* MLXR_CONFIG_DIR override */
    setenv("MLXR_CONFIG_DIR", sub, 1);
    char *cd = mlxr_config_dir(&e);
    CHECK(cd != NULL);
    CHECK_EQ_STR(cd, sub);
    free(cd);
    unsetenv("MLXR_CONFIG_DIR");
    setenv("HOME", dir, 1);
    cd = mlxr_config_dir(&e);
    snprintf(p, sizeof p, "%s/.config/moonlight-xr", dir);
    CHECK_EQ_STR(cd, p);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0700);
    free(cd);
}

static void test_hosts(void)
{
    const char *dir = make_tmpdir();
    mlxr_hosts h;
    mlxr_error e;
    CHECK_OK(mlxr_hosts_load(dir, &h, &e));      /* missing file == empty */
    CHECK_EQ_INT(h.n, 0);

    mlxr_host_record r = {0};
    strcpy(r.name, "gaming-pc");
    strcpy(r.uuid, "6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B");
    strcpy(r.address, "192.168.1.10");
    strcpy(r.extra_addresses, "100.64.0.5,203.0.113.7");
    r.has_mac = true;
    uint8_t mac[6] = {0x02, 0xf0, 0x2c, 0x5d, 0xa5, 0x10};
    memcpy(r.mac, mac, 6);
    r.http_port = 47989;
    r.https_port = 47984;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));      /* any cert will do as a pinned cert */
    CHECK_OK(mlxr_host_cert_save(dir, &r, id.cert_pem, &e));
    CHECK_EQ_STR(r.cert_path, "servercerts/6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B.pem");
    CHECK(mlxr_hosts_upsert(&h, &r) != NULL);
    mlxr_host_record r2 = r;
    strcpy(r2.uuid, "OTHER");
    strcpy(r2.name, "laptop\nevil = 1");           /* newline injection must not create keys */
    strcpy(r2.address, "10.0.0.9");
    r2.extra_addresses[0] = 0;
    r2.has_mac = false;
    r2.cert_path[0] = 0;
    CHECK(mlxr_hosts_upsert(&h, &r2) != NULL);
    CHECK_EQ_INT(h.n, 2);
    CHECK_OK(mlxr_hosts_save(dir, &h, &e));
    mlxr_hosts_free(&h);

    char p[700];
    struct stat st;
    snprintf(p, sizeof p, "%s/hosts.ini", dir);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0077) == 0);

    CHECK_OK(mlxr_hosts_load(dir, &h, &e));
    CHECK_EQ_INT(h.n, 2);
    mlxr_host_record *f = mlxr_hosts_find(&h, "192.168.1.10");
    CHECK(f != NULL);
    CHECK_EQ_STR(f->name, "gaming-pc");
    CHECK(f->has_mac && memcmp(f->mac, mac, 6) == 0);
    CHECK_EQ_INT(f->https_port, 47984);
    CHECK(mlxr_hosts_find(&h, "100.64.0.5") == f);          /* extra address */
    CHECK(mlxr_hosts_find(&h, "GAMING-PC") == f);          /* name, case-insensitive */
    CHECK(mlxr_hosts_find(&h, "6e1f0c2a-3b4d-4e5f-8a9b-0c1d2e3f4a5b") == f);
    CHECK(mlxr_hosts_find(&h, "10.9.9.9") == NULL);
    CHECK(mlxr_hosts_find_uuid(&h, "OTHER") != NULL);
    mlxr_host_record *o = mlxr_hosts_find_uuid(&h, "OTHER");
    CHECK_EQ_STR(o->name, "laptop evil = 1");
    CHECK(!o->has_mac);

    char *pem;
    CHECK_OK(mlxr_host_cert_load(dir, f, &pem, &e));
    CHECK_EQ_STR(pem, id.cert_pem);
    free(pem);
    CHECK_EQ_INT(mlxr_host_cert_load(dir, o, &pem, &e), MLXR_E_NOT_PAIRED);
    mlxr_host_record bad = *f;
    strcpy(bad.cert_path, "../../etc/passwd");
    CHECK_EQ_INT(mlxr_host_cert_load(dir, &bad, &pem, &e), MLXR_E_ARG);
    snprintf(p, sizeof p, "%s/servercerts/6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B.pem", dir);
    FILE *fp = fopen(p, "w");
    fputs("junk", fp);
    fclose(fp);
    CHECK_EQ_INT(mlxr_host_cert_load(dir, f, &pem, &e), MLXR_E_CRYPTO);

    /* upsert replaces by uuid */
    mlxr_host_record upd = *f;
    strcpy(upd.address, "192.168.1.99");
    mlxr_hosts_upsert(&h, &upd);
    CHECK_EQ_INT(h.n, 2);
    CHECK_EQ_STR(mlxr_hosts_find_uuid(&h, upd.uuid)->address, "192.168.1.99");
    mlxr_hosts_free(&h);
    mlxr_identity_free(&id);
}

int main(void)
{
    test_identity();
    test_hosts();
    TEST_DONE();
}
