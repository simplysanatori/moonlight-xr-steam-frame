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

/* What the host really receives: never more than the display's rate, 144 at most, 90 with the
 * display not known. Native (lane refresh): the ceiling was 90. */
static void test_launch_fps_capped(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "4821", .gen = 7};
    fake_host *h = fake_host_start(&o);
    mlxr_server *s = connect_to(&id, h);
    mlxr_computer c;
    char *cert = NULL;
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "4821", 10000, &cert, &e));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));

    static const struct { int asked, display_hz, sent; } cases[] = {
        {360, 0, 90}, {144, 90, 90}, {120, 120, 120}, {90, 90, 90}, {90, 72, 72}, {144, 72, 72}, {60, 72, 60}, {60, 0, 60},
        {144, 120, 120}, {144, 144, 144}, {120, 144, 120}, {360, 144, 144}, {360, 165, 144},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mlxr_launch_params p;
        CHECK_OK(mlxr_launch_params_init(&p));
        p.width = 2560; p.height = 1440; p.fps = cases[i].asked;
        p.max_fps = mlxr_fps_cap_for_display(cases[i].display_hz);
        char *url = NULL;
        CHECK_OK(mlxr_server_start_app(s, false, 881448767, &p, &url, &e));
        free(url);
        char q[1100], want[40];
        fake_host_last_launch(h, q, sizeof q);
        snprintf(want, sizeof want, "&mode=2560x1440x%d&", cases[i].sent);
        CHECK(strstr(q, want) != NULL);
        CHECK(strstr(q, "x360&") == NULL && (cases[i].sent == 144 || strstr(q, "x144&") == NULL));
        CHECK_OK(mlxr_server_quit_app(s, &e));
    }
    /* a caller that forgets the cap still cannot ask for more than 90 */
    mlxr_launch_params p;
    CHECK_OK(mlxr_launch_params_init(&p));
    p.width = 2560; p.height = 1440; p.fps = 360;
    char *url = NULL;
    CHECK_OK(mlxr_server_start_app(s, false, 881448767, &p, &url, &e));
    free(url);
    char q[1100];
    fake_host_last_launch(h, q, sizeof q);
    CHECK(strstr(q, "&mode=2560x1440x90&") != NULL);

    free(cert);
    mlxr_server_free(s);
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

static void test_expired_cert_still_pinned(void)
{
    /* Qt-style pinning ignores validity: an expired self-signed host cert that equals the pin is accepted */
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "5555", .expired_cert = true};
    fake_host *h = fake_host_start(&o);
    mlxr_server *s = connect_to(&id, h);
    mlxr_computer c;
    char *cert;
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "5555", 10000, &cert, &e));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(c.via_https && c.paired);
    /* ... while a *different* cert is refused even though this one is long expired too */
    fake_host *h2 = fake_host_start(&o);
    mlxr_server *s2 = connect_to(&id, h2);
    CHECK_OK(mlxr_server_set_pinned_cert(s2, cert, &e));
    mlxr_server_set_https_port(s2, fake_host_https_port(h2));
    mlxr_app *apps;
    size_t n;
    CHECK_EQ_INT(mlxr_server_get_apps(s2, &apps, &n, &e), MLXR_E_TLS);
    free(cert);
    mlxr_server_free(s);
    mlxr_server_free(s2);
    mlxr_identity_free(&id);
    fake_host_stop(h);
    fake_host_stop(h2);
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

/* Native (lane artemis-settings): Apollo's serverinfo fields, launch extensions and actions/clipboard */
static void test_apollo(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    fake_opts o = {.pin = "7777", .apollo = true};
    fake_host *h = fake_host_start(&o);
    mlxr_server *s = connect_to(&id, h);
    mlxr_computer c;
    char *cert;
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "7777", 10000, &cert, &e));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK(mlxr_computer_is_apollo(&c) && c.vdisplay_capable && c.vdisplay_ready);
    CHECK_EQ_INT(c.n_server_cmds, 1);
    CHECK_EQ_STR(c.server_cmds[0], "Bubbles");

    mlxr_launch_params p;
    mlxr_launch_params_init(&p);
    p.width = 2560; p.height = 1440; p.fps = 90;
    p.virtual_display = true;
    p.scale_factor = 120;
    char *url;
    CHECK_OK(mlxr_server_start_app(s, false, 881448767, &p, &url, &e));
    free(url);
    char q[1100];
    fake_host_last_launch(h, q, sizeof q);
    CHECK(strstr(q, "&mode=2560x1440x90&scaleFactor=120&virtualDisplay=1&") != NULL);

    /* clipboard: refused with no stream (Apollo's 403), then both ways while "streaming" */
    char *text = NULL;
    CHECK_EQ_INT(mlxr_server_get_clipboard(s, &text, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 403);
    CHECK_EQ_INT(mlxr_server_set_clipboard(s, "x", &e), MLXR_E_HTTP);
    fake_host_set_streaming(h, true);
    CHECK_OK(mlxr_server_set_clipboard(s, "hello from the Frame\nline 2 & more", &e));
    char got[256];
    fake_host_clipboard(h, got, sizeof got);
    CHECK_EQ_STR(got, "hello from the Frame\nline 2 & more");
    fake_host_set_clipboard(h, "copied on the PC");
    CHECK_OK(mlxr_server_get_clipboard(s, &text, &e));
    CHECK_EQ_STR(text, "copied on the PC");
    free(text);
    fake_host_set_clipboard(h, "");
    CHECK_OK(mlxr_server_get_clipboard(s, &text, &e));   /* an empty clipboard is not an error */
    CHECK_EQ_STR(text, "");
    free(text);
    free(cert);
    mlxr_server_free(s);
    fake_host_stop(h);

    /* without the permission: 401 */
    fake_opts o2 = {.pin = "7778", .apollo = true, .clipboard_denied = true};
    h = fake_host_start(&o2);
    s = connect_to(&id, h);
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "7778", 10000, &cert, &e));
    fake_host_set_streaming(h, true);
    CHECK_EQ_INT(mlxr_server_get_clipboard(s, &text, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 401);
    free(cert);
    mlxr_server_free(s);
    mlxr_identity_free(&id);
    fake_host_stop(h);
}

