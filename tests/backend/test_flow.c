/* End-to-end through the real nvhttp/pairing code against the fake host:
 * serverinfo, the full pairing handshake, pinned HTTPS, applist, launch, resume, cancel. GPLv3. */
#include "backend.h"
#include "check.h"
#include "fake_host.h"

#include <openssl/pem.h>
#include <openssl/x509.h>

static mlxr_server *connect_to(const mlxr_identity *id, fake_host *h)
{
    mlxr_server *s = mlxr_server_new(id, "127.0.0.1", fake_host_http_port(h));
    return s;
}

static void test_pairing_flow(int gen, bool gfe)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "4821", .gen = gen, .gfe = gfe};
    fake_host *h = fake_host_start(&o);
    CHECK(h != NULL);
    mlxr_server *s = connect_to(&id, h);
    mlxr_server_set_gfe(s, gfe);

    /* unpaired: HTTP serverinfo */
    mlxr_computer c;
    char *raw = NULL;
    CHECK_OK(mlxr_server_get_info(s, false, &c, &raw, &e));
    CHECK(strstr(raw, "<hostname>fake-host</hostname>") != NULL);
    free(raw);
    CHECK_EQ_STR(c.name, "fake-host");
    CHECK(!c.via_https && !c.paired);
    CHECK_EQ_INT(c.https_port, fake_host_https_port(h));
    CHECK_EQ_INT(mlxr_server_https_port(s), fake_host_https_port(h));   /* learned from the answer */
    CHECK_EQ_INT(mlxr_parse_major_version(c.app_version), gen);
    CHECK_EQ_INT(c.is_nvidia, gfe);
    char uid[64];
    fake_host_last_uniqueid(h, uid, sizeof uid);
    CHECK_EQ_STR(uid, gfe ? "0123456789ABCDEF" : id.unique_id);

    /* HTTPS is refused until a cert is pinned */
    mlxr_app *apps;
    size_t n;
    CHECK_EQ_INT(mlxr_server_get_apps(s, &apps, &n, &e), MLXR_E_ARG);

    /* pair */
    char *cert = NULL;
    CHECK_OK(mlxr_pair(s, c.app_version, "4821", 10000, &cert, &e));
    CHECK(cert != NULL);
    CHECK_EQ_STR(cert, fake_host_cert_pem(h));
    CHECK(fake_host_is_paired(h));
    CHECK(mlxr_server_pinned_cert(s) != NULL);       /* left pinned to the host cert */

    /* paired: HTTPS serverinfo reports PairStatus */
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(c.via_https && c.paired);
    CHECK_OK(mlxr_server_get_apps(s, &apps, &n, &e));
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(apps[0].name, "Desktop");
    CHECK_EQ_INT(apps[0].id, 881448767);
    mlxr_apps_free(apps, n);

    /* launch */
    mlxr_launch_params p;
    CHECK_OK(mlxr_launch_params_init(&p));
    p.width = 2560; p.height = 1440; p.fps = 90; p.is_gfe = gfe;
    char *url = NULL;
    CHECK_OK(mlxr_server_launch_or_resume(s, &c, 881448767, &p, &url, &e));
    CHECK_EQ_STR(url, "rtspenc://127.0.0.1:48010");
    free(url);
    char q[1100], expect[1100];
    fake_host_last_launch(h, q, sizeof q);
    mlxr_launch_query(881448767, &p, expect, sizeof expect);
    CHECK(strstr(q, "uniqueid=") == q && strstr(q, "&uuid=") != NULL);
    CHECK(strstr(q, expect) != NULL && strcmp(strstr(q, expect), expect) == 0);   /* query ends with ours */
    CHECK_EQ_INT(fake_host_count(h, "launch"), 1);
    CHECK_EQ_INT(fake_host_current_game(h), 881448767);

    /* next time: serverinfo says busy -> resume */
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_EQ_INT(c.current_game, 881448767);
    CHECK_OK(mlxr_server_launch_or_resume(s, &c, 881448767, &p, &url, &e));
    free(url);
    CHECK_EQ_INT(fake_host_count(h, "resume"), 1);

    /* quit */
    CHECK_OK(mlxr_server_quit_app(s, &e));
    CHECK_EQ_INT(fake_host_current_game(h), 0);
    /* resume without a game: host error surfaces with its status */
    CHECK_EQ_INT(mlxr_server_start_app(s, true, 1, &p, &url, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 400);

    /* a fresh handle that only knows the pinned cert works over HTTPS */
    mlxr_server *s2 = connect_to(&id, h);
    CHECK_OK(mlxr_server_set_pinned_cert(s2, cert, &e));
    mlxr_server_set_https_port(s2, fake_host_https_port(h));
    CHECK_OK(mlxr_server_get_info(s2, true, &c, NULL, &e));
    CHECK(c.via_https && c.paired);

    /* a different client identity is not paired */
    mlxr_identity stranger;
    CHECK_OK(mlxr_identity_generate(&stranger, &e));
    mlxr_server *s3 = connect_to(&stranger, h);
    CHECK_OK(mlxr_server_set_pinned_cert(s3, cert, &e));
    mlxr_server_set_https_port(s3, fake_host_https_port(h));
    CHECK_OK(mlxr_server_get_info(s3, true, &c, NULL, &e));
    CHECK(c.via_https && !c.paired);
    CHECK_EQ_INT(mlxr_server_get_apps(s3, &apps, &n, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 401);

    /* unpair */
    CHECK_OK(mlxr_server_unpair(s, &e));
    CHECK(!fake_host_is_paired(h));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(!c.paired);

    free(cert);
    mlxr_server_free(s);
    mlxr_server_free(s2);
    mlxr_server_free(s3);
    mlxr_identity_free(&stranger);
    mlxr_identity_free(&id);
    fake_host_stop(h);
}

static void test_pairing_failures(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    mlxr_computer c;
    char *cert = NULL;

    /* wrong PIN */
    {
        fake_opts o = {.pin = "1111"};
        fake_host *h = fake_host_start(&o);
        mlxr_server *s = connect_to(&id, h);
        CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
        CHECK_EQ_INT(mlxr_pair(s, c.app_version, "2222", 10000, &cert, &e), MLXR_E_PIN_WRONG);
        CHECK(cert == NULL);
        CHECK(!fake_host_is_paired(h));
        CHECK(mlxr_server_pinned_cert(s) == NULL);       /* pin restored (none) */
        CHECK_EQ_INT(fake_host_count(h, "unpair"), 1);   /* host told to forget the attempt */
        mlxr_server_free(s);
        fake_host_stop(h);
    }
    /* host already pairing with someone else */
    {
        fake_opts o = {.busy_pairing = true};
        fake_host *h = fake_host_start(&o);
        mlxr_server *s = connect_to(&id, h);
        CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
        CHECK_EQ_INT(mlxr_pair(s, c.app_version, "1234", 10000, &cert, &e), MLXR_E_PAIR_BUSY);
        mlxr_server_free(s);
        fake_host_stop(h);
    }
    /* secret signature does not verify against the cert we were given: MITM */
    {
        fake_opts o = {.bad_secret_signature = true};
        fake_host *h = fake_host_start(&o);
        mlxr_server *s = connect_to(&id, h);
        CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
        CHECK_EQ_INT(mlxr_pair(s, c.app_version, "1234", 10000, &cert, &e), MLXR_E_MITM);
        CHECK(!fake_host_is_paired(h));
        mlxr_server_free(s);
        fake_host_stop(h);
    }
    /* bad PIN syntax */
    {
        mlxr_server *s = mlxr_server_new(&id, "127.0.0.1", 1);
        CHECK_EQ_INT(mlxr_pair(s, "7.1.0.0", "", 1000, &cert, &e), MLXR_E_ARG);
        CHECK_EQ_INT(mlxr_pair(s, "7.1.0.0", "1234", 1000, &cert, &e), MLXR_E_NET);   /* unreachable */
        mlxr_server_free(s);
    }
    mlxr_identity_free(&id);
}

static void test_pin_mismatch_and_fallback(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "9999"};
    fake_host *a = fake_host_start(&o), *b = fake_host_start(&o);
    mlxr_computer c;
    char *cert = NULL;
    mlxr_server *s = connect_to(&id, a);
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "9999", 10000, &cert, &e));
    mlxr_server_free(s);

    /* host B answers on its own ports but we pin host A's cert: TLS must fail on HTTPS requests */
    s = connect_to(&id, b);
    CHECK_OK(mlxr_server_set_pinned_cert(s, cert, &e));
    mlxr_server_set_https_port(s, fake_host_https_port(b));
    mlxr_app *apps;
    size_t n;
    CHECK_EQ_INT(mlxr_server_get_apps(s, &apps, &n, &e), MLXR_E_TLS);
    /* serverinfo falls back to HTTP and reports "not paired" (re-pair needed), like moonlight-qt */
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(!c.via_https && !c.paired);
    CHECK_EQ_INT(fake_host_count(b, "serverinfo"), 1);
    mlxr_launch_params p;
    mlxr_launch_params_init(&p);
    char *url;
    CHECK_EQ_INT(mlxr_server_start_app(s, false, 1, &p, &url, &e), MLXR_E_TLS);
    CHECK_EQ_INT(fake_host_current_game(b), 0);
    mlxr_server_free(s);

    /* pinned cert unparsable */
    s = connect_to(&id, a);
    CHECK_EQ_INT(mlxr_server_set_pinned_cert(s, "junk", &e), MLXR_E_CRYPTO);
    CHECK(mlxr_server_pinned_cert(s) == NULL);
    mlxr_server_free(s);

    /* server not listening */
    s = mlxr_server_new(&id, "127.0.0.1", 1);
    CHECK_EQ_INT(mlxr_server_get_info(s, true, &c, NULL, &e), MLXR_E_NET);
    mlxr_server_free(s);

    free(cert);
    fake_host_stop(a);
    fake_host_stop(b);
    mlxr_identity_free(&id);
}

