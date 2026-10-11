/* Network path choice: preference precedence, classifying addresses with fake interface tables,
 * scoring, the fallbacks and their toasts, candidate gathering, and the real probe against the
 * fake host. GPLv3. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#include "backend.h"
#include "check.h"
#include "fake_host.h"
#include "netpath.h"

static uint32_t ip(const char *s) { struct in_addr a; inet_pton(AF_INET, s, &a); return ntohl(a.s_addr); }

static void add_if(mlxr_netifs *t, const char *name, const char *addr, int bits, bool wifi, int type, bool defroute)
{
    mlxr_netif *i = &t->v[t->n++];
    memset(i, 0, sizeof *i);
    snprintf(i->name, sizeof i->name, "%s", name);
    i->ifindex = t->n;
    i->up = true;
    i->loopback = !strcmp(name, "lo");
    i->wifi = wifi;
    i->wifi_type = type;
    i->addr = ip(addr);
    i->mask = bits ? 0xffffffffu << (32 - bits) : 0;
    i->default_route = defroute;
}

/* The Frame as measured: Wi-Fi client with the default route, its own AP, an idle USB ethernet */
static void frame_table(mlxr_netifs *t, bool nl80211)
{
    memset(t, 0, sizeof *t);
    t->nl80211 = nl80211;
    add_if(t, "lo", "127.0.0.1", 8, false, 0, false);
    add_if(t, "wlan0", "192.0.2.10", 24, true, nl80211 ? 1 : 0, true);
    add_if(t, "wlanap", "198.51.100.1", 24, true, nl80211 ? 2 : 0, false);
}

static void test_pref(void)
{
    mlxr_net_pref p;
    CHECK(mlxr_net_pref_parse("auto", &p) && p == MLXR_NET_AUTO);
    CHECK(mlxr_net_pref_parse(" Home\n", &p) && p == MLXR_NET_HOME);
    CHECK(mlxr_net_pref_parse("ADAPTER", &p) && p == MLXR_NET_ADAPTER);
    p = MLXR_NET_HOME;
    CHECK(!mlxr_net_pref_parse("wifi", &p) && p == MLXR_NET_HOME);
    CHECK(!mlxr_net_pref_parse("", &p) && !mlxr_net_pref_parse(NULL, &p) && !mlxr_net_pref_parse("home adapter", &p));

    const char *src, *warn;
    CHECK(mlxr_net_pref_resolve(NULL, NULL, NULL, &src, &warn) == MLXR_NET_AUTO);
    CHECK_EQ_STR(src, "default");
    CHECK(warn == NULL);
    CHECK(mlxr_net_pref_resolve(NULL, NULL, "adapter", &src, &warn) == MLXR_NET_ADAPTER);
    CHECK_EQ_STR(src, "config");
    CHECK(mlxr_net_pref_resolve("home", NULL, "adapter", &src, &warn) == MLXR_NET_HOME);   /* the flag beats the config */
    CHECK_EQ_STR(src, "flag");
    CHECK(mlxr_net_pref_resolve("home", "adapter", "auto", &src, &warn) == MLXR_NET_ADAPTER);   /* the env beats both */
    CHECK_EQ_STR(src, "env");
    CHECK(mlxr_net_pref_resolve("", "", "home", &src, &warn) == MLXR_NET_HOME);
    CHECK_EQ_STR(src, "config");
    /* an invalid one is skipped, with a warning */
    CHECK(mlxr_net_pref_resolve("bogus", NULL, "adapter", &src, &warn) == MLXR_NET_ADAPTER);
    CHECK_EQ_STR(src, "config");
    CHECK(warn && strstr(warn, "--network"));
    CHECK(mlxr_net_pref_resolve(NULL, "bogus", NULL, &src, &warn) == MLXR_NET_AUTO);
    CHECK(warn && strstr(warn, "MLXR_NETWORK") && !strcmp(src, "default"));
    CHECK_EQ_STR(mlxr_net_pref_name(MLXR_NET_ADAPTER), "adapter");
    CHECK_EQ_STR(mlxr_net_path_label(MLXR_PATH_HOME), "Wi-Fi");
    CHECK_EQ_STR(mlxr_net_path_label(MLXR_PATH_ADAPTER), "Steam Frame Wireless Adapter");
}

