/* XML parsing of recorded serverinfo / applist / pair / launch responses, plus request building. GPLv3. */
#include "backend.h"
#include "check.h"

static char *fx(const char *name)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", FIXTURE_DIR, name);
    char *s = slurp(p);
    if (!s) { fprintf(stderr, "missing fixture %s\n", p); exit(2); }
    return s;
}

static void test_xml(void)
{
    const char *d = "<?xml version=\"1.0\"?><root status_code=\"200\"><a>1</a><b><a>2</a><c>x &amp; y</c></b><e/></root>";
    mlxr_xml *x = mlxr_xml_parse(d, strlen(d));
    CHECK(x != NULL);
    char *a = mlxr_xml_get(x, "a");
    CHECK_EQ_STR(a, "1");                       /* first in document order */
    free(a);
    char *c = mlxr_xml_get(x, "c");
    CHECK_EQ_STR(c, "x & y");                   /* entities decoded */
    free(c);
    char *e = mlxr_xml_get(x, "e");
    CHECK_EQ_STR(e, "");                        /* empty element != missing */
    free(e);
    CHECK(mlxr_xml_get(x, "nope") == NULL);
    CHECK_EQ_INT(mlxr_xml_count(x, "a"), 2);
    int code; char msg[32];
    CHECK(mlxr_xml_status(x, &code, msg, sizeof msg));
    CHECK_EQ_INT(code, 200);
    mlxr_xml_free(x);
    CHECK(mlxr_xml_parse("", 0) == NULL);
    CHECK(mlxr_xml_parse("not xml at all", 14) == NULL);

    /* external entities are never fetched or expanded */
    const char *xxe = "<?xml version=\"1.0\"?><!DOCTYPE r [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><root status_code=\"200\"><v>&x;</v></root>";
    x = mlxr_xml_parse(xxe, strlen(xxe));
    char *v = x ? mlxr_xml_get(x, "v") : NULL;
    CHECK(!v || strstr(v, "root:") == NULL);
    free(v);
    mlxr_xml_free(x);
}