static void test_quit_not_owner_and_misc(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "0042", .cancel_is_noop = true, .zero_mac = true, .app_title_extra = "Q&amp;A <3"};
    fake_host *h = fake_host_start(&o);
    mlxr_server *s = connect_to(&id, h);
    mlxr_computer c;
    char *cert;
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(!c.has_mac);
    CHECK_OK(mlxr_pair(s, c.app_version, "0042", 10000, &cert, &e));   /* leading zeros survive */
    mlxr_app *apps;
    size_t n;
    CHECK_OK(mlxr_server_get_apps(s, &apps, &n, &e));
    CHECK_EQ_INT(n, 3);
    mlxr_apps_free(apps, n);
    mlxr_launch_params p;
    mlxr_launch_params_init(&p);
    char *url;
    CHECK_OK(mlxr_server_start_app(s, false, 7, &p, &url, &e));
    free(url);
    CHECK_EQ_INT(mlxr_server_quit_app(s, &e), MLXR_E_BUSY_OTHER);
    CHECK_EQ_INT(e.status, 599);
    free(cert);
    mlxr_server_free(s);
    mlxr_identity_free(&id);
    fake_host_stop(h);
}

int main(void)
{
    test_pairing_flow(7, false);    /* Sunshine / modern: SHA-256 */
    test_pairing_flow(7, true);     /* GFE-style: placeholder uniqueid */
    test_pairing_flow(6, false);    /* pre-gen-7: SHA-1 */
    test_pairing_failures();
    test_pin_mismatch_and_fallback();
    test_quit_not_owner_and_misc();
    TEST_DONE();
}