static void test_classify(void)
{
    mlxr_netifs t;
    const mlxr_netif *via;
    for (int nl = 1; nl >= 0; nl--) {   /* with nl80211's modes, and with only the heuristic */
        frame_table(&t, nl);
        CHECK(mlxr_net_classify(&t, "198.51.100.27", &via) == MLXR_PATH_ADAPTER && via && !strcmp(via->name, "wlanap"));
        CHECK(mlxr_net_classify(&t, "192.0.2.10", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "wlan0"));
        /* off-link: the default route's interface */
        CHECK(mlxr_net_classify(&t, "203.0.113.9", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "wlan0"));
        CHECK(mlxr_net_classify(&t, "203.0.113.5", NULL) == MLXR_PATH_HOME);
        /* names and IPv6 take the default route too */
        CHECK(mlxr_net_classify(&t, "DESKTOP-EXAMPLE.local", &via) == MLXR_PATH_HOME && via);
        CHECK(mlxr_net_classify(&t, "fe80::1", NULL) == MLXR_PATH_HOME);
    }
    /* the AP gets a second address in another subnet */
    frame_table(&t, true);
    add_if(&t, "wlanap", "10.35.99.1", 24, true, 2, false);
    CHECK(mlxr_net_classify(&t, "10.35.99.5", NULL) == MLXR_PATH_ADAPTER);
    /* the longest matching subnet wins (a /16 home network containing a /24 AP) */
    frame_table(&t, true);
    t.v[1].addr = ip("10.35.0.2");
    t.v[1].mask = 0xffff0000u;
    CHECK(mlxr_net_classify(&t, "198.51.100.27", NULL) == MLXR_PATH_ADAPTER);
    CHECK(mlxr_net_classify(&t, "10.35.1.1", NULL) == MLXR_PATH_HOME);
    /* an interface that is down does not count */
    frame_table(&t, true);
    t.v[2].up = false;
    CHECK(mlxr_net_classify(&t, "198.51.100.27", NULL) == MLXR_PATH_HOME);
    /* ethernet only: everything is home; loopback too */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_if(&t, "eth0", "192.168.1.5", 24, false, 0, true);
    CHECK(mlxr_net_classify(&t, "192.168.1.9", NULL) == MLXR_PATH_HOME);
    CHECK(mlxr_net_classify(&t, "198.51.100.27", NULL) == MLXR_PATH_HOME);
    CHECK(mlxr_net_classify(&t, "127.0.0.1", NULL) == MLXR_PATH_HOME);
    /* a managed Wi-Fi that is the only radio is not an AP even without the default route */
    memset(&t, 0, sizeof t);
    add_if(&t, "wlan0", "192.0.2.10", 24, true, 0, false);
    CHECK(mlxr_net_classify(&t, "192.0.2.10", NULL) == MLXR_PATH_HOME);
    /* no interfaces: home */
    memset(&t, 0, sizeof t);
    CHECK(mlxr_net_classify(&t, "10.0.0.1", &via) == MLXR_PATH_HOME && !via);

    /* classify a plan, the ifname follows */
    frame_table(&t, true);
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "default", 47989, "", "");
    mlxr_net_plan_add(&p, "198.51.100.27", 0);
    mlxr_net_plan_add(&p, "192.0.2.10", 0);
    mlxr_net_plan_classify(&p, &t);
    CHECK(p.c[0].path == MLXR_PATH_ADAPTER && !strcmp(p.c[0].ifname, "wlanap"));
    CHECK(p.c[1].path == MLXR_PATH_HOME && !strcmp(p.c[1].ifname, "wlan0"));
}