static void test_serverinfo(void)
{
    mlxr_computer c;
    mlxr_error e;
    char *s = fx("serverinfo_sunshine.xml");
    CHECK_OK(mlxr_computer_parse(s, strlen(s), 47989, &c, &e));
    CHECK_EQ_STR(c.name, "gaming-pc");
    CHECK_EQ_STR(c.uuid, "6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B");
    CHECK(c.has_mac && c.mac[0] == 0x02 && c.mac[1] == 0xf0 && c.mac[5] == 0x10);
    CHECK_EQ_STR(c.local_ip, "192.168.1.10");
    CHECK_EQ_INT(c.http_port, 47989);
    CHECK_EQ_INT(c.https_port, 47984);
    CHECK_EQ_INT(c.external_port, 47989);
    CHECK(!c.is_nvidia);
    CHECK(c.paired);
    CHECK_EQ_INT(c.current_game, 0);
    CHECK_EQ_STR(c.state, "SUNSHINE_SERVER_FREE");
    CHECK_EQ_STR(c.app_version, "7.1.431.-1");
    CHECK_EQ_STR(c.gfe_version, "3.23.0.74");
    CHECK_EQ_INT(c.server_codec_mode_support, 0x10301);
    CHECK_EQ_INT(c.max_luma_pixels_hevc, 1869449984);
    CHECK_EQ_INT(c.n_modes, 3);
    /* sorted ascending by w*h*refresh: 720p60, 720p120, 1080p60 -> 1280*720*120 == 110.6M vs 1920*1080*60 == 124.4M */
    CHECK(c.modes[0].width == 1280 && c.modes[0].refresh_rate == 60);
    CHECK(c.modes[1].width == 1280 && c.modes[1].refresh_rate == 120);
    CHECK(c.modes[2].width == 1920);
    CHECK_EQ_INT(mlxr_parse_major_version(c.app_version), 7);
    /* Native (lane artemis-settings): plain Sunshine has none of Apollo's fields */
    CHECK(!c.vdisplay_capable && !c.vdisplay_ready);
    CHECK_EQ_INT(c.permission, -1);
    CHECK_EQ_INT(c.n_server_cmds, 0);
    CHECK(!mlxr_computer_is_apollo(&c));
    free(s);

    /* Apollo (the owner's host, 2026-10-11, read-only serverinfo; address changed, a second command added) */
    s = fx("serverinfo_apollo.xml");
    CHECK_OK(mlxr_computer_parse(s, strlen(s), 47989, &c, &e));
    CHECK_EQ_STR(c.app_version, "7.1.431.-1");   /* Apollo reports Sunshine's version */
    CHECK(c.vdisplay_capable && c.vdisplay_ready);
    CHECK_EQ_INT(c.permission, 118693632);
    CHECK(c.permission & MLXR_PERM_CLIPBOARD_SET);
    CHECK(c.permission & MLXR_PERM_CLIPBOARD_READ);
    CHECK(c.permission & MLXR_PERM_SERVER_CMD);
    CHECK(c.permission & MLXR_PERM_LAUNCH);
    CHECK_EQ_INT(c.n_server_cmds, 2);
    CHECK_EQ_STR(c.server_cmds[0], "Bubbles");
    CHECK_EQ_STR(c.server_cmds[1], "Sleep PC");
    CHECK(mlxr_computer_is_apollo(&c));
    free(s);

    s = fx("serverinfo_sunshine_busy.xml");
    CHECK_OK(mlxr_computer_parse(s, strlen(s), 47989, &c, &e));
    CHECK_EQ_INT(c.current_game, 881448767);
    CHECK(!c.has_mac);                           /* 00:00:00:00:00:00 */
    CHECK_EQ_STR(c.local_ip, "");                /* loopback dropped */
    CHECK_EQ_INT(c.external_port, 48000);
    CHECK_EQ_STR(c.external_ip, "203.0.113.7");
    CHECK(!c.paired);
    free(s);

    s = fx("serverinfo_gfe.xml");
    CHECK_OK(mlxr_computer_parse(s, strlen(s), 47989, &c, &e));
    CHECK(c.is_nvidia);                          /* MJOLNIR */
    CHECK_EQ_INT(c.current_game, 0);             /* currentgame ignored unless *_SERVER_BUSY */
    CHECK_EQ_INT(c.server_codec_mode_support, 1);/* absent -> H.264 only */
    CHECK_EQ_INT(c.n_modes, 0);
    free(s);

    s = fx("serverinfo_old_gfe.xml");
    CHECK_OK(mlxr_computer_parse(s, strlen(s), 0, &c, &e));
    CHECK_EQ_STR(c.name, "UNKNOWN");
    CHECK_EQ_STR(c.gfe_version, "");
    CHECK_EQ_INT(c.current_game, 12);
    CHECK_EQ_INT(c.http_port, 47989);
    CHECK_EQ_INT(c.https_port, 47984);
    CHECK_EQ_INT(mlxr_parse_major_version(c.app_version), 5);
    free(s);

    s = fx("error_401.xml");
    CHECK_EQ_INT(mlxr_computer_parse(s, strlen(s), 47989, &c, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 401);
    CHECK(strstr(e.msg, "not authorized") != NULL);
    free(s);

    CHECK_EQ_INT(mlxr_computer_parse("<html>", 6, 47989, &c, &e), MLXR_E_XML);
    CHECK_EQ_INT(mlxr_computer_parse("<x/>", 4, 47989, &c, &e), MLXR_E_XML);   /* no <root> */
}

static void test_applist(void)
{
    mlxr_app *apps;
    size_t n;
    mlxr_error e;
    char *s = fx("applist_sunshine.xml");
    CHECK_OK(mlxr_parse_applist(s, strlen(s), &apps, &n, &e));
    CHECK_EQ_INT(n, 5);
    /* case-insensitive sort: "", Assassin's..., cuphead, Desktop, Steam */
    CHECK_EQ_STR(apps[0].name, "");
    CHECK_EQ_INT(apps[0].id, 5);                 /* <AppTitle/> -> empty, not an error */
    CHECK_EQ_STR(apps[1].name, "Assassin's Creed Origins & DLC");
    CHECK_EQ_INT(apps[1].id, 1439608285);
    CHECK(apps[1].hdr_supported);
    CHECK_EQ_STR(apps[2].name, "cuphead");
    CHECK(apps[2].app_collector_game && !apps[2].hdr_supported);
    CHECK_EQ_STR(apps[3].name, "Desktop");
    CHECK_EQ_INT(apps[3].id, 881448767);
    CHECK_EQ_STR(apps[4].name, "Steam Big Picture");
    mlxr_apps_free(apps, n);
    free(s);

    s = fx("applist_broken.xml");
    CHECK_EQ_INT(mlxr_parse_applist(s, strlen(s), &apps, &n, &e), MLXR_E_XML);
    free(s);
    CHECK_EQ_INT(mlxr_parse_applist("<root status_code=\"200\"></root>", 31, &apps, &n, &e), MLXR_OK);
    CHECK_EQ_INT(n, 0);
    mlxr_apps_free(apps, n);
}

static void test_status_and_pair_fixtures(void)
{
    mlxr_error e;
    char *s = fx("error_gfe_ffffffff.xml");
    extern mlxr_status mlxr_check_response(const char *, mlxr_error *);
    CHECK_EQ_INT(mlxr_check_response(s, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 418);                 /* GFE "Invalid" / 0xFFFFFFFF special case */
    free(s);
    s = fx("launch_error.xml");
    CHECK_EQ_INT(mlxr_check_response(s, &e), MLXR_E_HTTP);
    CHECK_EQ_INT(e.status, 400);
    free(s);
    CHECK_EQ_INT(mlxr_check_response("garbage", &e), MLXR_E_XML);

    s = fx("pair_stage1.xml");
    mlxr_xml *x = mlxr_xml_parse(s, strlen(s));
    char *p = mlxr_xml_get(x, "paired");
    CHECK_EQ_STR(p, "1");
    free(p);
    char *pc = mlxr_xml_get(x, "plaincert");
    CHECK(pc && strlen(pc) > 20);
    free(pc);
    mlxr_xml_free(x);
    free(s);

    s = fx("pair_not_paired.xml");
    x = mlxr_xml_parse(s, strlen(s));
    p = mlxr_xml_get(x, "paired");
    CHECK_EQ_STR(p, "0");
    free(p);
    CHECK(mlxr_xml_get(x, "plaincert") == NULL);
    mlxr_xml_free(x);
    free(s);

    s = fx("launch_ok.xml");
    x = mlxr_xml_parse(s, strlen(s));
    char *u = mlxr_xml_get(x, "sessionUrl0");
    CHECK_EQ_STR(u, "rtspenc://192.168.1.10:48010");
    free(u);
    mlxr_xml_free(x);
    free(s);
}

/* The host is never told about more than the headset shows: 90 Hz at most, SteamVR's rate if lower */
static void test_fps_cap(void)
{
    CHECK_EQ_INT(mlxr_fps_cap_for_display(0), 90);      /* not known yet */
    CHECK_EQ_INT(mlxr_fps_cap_for_display(90), 90);
    CHECK_EQ_INT(mlxr_fps_cap_for_display(72), 72);
    CHECK_EQ_INT(mlxr_fps_cap_for_display(120), 90);
    CHECK_EQ_INT(mlxr_fps_cap_for_display(360), 90);
    CHECK_EQ_INT(mlxr_fps_cap_for_display(5), 90);      /* nonsense is not believed */
    CHECK_EQ_INT(mlxr_fps_clamp(360, 90), 90);
    CHECK_EQ_INT(mlxr_fps_clamp(144, 72), 72);
    CHECK_EQ_INT(mlxr_fps_clamp(60, 90), 60);           /* below the cap stays */
    CHECK_EQ_INT(mlxr_fps_clamp(360, 0), 90);           /* no cap given: the ceiling */
    CHECK_EQ_INT(mlxr_fps_clamp(360, 1000), 90);        /* a cap cannot lift the ceiling */
    /* Native (lane artemis-settings): Match headset */
    CHECK_EQ_INT(mlxr_fps_for_request(60, 90, 1), 90);   /* the display's rate, whatever --mode said */
    CHECK_EQ_INT(mlxr_fps_for_request(90, 72, 1), 72);   /* a 72 Hz headset */
    CHECK_EQ_INT(mlxr_fps_for_request(90, 0, 1), 90);    /* not known yet: the ceiling */
    CHECK_EQ_INT(mlxr_fps_for_request(60, 72, 0), 60);   /* a fixed rate stays */
    CHECK_EQ_INT(mlxr_fps_for_request(144, 72, 0), 72);  /* ... under the cap */

    mlxr_launch_params p;
    CHECK_OK(mlxr_launch_params_init(&p));
    CHECK_EQ_INT(p.max_fps, 90);                        /* capped by default */
    char q[1024];
    p.width = 2560; p.height = 1440; p.fps = 360;
    CHECK_EQ_INT(mlxr_launch_fps(&p), 90);
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "mode=2560x1440x90&") != NULL);     /* width x height unchanged */
    p.fps = 144; p.max_fps = mlxr_fps_cap_for_display(72);
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "mode=2560x1440x72&") != NULL);
    p.fps = 60;
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "mode=2560x1440x60&") != NULL);
    p.fps = 360; p.max_fps = 360;                       /* not even a larger cap passes 90 */
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "mode=2560x1440x90&") != NULL);
}