/* Native (lane wave4-apollo): the Host sheet's data from a live (fake) Apollo: the command list as
 * serverinfo gives it to a client with and without the Server Command permission, and the clipboard
 * on an Apollo that predates it (404) */
static void test_apollo_host_menu(void)
{
    mlxr_error e;
    mlxr_identity id;
    CHECK_OK(mlxr_identity_generate(&id, &e));
    mlxr_computer c;
    char *cert, *text = NULL;
    int idx = -1;

    fake_opts o = {.pin = "7781", .apollo = true, .more_cmds = true};
    fake_host *h = fake_host_start(&o);
    mlxr_server *s = connect_to(&id, h);
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "7781", 10000, &cert, &e));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_EQ_INT(c.n_server_cmds, 3);
    CHECK_EQ_STR(c.server_cmds[2], "Lock PC");
    CHECK_EQ_INT(mlxr_server_cmd_resolve(&c, "lock pc", &idx), MLXR_HOSTFEAT_OK);
    CHECK_EQ_INT(idx, 2);   /* the number the control stream runs it by */
    CHECK_EQ_INT(mlxr_clipboard_gate(&c, false), MLXR_HOSTFEAT_OK);
    /* a clone (the Host sheet's thread works on one) is as paired as the original, and outlives it */
    fake_host_set_streaming(h, true);
    mlxr_server *cl = mlxr_server_clone(s);
    CHECK(cl != NULL && mlxr_server_clone(NULL) == NULL);
    CHECK_EQ_INT(mlxr_server_https_port(cl), mlxr_server_https_port(s));
    mlxr_server_free(s);
    CHECK_OK(mlxr_server_set_clipboard(cl, "from the clone", &e));
    CHECK_OK(mlxr_server_get_clipboard(cl, &text, &e));
    CHECK_EQ_STR(text, "from the clone");
    free(text);
    text = NULL;
    mlxr_server_free(cl);
    free(cert);
    fake_host_stop(h);

    /* a client without the Server Command permission is told of none */
    fake_opts o2 = {.pin = "7782", .apollo = true, .cmd_denied = true};
    h = fake_host_start(&o2);
    s = connect_to(&id, h);
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "7782", 10000, &cert, &e));
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_EQ_INT(c.n_server_cmds, 0);
    CHECK(mlxr_computer_is_apollo(&c));
    CHECK_EQ_INT(mlxr_server_cmd_gate(&c, -1), MLXR_HOSTFEAT_NO_PERMISSION);
    CHECK_EQ_INT(mlxr_clipboard_gate(&c, true), MLXR_HOSTFEAT_OK);
    free(cert);
    mlxr_server_free(s);
    fake_host_stop(h);

    /* an Apollo without the clipboard endpoint: 404, which the sheet says as "not supported" */
    fake_opts o3 = {.pin = "7783", .apollo = true, .clipboard_404 = true};
    h = fake_host_start(&o3);
    s = connect_to(&id, h);
    CHECK_OK(mlxr_server_get_info(s, false, &c, NULL, &e));
    CHECK_OK(mlxr_pair(s, c.app_version, "7783", 10000, &cert, &e));
    fake_host_set_streaming(h, true);
    CHECK_EQ_INT(mlxr_server_get_clipboard(s, &text, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 404);
    CHECK_EQ_STR(mlxr_hostfeat_http_text(e.status), "Not supported by this host");
    CHECK_EQ_INT(mlxr_server_set_clipboard(s, "x", &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 404);
    free(cert);
    mlxr_server_free(s);
    mlxr_identity_free(&id);
    fake_host_stop(h);
}

int main(void)
{
    test_apollo();
    test_apollo_host_menu();
    test_pairing_flow(7, false);    /* Sunshine / modern: SHA-256 */
    test_pairing_flow(7, true);     /* GFE-style: placeholder uniqueid */
    test_pairing_flow(6, false);    /* pre-gen-7: SHA-1 */
    test_launch_fps_capped();
    test_pairing_failures();
    test_pin_mismatch_and_fallback();
    test_expired_cert_still_pinned();
    test_quit_not_owner_and_misc();
    TEST_DONE();
}