static void test_candidates(void)
{
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "default", 0, "u", "");
    CHECK(p.default_port == 47989 && p.budget_ms == 1500);
    CHECK(mlxr_net_plan_add(&p, "198.51.100.27", 0) == 0);
    CHECK(mlxr_net_plan_add(&p, " 192.0.2.10 ", 0) == 1);
    CHECK(mlxr_net_plan_add(&p, "198.51.100.27", 0) == 0);          /* deduped */
    CHECK(mlxr_net_plan_add(&p, "198.51.100.27", 47989) == 0);      /* the default port is the same candidate */
    CHECK(mlxr_net_plan_add(&p, "198.51.100.27", 50000) == 2);      /* another port is not */
    CHECK(mlxr_net_plan_add(&p, "", 0) == -1 && mlxr_net_plan_add(&p, NULL, 0) == -1);
    mlxr_net_plan_add_record(&p, "192.0.2.10", "203.0.113.5, 198.51.100.27,203.0.113.9");
    CHECK_EQ_INT(p.n, 5);
    CHECK_EQ_STR(p.c[3].address, "203.0.113.5");
    CHECK_EQ_STR(p.c[4].address, "203.0.113.9");
    for (int i = 0; i < 40; i++) {
        char a[32];
        snprintf(a, sizeof a, "10.9.9.%d", i);
        mlxr_net_plan_add(&p, a, 0);
    }
    CHECK_EQ_INT(p.n, MLXR_NET_MAX_CANDS);
}

static mlxr_net_cand mk(mlxr_net_path path, const char *addr, int ok, int failed, double rtt, double jit, double quality)
{
    mlxr_net_cand c;
    memset(&c, 0, sizeof c);
    snprintf(c.address, sizeof c.address, "%s", addr);
    c.path = path;
    c.probed = true;
    c.sent = ok + failed;
    c.ok = ok;
    c.failed = failed;
    for (int i = 0; i < ok && i < MLXR_NET_MAX_SAMPLES; i++) c.rtt_ms[c.n_rtt++] = rtt + (i % 2 ? jit : -jit);
    c.link_quality = quality;
    return c;
}

static void set_link(mlxr_net_cand *c, int dbm, double mbps)
{
    c->link.valid = c->link.wifi = true;
    c->link.signal_dbm = dbm;
    c->link.tx_mbps = mbps;
}