static void test_launch_query(void)
{
    mlxr_launch_params p;
    CHECK_OK(mlxr_launch_params_init(&p));
    for (int i = 0; i < 16; i++) p.ri_key[i] = (uint8_t)(0xA0 + i);
    memset(p.ri_iv, 0, 16);
    p.ri_iv[0] = 0xFF; p.ri_iv[1] = 0x00; p.ri_iv[2] = 0x01; p.ri_iv[3] = 0x02;   /* negative as int32 */
    CHECK_EQ_INT(mlxr_launch_rikeyid(&p), (int32_t)0xFF000102);
    p.width = 2560; p.height = 1440; p.fps = 90;
    p.local_audio = true;
    p.gamepad_mask = 1;
    char q[1024];
    CHECK_OK(mlxr_launch_query(881448767, &p, q, sizeof q));
    CHECK_EQ_STR(q, "appid=881448767&mode=2560x1440x90&additionalStates=1&sops=1"
                    "&rikey=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf&rikeyid=-16776958"
                    "&localAudioPlayMode=1&surroundAudioInfo=196610&remoteControllersBitmap=1&gcmap=1&gcpersist=0&corever=1");
    p.audio_configuration = MLXR_AUDIO_51;
    p.hdr = true;
    p.is_gfe = true;
    p.fps = 120;
    p.max_fps = 144;                                              /* a raised cap, to reach the GFE rule */
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "mode=2560x1440x0&") != NULL);               /* GFE: fps > 60 forced to 0 */
    CHECK(strstr(q, "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0&") != NULL);
    CHECK(strstr(q, "surroundAudioInfo=4128774&") != NULL);     /* (0x3F<<16)|6 */
    p.extra_query = "&corever=2";
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "&corever=2") != NULL && strstr(q, "corever=1") == NULL);
    CHECK_EQ_INT(mlxr_launch_query(1, &p, q, 20), MLXR_E_ARG);   /* truncation is an error */

    test_fps_cap();

    /* Native (lane artemis-settings): Apollo's virtual display, scale factor, custom refresh rate */
    p.is_gfe = false;
    p.hdr = false;
    p.fps = 90;
    p.max_fps = 90;
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "scaleFactor") == NULL && strstr(q, "virtualDisplay") == NULL);  /* not asked: not sent */
    p.virtual_display = true;
    p.scale_factor = 150;
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "&mode=2560x1440x90&scaleFactor=150&virtualDisplay=1&additionalStates=1&") != NULL);
    p.virtual_display = false;
    p.scale_factor = 0;
    p.fps = 60;
    p.fps_x1000 = 59940;
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "&mode=2560x1440x59940&additionalStates") != NULL);   /* Artemis: fps*1000 */
    p.fps_x1000 = 60000;                                                    /* a whole rate stays as is */
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "&mode=2560x1440x60&") != NULL);
    p.fps = 72;
    p.max_fps = 72;                                                         /* a 72 Hz headset */
    p.fps_x1000 = 89910;
    CHECK_OK(mlxr_launch_query(1, &p, q, sizeof q));
    CHECK(strstr(q, "&mode=2560x1440x72&") != NULL);                        /* over the cap: the capped rate */
    p.fps_x1000 = 0;

    int w, h, f;
    CHECK_OK(mlxr_parse_mode("2560x1440x90", &w, &h, &f));
    CHECK(w == 2560 && h == 1440 && f == 90);
    CHECK_EQ_INT(mlxr_parse_mode("2560x1440", &w, &h, &f), MLXR_E_ARG);
    CHECK_EQ_INT(mlxr_parse_mode("1920x1080x60x", &w, &h, &f), MLXR_E_ARG);
    CHECK_EQ_INT(mlxr_parse_mode("0x0x0", &w, &h, &f), MLXR_E_ARG);
    CHECK_EQ_INT(mlxr_parse_mode(NULL, &w, &h, &f), MLXR_E_ARG);
}

int main(void)
{
    test_xml();
    test_serverinfo();
    test_applist();
    test_status_and_pair_fixtures();
    test_launch_query();
    TEST_DONE();
}