static void test_stats_and_scoring(void)
{
    mlxr_net_cand c = mk(MLXR_PATH_HOME, "a", 5, 0, 10.0, 1.0, 50);   /* 9 11 9 11 9 */
    mlxr_net_cand_stats(&c);
    CHECK(c.median_ms == 9.0);
    CHECK(c.jitter_ms > 0.5 && c.jitter_ms < 1.5);
    mlxr_net_cand e = mk(MLXR_PATH_HOME, "b", 4, 0, 5.0, 0, 50);
    mlxr_net_cand_stats(&e);
    CHECK(e.median_ms == 5.0 && e.jitter_ms == 0);
    mlxr_net_cand none = mk(MLXR_PATH_HOME, "c", 0, 3, 0, 0, 0);
    mlxr_net_cand_stats(&none);
    CHECK(none.median_ms == 0);

    /* poor: signal under -70 dBm, or a PHY rate under 4x the session bitrate */
    mlxr_net_cand j = mk(MLXR_PATH_ADAPTER, "j", 7, 0, 2, 0, 0);
    set_link(&j, -80, 576);
    mlxr_net_cand_judge(&j, 80000);
    CHECK(j.poor);                                    /* the Frame's adapter as measured */
    set_link(&j, -70, 576);
    mlxr_net_cand_judge(&j, 80000);
    CHECK(!j.poor);                                   /* 576 >= 4 x 80 Mbit/s, -70 is not under -70 */
    mlxr_net_cand_judge(&j, 150000);
    CHECK(j.poor);                                    /* 576 < 4 x 150 */
    mlxr_net_cand_judge(&j, 0);                       /* 0: the 80 Mbit/s default */
    CHECK(!j.poor);
    set_link(&j, 0, 300);                             /* signal unknown, rate 300 < 320 */
    mlxr_net_cand_judge(&j, 80000);
    CHECK(j.poor);
    set_link(&j, -48, 1201);
    mlxr_net_cand_judge(&j, 80000);
    CHECK(!j.poor);
    memset(&j.link, 0, sizeof j.link);                /* no data: not called poor */
    mlxr_net_cand_judge(&j, 80000);
    CHECK(!j.poor);
    j.link.valid = true;                              /* wired */
    mlxr_net_cand_judge(&j, 80000);
    CHECK(!j.poor);

    mlxr_net_cand home = mk(MLXR_PATH_HOME, "h", 7, 0, 3.5, 0.2, 100);
    mlxr_net_cand ad = mk(MLXR_PATH_ADAPTER, "a", 7, 0, 2.2, 0.4, 38);
    mlxr_net_cand_stats(&home);
    mlxr_net_cand_stats(&ad);
    /* both under 10 ms: the round trips are equal, the better link wins */
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* failures come first, whatever the rest */
    ad.failed = 1;
    CHECK(mlxr_net_better(&ad, &home) == false && mlxr_net_better(&home, &ad));
    home.failed = 2;
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    home.failed = ad.failed = 0;
    /* a poor link loses to a good one even with a clearly lower round trip */
    set_link(&ad, -80, 576);
    set_link(&home, -48, 1201);
    mlxr_net_cand_judge(&ad, 80000);
    mlxr_net_cand_judge(&home, 80000);
    home.median_ms = 40;
    ad.median_ms = 2;
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* both good: a clearly lower round trip wins (beyond max(25%, 5 ms) and over 10 ms) */
    set_link(&ad, -55, 900);
    mlxr_net_cand_judge(&ad, 80000);
    home.median_ms = 40;
    home.jitter_ms = 0;
    ad.median_ms = 20;
    ad.jitter_ms = 0;
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    /* ... but 12 against 15 ms is a tie (within 5 ms): the link decides */
    ad.median_ms = 12;
    home.median_ms = 15;
    ad.link_quality = 70;
    home.link_quality = 98;
    CHECK(mlxr_net_better(&home, &ad));
    /* both poor: the rest decides, not the "poor" flag */
    set_link(&home, -75, 576);
    mlxr_net_cand_judge(&home, 80000);
    mlxr_net_cand_judge(&ad, 80000);
    set_link(&ad, -78, 576);
    mlxr_net_cand_judge(&ad, 80000);
    CHECK(home.poor && ad.poor);
    home.median_ms = ad.median_ms = 5;
    home.link_quality = ad.link_quality = 40;
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));   /* a full tie: the adapter */
    home.median_ms = 4;
    CHECK(mlxr_net_better(&home, &ad));                                    /* the lower median first */
}

static mlxr_net_plan plan2(mlxr_net_pref pref, mlxr_net_cand a, mlxr_net_cand b)
{
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, pref, "test", 47989, "", "");
    p.c[0] = a;
    p.c[1] = b;
    p.n = 2;
    return p;
}

static void test_choose(void)
{
    mlxr_net_cand ad = mk(MLXR_PATH_ADAPTER, "198.51.100.27", 7, 0, 2.0, 0.1, 51);
    mlxr_net_cand home = mk(MLXR_PATH_HOME, "192.0.2.10", 7, 0, 3.2, 0.1, 98);
    mlxr_net_cand dead = mk(MLXR_PATH_HOME, "192.0.2.10", 0, 4, 0, 0, 98);
    strcpy(dead.error, "connect timed out");

    /* auto, the case measured on the Frame: the adapter has the lower idle round trip (2.2 against
     * 3.5 ms) but a poor link (-80 dBm, 576 Mbit/s); Wi-Fi is -48 dBm, 1201 Mbit/s */
    mlxr_net_cand mad = mk(MLXR_PATH_ADAPTER, "198.51.100.27", 7, 0, 2.2, 0.4, 38);
    mlxr_net_cand mhome = mk(MLXR_PATH_HOME, "192.0.2.10", 7, 0, 3.5, 0.2, 100);
    set_link(&mad, -80, 576);
    set_link(&mhome, -48, 1201);
    mlxr_net_plan p = plan2(MLXR_NET_AUTO, mad, mhome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(p.chosen_path == MLXR_PATH_HOME && !p.fell_back && !p.toast[0]);
    CHECK(strstr(p.reason, "chose Wi-Fi: usable, preferred over the adapter (-48 dBm, 1201 Mbit/s"));
    CHECK(strstr(p.summary, "home 192.0.2.10"));
    p = plan2(MLXR_NET_AUTO, mhome, mad);   /* the order does not matter */
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* a faster session bitrate than the PHY rate allows makes Wi-Fi poor too: then the rest decides */
    p = plan2(MLXR_NET_AUTO, mad, mhome);
    p.bitrate_kbps = 400000;                /* 4 x 400 = 1600 Mbit/s: both poor */
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);   /* both poor: equal round trips, the better link, Wi-Fi */
    /* both good: Wi-Fi is preferred, even with the adapter's lower round trip (the owner's 01:39 run:
     * adapter -69 dBm 865 Mbit/s 2.0 ms, Wi-Fi -49 dBm 1081 Mbit/s 3.5 ms) */
    mlxr_net_cand gad = mk(MLXR_PATH_ADAPTER, "198.51.100.27", 7, 0, 2.0, 0.3, 66);
    mlxr_net_cand ghome = mk(MLXR_PATH_HOME, "192.0.2.10", 7, 0, 3.5, 0.2, 100);
    set_link(&gad, -69, 865);
    set_link(&ghome, -49, 1081);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose Wi-Fi: usable, preferred over the adapter"));
    CHECK_EQ_STR(p.connected, "Connected over Wi-Fi.");
    /* Wi-Fi weak (-75 dBm), adapter good: the adapter */
    set_link(&ghome, -75, 1081);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose adapter: Wi-Fi link weak (-75 dBm, 1081 Mbit/s"));
    CHECK_EQ_STR(p.connected, "Connected over Steam Frame Wireless Adapter.");
    /* both weak: still Wi-Fi */
    set_link(&gad, -80, 576);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "both links weak"));
    /* Wi-Fi answers but half the probes failed: weak, the good adapter wins */
    set_link(&gad, -55, 1201);
    set_link(&ghome, -48, 1201);
    ghome = mk(MLXR_PATH_HOME, "192.0.2.10", 3, 4, 3.5, 0.2, 100);
    set_link(&ghome, -48, 1201);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    ghome = mk(MLXR_PATH_HOME, "192.0.2.10", 6, 1, 3.5, 0.2, 100);   /* one failure of seven is fine */
    set_link(&ghome, -48, 1201);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    /* Wi-Fi did not answer at all: the adapter, and the reason says what failed */
    mlxr_net_cand deadhome = mk(MLXR_PATH_HOME, "192.0.2.10", 0, 4, 0, 0, 0);
    strcpy(deadhome.error, "connect timed out");
    p = plan2(MLXR_NET_AUTO, deadhome, gad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose adapter: Wi-Fi did not answer (192.0.2.10: connect timed out)"));
    /* auto: the better */
    p = plan2(MLXR_NET_AUTO, home, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);   /* no link data, round trips equal, 98 against 51: Wi-Fi */
    /* auto: only one answers */
    p = plan2(MLXR_NET_AUTO, dead, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    p = plan2(MLXR_NET_AUTO, ad, dead);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* auto: nobody */
    p = plan2(MLXR_NET_AUTO, dead, dead);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);
    CHECK(p.chosen_path == MLXR_PATH_NONE && !p.summary[0]);
    /* a PC that answers as another PC is no candidate */
    mlxr_net_cand other = home;
    other.other_host = true;
    p = plan2(MLXR_NET_AUTO, other, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    p = plan2(MLXR_NET_AUTO, other, other);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);
    /* not probed is no candidate */
    mlxr_net_cand unprobed = home;
    unprobed.probed = false;
    p = plan2(MLXR_NET_AUTO, unprobed, dead);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);

    /* home: the first answering home candidate, in address order, even if the adapter is faster */
    mlxr_net_cand home2 = mk(MLXR_PATH_HOME, "192.0.2.10", 7, 0, 9.0, 0.1, 98);
    p = plan2(MLXR_NET_HOME, ad, dead);
    p.c[2] = home2;
    p.c[3] = home;
    p.n = 4;
    CHECK_EQ_INT(mlxr_net_choose(&p), 2);
    CHECK(!p.fell_back && !p.toast[0]);
    /* adapter: likewise */
    p = plan2(MLXR_NET_ADAPTER, home, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(!p.fell_back);
    /* adapter asked, only home answers: fall back with the toast */
    p = plan2(MLXR_NET_ADAPTER, home, mk(MLXR_PATH_ADAPTER, "198.51.100.27", 0, 4, 0, 0, 51));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back && p.chosen_path == MLXR_PATH_HOME);
    CHECK_EQ_STR(p.toast, "Steam Frame Wireless Adapter not reachable (198.51.100.27: no answer). Using Wi-Fi.");
    CHECK(strstr(p.summary, "fell back"));
    /* home asked, only the adapter answers */
    p = plan2(MLXR_NET_HOME, dead, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK_EQ_STR(p.toast, "Wi-Fi not reachable (192.0.2.10: connect timed out). Using Steam Frame Wireless Adapter.");
    CHECK_EQ_STR(p.failure, "192.0.2.10: connect timed out");
    /* asked for a path with no address at all */
    p = plan2(MLXR_NET_ADAPTER, home, home2);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back);
    CHECK_EQ_STR(p.failure, "no address of the PC known on this network");
    CHECK(strstr(p.toast, "Steam Frame Wireless Adapter not reachable (no address of the PC known on this network). Using Wi-Fi."));
    /* none anywhere */
    p = plan2(MLXR_NET_HOME, dead, dead);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);
    CHECK(!p.toast[0]);
    /* the chosen address and port */
    p = plan2(MLXR_NET_AUTO, ghome, gad);
    p.c[0].port = 50000;
    mlxr_net_choose(&p);
    uint16_t port = 0;
    CHECK_EQ_STR(mlxr_net_chosen_address(&p, &port), "192.0.2.10");
    CHECK_EQ_INT(port, 50000);
}

static void test_describe(void)
{
    char b[96];
    mlxr_net_link l;
    memset(&l, 0, sizeof l);
    l.valid = l.wifi = true;
    snprintf(l.ssid, sizeof l.ssid, "Home 5GHz");
    l.band = 5;
    mlxr_net_path_describe(MLXR_PATH_HOME, &l, b, sizeof b);
    CHECK_EQ_STR(b, "Wi-Fi (Home 5GHz, 5 GHz)");
    snprintf(l.ssid, sizeof l.ssid, "frame_EXAMPLE0");
    l.band = 6;
    mlxr_net_path_describe(MLXR_PATH_ADAPTER, &l, b, sizeof b);   /* the adapter's own network name is not shown */
    CHECK_EQ_STR(b, "Steam Frame Wireless Adapter (6 GHz)");
    l.band = 0;
    l.ssid[0] = 0;
    mlxr_net_path_describe(MLXR_PATH_HOME, &l, b, sizeof b);
    CHECK_EQ_STR(b, "Wi-Fi");
    mlxr_net_path_describe(MLXR_PATH_ADAPTER, NULL, b, sizeof b);
    CHECK_EQ_STR(b, "Steam Frame Wireless Adapter");
    l.wifi = false;
    mlxr_net_path_describe(MLXR_PATH_HOME, &l, b, sizeof b);
    CHECK_EQ_STR(b, "Wired network");
    l.wifi = true;
    l.band = 24;
    mlxr_net_path_describe(MLXR_PATH_HOME, &l, b, sizeof b);
    CHECK_EQ_STR(b, "Wi-Fi (2.4 GHz)");

    /* link quality: wired best, weak signal and slow rate lower, unknown neutral */
    mlxr_net_link w = { .valid = true, .wifi = false };
    CHECK(mlxr_net_link_quality(&w) == 100.0);
    mlxr_net_link good = { .valid = true, .wifi = true, .signal_dbm = -48, .tx_mbps = 1081 };
    mlxr_net_link weak = { .valid = true, .wifi = true, .signal_dbm = -78, .tx_mbps = 576 };
    mlxr_net_link none = { 0 };
    CHECK(mlxr_net_link_quality(&good) > mlxr_net_link_quality(&weak));
    CHECK(mlxr_net_link_quality(&good) <= 100.0 && mlxr_net_link_quality(&weak) > 0);
    CHECK(mlxr_net_link_quality(&none) == 30.0);
}

/* The real probe against the fake host: one live candidate, one closed port, one that answers as
 * another PC. The fake host speaks on 127.0.0.1 only, so the "paths" are set by hand. */
static void test_probe(void)
{
    fake_opts o = { 0 };
    fake_host *h = fake_host_start(&o);
    CHECK(h != NULL);
    if (!h) return;
    uint16_t live = fake_host_http_port(h);
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", live, "FAKE-HOST-UUID-0001", "0123456789ABCDEF");
    mlxr_net_plan_add(&p, "127.0.0.1", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", 1);        /* nothing listens on port 1: refused at once */
    mlxr_net_plan_add(&p, "no-such-host.invalid", 0);   /* not an IP address: never resolved */
    p.c[0].path = MLXR_PATH_ADAPTER;
    p.c[1].path = MLXR_PATH_HOME;
    p.c[2].path = MLXR_PATH_HOME;
    mlxr_net_probe(&p, NULL, 5, false);
    CHECK(p.c[0].probed && p.c[0].ok == 5 && p.c[0].failed == 0 && p.c[0].n_rtt == 5);
    CHECK(p.c[0].info_ms > 0);
    CHECK(p.c[1].probed && p.c[1].ok == 0 && p.c[1].failed == 5 && p.c[1].error[0]);
    CHECK(p.c[2].probed && p.c[2].ok == 0 && p.c[2].failed >= 1);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.c[0].median_ms > 0 && p.c[0].median_ms < 100);
    CHECK(fake_host_count(h, "serverinfo") >= 1);

    /* home asked: nothing answers on it, the adapter does: toast */
    mlxr_net_plan_init(&p, MLXR_NET_HOME, "test", live, "", "");
    mlxr_net_plan_add(&p, "127.0.0.1", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", 1);
    p.c[0].path = MLXR_PATH_ADAPTER;
    p.c[1].path = MLXR_PATH_HOME;
    bool which[MLXR_NET_MAX_CANDS] = { false, true };
    mlxr_net_probe(&p, which, 2, true);
    CHECK(p.c[1].probed && !p.c[0].probed);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);
    which[0] = true;
    which[1] = false;
    mlxr_net_probe(&p, which, 2, true);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back && strstr(p.toast, "Wi-Fi not reachable"));

    /* a different uuid: the address answers as another PC and is out */
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", live, "SOME-OTHER-PC", "");
    mlxr_net_plan_add(&p, "127.0.0.1", 0);
    mlxr_net_probe(&p, NULL, 3, false);
    CHECK(p.c[0].other_host && p.c[0].ok == 0);
    CHECK_EQ_INT(mlxr_net_choose(&p), -1);

    /* the whole select with a fake table: loopback is "home"; the closed port fails fast, the
     * time stays far inside the budget */
    mlxr_netifs t;
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", live, "FAKE-HOST-UUID-0001", "");
    mlxr_net_plan_add(&p, "127.0.0.1", 1);
    mlxr_net_plan_add(&p, "127.0.0.1", 0);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    CHECK(p.fell_back && strstr(p.toast, "Steam Frame Wireless Adapter not reachable"));

    /* choosing a path means that path: a refused address on it is retried for retry_ms before the
     * other path is tried, and the toast names the address and how it failed */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_if(&t, "wlanap", "198.51.100.1", 24, true, 2, false);   /* documentation addresses only */
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", 1, "", "");   /* port 1: refused */
    p.retry_ms = 700;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);                    /* the adapter: nothing answers */
    mlxr_net_plan_add(&p, "127.0.0.1", live);                    /* "Wi-Fi": the fake host */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    CHECK(ms >= 650 && ms < 8000);   /* the retry window passed before the fall back */
    CHECK(p.fell_back && strstr(p.failure, "198.51.100.7:"));
    CHECK(strstr(p.toast, "Steam Frame Wireless Adapter not reachable (198.51.100.7: "));
    fake_host_stop(h);
}

/* mDNS: an answer naming the PC (with a compressed name) and one naming something else */
static size_t put_rr(uint8_t *b, size_t o, const uint8_t *name, size_t nl, const uint8_t *ip)
{
    memcpy(b + o, name, nl);
    o += nl;
    const uint8_t hdr[10] = { 0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4 };
    memcpy(b + o, hdr, 10);
    memcpy(b + o + 10, ip, 4);
    return o + 14;
}

static void test_mdns_and_sources(void)
{
    uint8_t m[512] = { 0 };
    m[2] = 0x84;
    m[7] = 3;   /* three answers */
    size_t o = 12;
    /* "DESKTOP-EXAMPLE.local" spelled out once, then pointed to */
    const uint8_t n1[] = { 12, 'S', 'a', 'n', 'a', '-', 'd', 'e', 's', 'k', 't', 'o', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0 };
    const uint8_t ptr[] = { 0xC0, 12 };
    const uint8_t other[] = { 5, 'p', 'r', 'i', 'n', 't', 5, 'l', 'o', 'c', 'a', 'l', 0 };
    const uint8_t ip1[] = { 192, 168, 0, 12 }, ip2[] = { 10, 35, 78, 27 }, ip3[] = { 192, 168, 0, 50 };
    o = put_rr(m, o, n1, sizeof n1, ip1);
    o = put_rr(m, o, ptr, sizeof ptr, ip2);
    o = put_rr(m, o, other, sizeof other, ip3);
    char out[8][128];
    const char *src[8];
    int n = mlxr_net_mdns_parse(m, o, "DESKTOP-EXAMPLE.local", out, src, 0, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(out[0], "192.0.2.10");
    CHECK_EQ_STR(out[1], "198.51.100.27");
    CHECK_EQ_STR(src[0], "mdns");
    CHECK_EQ_INT(mlxr_net_mdns_parse(m, o, "", out, src, 0, 8), 3);          /* any name */
    CHECK_EQ_INT(mlxr_net_mdns_parse(m, o, "nope.local", out, src, 0, 8), 0);
    CHECK_EQ_INT(mlxr_net_mdns_parse(m, 5, "", out, src, 0, 8), 0);           /* truncated */
    m[12] = 0xC0; m[13] = 12;                                                 /* a name pointing at itself */
    CHECK_EQ_INT(mlxr_net_mdns_parse(m, o, "", out, src, 0, 8), 0);

    /* a MAC nobody has: the ARP table gives nothing; no name and no budget: no mDNS */
    mlxr_netifs t;
    frame_table(&t, true);
    const uint8_t nomac[6] = { 2, 0, 0, 0xde, 0xad, 1 };
    CHECK_EQ_INT(mlxr_net_discover(&t, "", nomac, 0, out, src, 8), 0);

    /* sources and on-link flags are kept, and logged in one line */
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 47989, "", "");
    mlxr_net_plan_add_src(&p, "198.51.100.27", 0, "launcher");
    mlxr_net_plan_add_record(&p, "198.51.100.27", "192.0.2.10,203.0.113.9");
    mlxr_net_plan_add_src(&p, "192.0.2.10", 0, "mdns");   /* a duplicate: the first source stays */
    CHECK_EQ_INT(p.n, 3);
    CHECK_EQ_STR(p.c[0].source, "launcher");
    CHECK_EQ_STR(p.c[1].source, "hosts.ini");
    mlxr_net_plan_classify(&p, &t);
    CHECK(p.c[0].onlink && p.c[1].onlink && !p.c[2].onlink);
    CHECK(mlxr_net_onlink_if(&t, "192.0.2.10") && !strcmp(mlxr_net_onlink_if(&t, "192.0.2.10")->name, "wlan0"));
    CHECK(mlxr_net_onlink_if(&t, "8.8.8.8") == NULL && mlxr_net_onlink_if(&t, "host.local") == NULL);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);   /* the in-process fake host writes to probes that hung up */
    test_pref();
    test_classify();
    test_candidates();
    test_stats_and_scoring();
    test_choose();
    test_describe();
    test_mdns_and_sources();
    test_probe();
    TEST_DONE();
}
