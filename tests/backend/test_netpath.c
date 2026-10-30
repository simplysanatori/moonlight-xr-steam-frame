/* Network path choice: preference precedence, classifying addresses with fake interface tables,
 * scoring, the fallbacks and their toasts, candidate gathering, and the real probe against the
 * fake host. GPLv3. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "backend.h"
#include "check.h"
#include "fake_host.h"
#include "netpath.h"
#include "nethistory.h"

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
    add_if(t, "wlan0", "192.168.1.20", 24, true, nl80211 ? 1 : 0, true);
    add_if(t, "wlanap", "10.35.50.1", 24, true, nl80211 ? 2 : 0, false);
}

static void test_pref(void)
{
    mlxr_net_pref p;
    CHECK(mlxr_net_pref_parse("auto", &p) && p == MLXR_NET_AUTO);
    CHECK(mlxr_net_pref_parse(" Home\n", &p) && p == MLXR_NET_HOME);
    CHECK(mlxr_net_pref_parse("ADAPTER", &p) && p == MLXR_NET_ADAPTER);
    CHECK(mlxr_net_pref_parse(" usb\n", &p) && p == MLXR_NET_USB);   /* Native (lane wave4c-usb): the config.ini value is "usb" */
    CHECK(mlxr_net_pref_parse("USB", &p) && p == MLXR_NET_USB);
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
    CHECK(mlxr_net_pref_resolve(NULL, NULL, "usb", &src, &warn) == MLXR_NET_USB);
    CHECK_EQ_STR(src, "config");
    CHECK(mlxr_net_pref_resolve("usb", NULL, "home", &src, &warn) == MLXR_NET_USB);
    CHECK_EQ_STR(src, "flag");
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
        CHECK(mlxr_net_classify(&t, "10.35.50.27", &via) == MLXR_PATH_ADAPTER && via && !strcmp(via->name, "wlanap"));
        CHECK(mlxr_net_classify(&t, "192.168.1.10", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "wlan0"));
        /* off-link: the default route's interface */
        CHECK(mlxr_net_classify(&t, "203.0.113.50", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "wlan0"));
        CHECK(mlxr_net_classify(&t, "100.64.0.5", NULL) == MLXR_PATH_HOME);
        /* names and IPv6 take the default route too */
        CHECK(mlxr_net_classify(&t, "gaming-pc.local", &via) == MLXR_PATH_HOME && via);
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
    CHECK(mlxr_net_classify(&t, "10.35.50.27", NULL) == MLXR_PATH_ADAPTER);
    CHECK(mlxr_net_classify(&t, "10.35.1.1", NULL) == MLXR_PATH_HOME);
    /* an interface that is down does not count */
    frame_table(&t, true);
    t.v[2].up = false;
    CHECK(mlxr_net_classify(&t, "10.35.50.27", NULL) == MLXR_PATH_HOME);
    /* ethernet only: everything is home; loopback too */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_if(&t, "eth0", "192.168.1.5", 24, false, 0, true);
    CHECK(mlxr_net_classify(&t, "192.168.1.9", NULL) == MLXR_PATH_HOME);
    CHECK(mlxr_net_classify(&t, "10.35.50.27", NULL) == MLXR_PATH_HOME);
    CHECK(mlxr_net_classify(&t, "127.0.0.1", NULL) == MLXR_PATH_HOME);
    /* a managed Wi-Fi that is the only radio is not an AP even without the default route */
    memset(&t, 0, sizeof t);
    add_if(&t, "wlan0", "192.168.1.20", 24, true, 0, false);
    CHECK(mlxr_net_classify(&t, "192.168.1.10", NULL) == MLXR_PATH_HOME);
    /* no interfaces: home */
    memset(&t, 0, sizeof t);
    CHECK(mlxr_net_classify(&t, "10.0.0.1", &via) == MLXR_PATH_HOME && !via);

    /* classify a plan, the ifname follows */
    frame_table(&t, true);
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "default", 47989, "", "");
    mlxr_net_plan_add(&p, "10.35.50.27", 0);
    mlxr_net_plan_add(&p, "192.168.1.10", 0);
    mlxr_net_plan_classify(&p, &t);
    CHECK(p.c[0].path == MLXR_PATH_ADAPTER && !strcmp(p.c[0].ifname, "wlanap"));
    CHECK(p.c[1].path == MLXR_PATH_HOME && !strcmp(p.c[1].ifname, "wlan0"));
}

static void test_candidates(void)
{
    mlxr_net_plan p;
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "default", 0, "u", "");
    CHECK(p.default_port == 47989 && p.budget_ms == 1500);
    CHECK(mlxr_net_plan_add(&p, "10.35.50.27", 0) == 0);
    CHECK(mlxr_net_plan_add(&p, " 192.168.1.10 ", 0) == 1);
    CHECK(mlxr_net_plan_add(&p, "10.35.50.27", 0) == 0);          /* deduped */
    CHECK(mlxr_net_plan_add(&p, "10.35.50.27", 47989) == 0);      /* the default port is the same candidate */
    CHECK(mlxr_net_plan_add(&p, "10.35.50.27", 50000) == 2);      /* another port is not */
    CHECK(mlxr_net_plan_add(&p, "", 0) == -1 && mlxr_net_plan_add(&p, NULL, 0) == -1);
    mlxr_net_plan_add_record(&p, "192.168.1.10", "100.64.0.5, 10.35.50.27,203.0.113.50");
    CHECK_EQ_INT(p.n, 5);
    CHECK_EQ_STR(p.c[3].address, "100.64.0.5");
    CHECK_EQ_STR(p.c[4].address, "203.0.113.50");
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

/* Native (lane netpath-quality): every sample the same, so the median and p90 are `rtt`, the jitter 0 and
 * the score is rtt + 50 per failure */
static mlxr_net_cand mkflat(mlxr_net_path path, const char *addr, int ok, int failed, double rtt, double quality)
{
    mlxr_net_cand c = mk(path, addr, ok, failed, rtt, 0, quality);
    return c;
}

/* explicit samples (ms) */
static mlxr_net_cand mksamples(mlxr_net_path path, const char *addr, const double *v, int n, int failed)
{
    mlxr_net_cand c = mk(path, addr, 0, failed, 0, 0, 0);
    c.ok = n;
    c.sent = n + failed;
    for (int i = 0; i < n; i++) c.rtt_ms[c.n_rtt++] = v[i];
    return c;
}

/* a flat round trip (median and p90 equal) */
#define SETRTT(c, v) ((c).median_ms = (c).p90_ms = (v))

static void test_stats_and_scoring(void)
{
    mlxr_net_cand c = mk(MLXR_PATH_HOME, "a", 5, 0, 10.0, 1.0, 50);   /* 9 11 9 11 9 */
    mlxr_net_cand_stats(&c);
    CHECK(c.median_ms == 9.0);
    CHECK(c.p90_ms == 11.0);                          /* nearest rank: the 5th of 5 */
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

    /* Native (lane netpath-quality): the score is median + jitter + (p90 - median) + 50 per failure + history */
    {
        /* ten samples: 4 4 4 4 4 4 4 4 4 14: median 4, p90 (9th of 10) 4, jitter (10 / 10) 1, score 5 */
        double v[10] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 14 };
        mlxr_net_cand s1 = mksamples(MLXR_PATH_HOME, "s", v, 10, 0);
        mlxr_net_cand_stats(&s1);
        CHECK(s1.median_ms == 4 && s1.p90_ms == 4 && s1.jitter_ms == 1.0);
        CHECK(mlxr_net_score(&s1) == 5.0);
        /* a tail: 15 samples, 13 at 4 ms and two at 24 ms: p90 (14th) is 24, score 4 + 2.67 + 20 */
        double w[15] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 24, 24 };
        mlxr_net_cand s2 = mksamples(MLXR_PATH_HOME, "s", w, 15, 0);
        mlxr_net_cand_stats(&s2);
        CHECK(s2.median_ms == 4 && s2.p90_ms == 24);
        CHECK(fabs(mlxr_net_score(&s2) - (4 + 40.0 / 15 + 20)) < 1e-9);
        /* failures: 50 ms each */
        mlxr_net_cand s3 = mkflat(MLXR_PATH_HOME, "s", 13, 2, 4.0, 0);
        mlxr_net_cand_stats(&s3);
        CHECK(mlxr_net_score(&s3) == 104.0);
        /* history penalty is part of the score */
        s3.hist_penalty_ms = 12;
        CHECK(mlxr_net_score(&s3) == 116.0);
    }
    /* the history penalty: nothing up to 0.5 percent, 20 ms per percent after, 150 at most */
    CHECK(mlxr_net_hist_penalty(0) == 0 && mlxr_net_hist_penalty(0.5) == 0);
    CHECK(fabs(mlxr_net_hist_penalty(0.6) - 12.0) < 1e-9);
    CHECK(fabs(mlxr_net_hist_penalty(2.3) - 46.0) < 1e-9);
    CHECK(mlxr_net_hist_penalty(7.5) == 150.0);
    CHECK(mlxr_net_hist_penalty(40) == 150.0 && mlxr_net_hist_penalty(100) == 150.0);

    mlxr_net_cand home = mkflat(MLXR_PATH_HOME, "h", 15, 0, 3.5, 100);
    mlxr_net_cand ad = mkflat(MLXR_PATH_ADAPTER, "a", 15, 0, 2.2, 38);
    mlxr_net_cand_stats(&home);
    mlxr_net_cand_stats(&ad);
    /* within 3 ms: a tie, the better link wins (more than 5 points apart) */
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* a tie and links within 5 points: Wi-Fi */
    ad.link_quality = 97;
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* ... also when the adapter's link is the better one by 5 points or less */
    ad.link_quality = 100;
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* the adapter's link clearly better: it wins the tie */
    ad.link_quality = 70;
    home.link_quality = 40;
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    /* the margin: exactly 3 ms apart is a tie, 3.5 ms is not */
    home.link_quality = 100;
    ad.link_quality = 38;
    SETRTT(home, 8.0);
    SETRTT(ad, 5.0);
    CHECK(mlxr_net_better(&home, &ad));
    SETRTT(ad, 4.5);
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    /* latency wins over the link rate: a poor link (-80 dBm, 576 Mbit/s) with the lower round trip beats a good one */
    set_link(&ad, -80, 576);
    set_link(&home, -48, 1201);
    mlxr_net_cand_judge(&ad, 80000);
    mlxr_net_cand_judge(&home, 80000);
    SETRTT(home, 12);
    SETRTT(ad, 2);
    CHECK(ad.poor && !home.poor);
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    /* ... and the other way: a clearly lower round trip wins whatever the link */
    SETRTT(home, 2);
    SETRTT(ad, 12);
    CHECK(mlxr_net_better(&home, &ad) && !mlxr_net_better(&ad, &home));
    /* a failure costs 50 ms, far more than a few ms of round trip */
    SETRTT(home, 3);
    SETRTT(ad, 9);
    home.failed = 1;
    CHECK(mlxr_net_better(&ad, &home) && !mlxr_net_better(&home, &ad));
    home.failed = 0;
    CHECK(mlxr_net_better(&home, &ad));
    /* jitter and the tail count too */
    SETRTT(home, 5);
    home.jitter_ms = 4;
    home.p90_ms = 12;                   /* score 5 + 4 + 7 = 16 */
    SETRTT(ad, 9);
    ad.jitter_ms = 0.5;
    ad.p90_ms = 9.5;                    /* score 10 */
    CHECK(mlxr_net_better(&ad, &home));
    /* two addresses on one path: the lower score, no margin */
    mlxr_net_cand h1 = mkflat(MLXR_PATH_HOME, "h1", 15, 0, 5.0, 100), h2 = mkflat(MLXR_PATH_HOME, "h2", 15, 0, 5.5, 100);
    mlxr_net_cand_stats(&h1);
    mlxr_net_cand_stats(&h2);
    CHECK(mlxr_net_better(&h1, &h2) && !mlxr_net_better(&h2, &h1));

    /* the history, applied to a plan: a path without history has none, one that is old or short is ignored */
    mlxr_net_plan hp;
    mlxr_net_plan_init(&hp, MLXR_NET_AUTO, "test", 47989, "", "");
    hp.now_s = 1800000000;
    hp.c[0] = mkflat(MLXR_PATH_HOME, "h", 15, 0, 3, 0);
    hp.c[1] = mkflat(MLXR_PATH_ADAPTER, "a", 15, 0, 3, 0);
    hp.n = 2;
    mlxr_net_plan_apply_history(&hp);
    CHECK(hp.c[0].hist_loss_pct < 0 && hp.c[1].hist_loss_pct < 0 && hp.c[0].hist_penalty_ms == 0 && hp.c[1].hist_penalty_ms == 0);
    hp.hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 9770, 230, 1.0, 4, hp.now_s - 3600 };   /* 2.3 percent */
    mlxr_net_plan_apply_history(&hp);
    CHECK(fabs(hp.c[0].hist_loss_pct - 2.3) < 1e-9 && fabs(hp.c[0].hist_penalty_ms - 46.0) < 1e-9);
    CHECK(hp.c[1].hist_loss_pct < 0 && hp.c[1].hist_penalty_ms == 0);   /* the adapter has none: not charged */
    hp.hist.e[MLXR_NETHIST_ADAPTER] = (mlxr_nethist_entry){ true, 20000, 0, 1.0, 4, hp.now_s - 3600 };
    mlxr_net_plan_apply_history(&hp);
    CHECK(hp.c[1].hist_loss_pct == 0 && hp.c[1].hist_penalty_ms == 0);   /* history with no loss: no penalty */
    hp.hist.e[MLXR_NETHIST_HOME].time_s = hp.now_s - MLXR_NETHIST_MAX_AGE_S - 10;
    mlxr_net_plan_apply_history(&hp);
    CHECK(hp.c[0].hist_loss_pct < 0 && hp.c[0].hist_penalty_ms == 0);   /* too old */
    hp.hist.e[MLXR_NETHIST_HOME].time_s = hp.now_s - 10;
    hp.hist.e[MLXR_NETHIST_HOME].frames = 100;
    hp.hist.e[MLXR_NETHIST_HOME].lost = 50;
    mlxr_net_plan_apply_history(&hp);
    CHECK(hp.c[0].hist_loss_pct < 0);                                    /* too few frames to say */
    hp.hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 1000, 1000, 1.0, 4, hp.now_s - 10 };   /* 50 percent */
    mlxr_net_plan_apply_history(&hp);
    CHECK(hp.c[0].hist_penalty_ms == 150.0);                             /* capped */
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
    mlxr_net_cand ad = mk(MLXR_PATH_ADAPTER, "10.35.50.27", 7, 0, 2.0, 0.1, 51);
    mlxr_net_cand home = mk(MLXR_PATH_HOME, "192.168.1.10", 7, 0, 3.2, 0.1, 98);
    mlxr_net_cand dead = mk(MLXR_PATH_HOME, "192.168.1.99", 0, 4, 0, 0, 98);
    strcpy(dead.error, "connect timed out");

    /* auto, the case measured on the Frame: the adapter has the lower idle round trip (2.2 against
     * 3.5 ms, a difference inside the 3 ms margin) but a poor link (-80 dBm, 576 Mbit/s); Wi-Fi is
     * -48 dBm, 1201 Mbit/s. Native (lane netpath-quality): a tie, the better link decides. */
    mlxr_net_cand mad = mk(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 2.2, 0.4, 38);
    mlxr_net_cand mhome = mk(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 3.5, 0.2, 100);
    set_link(&mad, -80, 576);
    set_link(&mhome, -48, 1201);
    mlxr_net_plan p = plan2(MLXR_NET_AUTO, mad, mhome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(p.chosen_path == MLXR_PATH_HOME && !p.fell_back && !p.toast[0]);
    CHECK(strstr(p.reason, "chose Wi-Fi: scores within 3 ms"));
    CHECK(strstr(p.reason, "the better link breaks the tie (quality 100 against 38)"));
    CHECK(strstr(p.summary, "home 192.168.1.10"));
    p = plan2(MLXR_NET_AUTO, mhome, mad);   /* the order does not matter */
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* a tie with no link data on either side: Wi-Fi is preferred */
    p = plan2(MLXR_NET_AUTO, mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 2.0, 0), mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 4.0, 0));
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "Wi-Fi preferred on a tie"));
    CHECK_EQ_STR(p.connected, "Connected over Wi-Fi.");
    /* latency wins over the link rate: the adapter's round trip is 10 ms lower though its link is poor */
    mlxr_net_cand gad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 2.0, 38);
    mlxr_net_cand ghome = mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 12.0, 100);
    set_link(&gad, -80, 576);
    set_link(&ghome, -48, 1201);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose adapter: score 2.0 ms against Wi-Fi 12.0 ms (lower is better); decided by latency (rtt 2.0 ms against 12.0 ms"));
    CHECK_EQ_STR(p.connected, "Connected over Steam Frame Wireless Adapter.");
    /* ... and Wi-Fi with the lower round trip beats an adapter with a good link */
    ghome = mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 2.0, 100);
    gad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 9.0, 100);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose Wi-Fi: score 2.0 ms against adapter 9.0 ms"));
    /* a failed probe costs 50 ms: the path with the lower round trip but a failure loses */
    ghome = mkflat(MLXR_PATH_HOME, "192.168.1.10", 14, 1, 3.0, 100);
    gad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 8.0, 38);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose adapter: score 8.0 ms against Wi-Fi 53.0 ms"));
    CHECK(strstr(p.reason, "decided by failed probes (Wi-Fi 1 of 15, adapter 0 of 15)"));
    /* half the probes failing is no longer a rule of its own, but it costs 400 ms */
    ghome = mkflat(MLXR_PATH_HOME, "192.168.1.10", 7, 8, 2.0, 100);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* the history: Wi-Fi is quicker (3 against 6 ms) but its last stream lost 2.3 percent of the frames */
    ghome = mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 3.0, 100);
    gad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 6.0, 38);
    p = plan2(MLXR_NET_AUTO, gad, ghome);
    p.now_s = 1800000000;
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);                                                    /* no history: Wi-Fi */
    p.hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 9770, 230, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose adapter: score 6.0 ms against Wi-Fi 49.0 ms"));
    CHECK(strstr(p.reason, "decided by the last stream's losses (Wi-Fi: 2.3% lost last time; adapter: no history)"));
    /* history for both paths, equal losses: the latency decides again */
    p.hist.e[MLXR_NETHIST_ADAPTER] = (mlxr_nethist_entry){ true, 9770, 230, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    /* a path with no history is not penalised: the adapter with a clean history next to a lossy Wi-Fi */
    p.hist.e[MLXR_NETHIST_ADAPTER] = (mlxr_nethist_entry){ true, 20000, 0, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* a loss of 0.4 percent is not charged */
    p.hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 9960, 40, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    /* the hard rules stay: a path that did not answer is never chosen, whatever its history or links */
    mlxr_net_cand deadad = mk(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 15, 0, 0, 100);
    strcpy(deadad.error, "connect timed out");
    p = plan2(MLXR_NET_AUTO, deadad, mkflat(MLXR_PATH_HOME, "192.168.1.10", 3, 12, 30.0, 0));
    p.now_s = 1800000000;
    p.hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 1000, 1000, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    /* the adapter skipped (no station) is never chosen, even next to a bad Wi-Fi */
    mlxr_net_cand skipped = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 0, 0, 100);
    skipped.probed = false;
    skipped.skipped = true;
    p = plan2(MLXR_NET_AUTO, skipped, mkflat(MLXR_PATH_HOME, "192.168.1.10", 5, 10, 90.0, 0));
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    /* Wi-Fi did not answer at all: the adapter, and the reason says what failed */
    mlxr_net_cand deadhome = mk(MLXR_PATH_HOME, "192.168.1.10", 0, 4, 0, 0, 0);
    strcpy(deadhome.error, "connect timed out");
    gad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 6.0, 38);
    p = plan2(MLXR_NET_AUTO, deadhome, gad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose adapter: Wi-Fi did not answer (192.168.1.10: connect timed out)"));
    /* auto: the better (no link data, scores 3.2 and 2.0: a tie, 98 against 51: Wi-Fi) */
    p = plan2(MLXR_NET_AUTO, home, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
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
    mlxr_net_cand home2 = mk(MLXR_PATH_HOME, "192.168.1.11", 7, 0, 9.0, 0.1, 98);
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
    p = plan2(MLXR_NET_ADAPTER, home, mk(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 4, 0, 0, 51));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back && p.chosen_path == MLXR_PATH_HOME);
    CHECK_EQ_STR(p.toast, "Steam Frame Wireless Adapter not reachable (10.35.50.27: no answer). Using Wi-Fi.");
    CHECK(strstr(p.summary, "fell back"));
    /* home asked, only the adapter answers */
    p = plan2(MLXR_NET_HOME, dead, ad);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK_EQ_STR(p.toast, "Wi-Fi not reachable (192.168.1.99: connect timed out). Using Steam Frame Wireless Adapter.");
    CHECK_EQ_STR(p.failure, "192.168.1.99: connect timed out");
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
    CHECK_EQ_STR(mlxr_net_chosen_address(&p, &port), "192.168.1.10");
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
    snprintf(l.ssid, sizeof l.ssid, "frame_1A2B3C4D");
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
static void station_inputs(mlxr_adapter_inputs *in);

/* ---------------------------------------------------------------- the adapter (Native, lane adapter-detect) */

/* Made-up facts. Addresses are from the documentation ranges, MACs from the locally administered range. */
#define ARP_HEAD "IP address       HW type     Flags       HW address            Mask     Device\n"
static const char *IW_ONE =
    "Station 02:00:00:aa:bb:01 (on wlanap)\n"
    "\tinactive time:\t120 ms\n"
    "\tsignal:  \t-48 [-50, -49] dBm\n"
    "\tsignal avg:\t-47 dBm\n"
    "\ttx bitrate:\t1201.0 MBit/s 160MHz HE-MCS 9\n";
static const char *IW_TWO =
    "Station 02:00:00:aa:bb:01 (on wlanap)\n"
    "\tsignal:  \t-61 [-62, -60] dBm\n"
    "Station 02:00:00:aa:bb:02 (on wlanap)\n"
    "\tsignal:  \t-52 [-53, -51] dBm\n";

static void make_inputs(mlxr_adapter_inputs *in, bool exists, const char *oper, const char *carrier, const char *iw, const char *arp_rows)
{
    memset(in, 0, sizeof *in);
    in->nl_stations = -1;
    in->exists = exists;
    snprintf(in->operstate, sizeof in->operstate, "%s", oper);
    snprintf(in->carrier, sizeof in->carrier, "%s", carrier);
    if (iw) { in->has_iw = true; snprintf(in->iw_dump, sizeof in->iw_dump, "%s", iw); }
    if (arp_rows) { in->has_arp = true; snprintf(in->arp, sizeof in->arp, "%s%s", ARP_HEAD, arp_rows); }
}

#define ARP_PC "198.51.100.7    0x1         0x2         02:00:00:aa:bb:01     *        wlanap\n"
#define ARP_OTHER_IF "192.0.2.10      0x1         0x2         02:00:00:cc:dd:01     *        wlan0\n"

static void station_inputs(mlxr_adapter_inputs *in)
{
    make_inputs(in, true, "up", "1", IW_ONE, ARP_OTHER_IF ARP_PC);
}

static char g_log[16384];
static void capture_log(int level, const char *msg, void *ud)
{
    (void)ud;
    if (level > 1) return;
    size_t n = strlen(g_log);
    snprintf(g_log + n, sizeof g_log - n, "%s\n", msg);
}

static void test_adapter_assess(void)
{
    mlxr_adapter_inputs in;
    mlxr_adapter_info a;
    const char *pc[] = { "198.51.100.7" };
    char text[200];

    /* wlanap missing */
    make_inputs(&in, false, "", "", NULL, NULL);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.checked && a.state == MLXR_ADAPTER_NONE && !a.exists && !a.up);
    CHECK_EQ_STR(a.reason, "wlanap missing");
    mlxr_net_adapter_text(&a, text, sizeof text);
    CHECK_EQ_STR(text, "no adapter found (wlanap missing)");
    CHECK(!mlxr_net_adapter_usable(&a));

    /* wlanap down: by operstate, and by carrier */
    make_inputs(&in, true, "down", "0", "", ARP_PC);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_NONE && a.exists && !a.up);
    CHECK_EQ_STR(a.reason, "wlanap down");
    make_inputs(&in, true, "up", "0", IW_ONE, ARP_PC);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_NONE);
    make_inputs(&in, true, "unknown", "", IW_ONE, ARP_PC);   /* "unknown" with no carrier file: up */
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && a.up);
    make_inputs(&in, true, "lowerlayerdown", "0", IW_ONE, ARP_PC);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_NONE);

    /* up, no station (a stale neighbour entry does not make one) */
    make_inputs(&in, true, "up", "1", "", ARP_PC);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_LINK_ONLY && a.up && a.stations_known && a.stations == 0);
    CHECK_EQ_STR(a.reason, "wlanap up, no station");
    mlxr_net_adapter_text(&a, text, sizeof text);
    CHECK_EQ_STR(text, "no adapter found (wlanap up, no station)");
    CHECK(!mlxr_net_adapter_usable(&a));

    /* a station, and the PC's address in the table */
    station_inputs(&in);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && a.stations == 1 && a.signal_dbm == -48);
    CHECK(a.pc_known && a.pc_in_table);
    CHECK_EQ_STR(a.stations_from, "iw");
    CHECK_EQ_STR(a.reason, "wlanap up, 1 station (-48 dBm), PC address in the neighbour table");
    mlxr_net_adapter_text(&a, text, sizeof text);
    CHECK(!strncmp(text, "present (wlanap up, 1 station", 29));
    CHECK(mlxr_net_adapter_usable(&a));

    /* a station, the PC's address is not in the table (the entry is on another interface, or missing) */
    make_inputs(&in, true, "up", "1", IW_ONE, ARP_OTHER_IF);
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && a.pc_known && !a.pc_in_table);
    CHECK(strstr(a.reason, "PC address not in the neighbour table"));
    CHECK(mlxr_net_adapter_usable(&a));          /* still usable: the probe decides */
    make_inputs(&in, true, "up", "1", IW_ONE, ARP_HEAD "198.51.100.7    0x1         0x0         00:00:00:00:00:00     *        wlanap\n");
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);   /* an incomplete entry is not in the table */
    CHECK(a.state == MLXR_ADAPTER_STATION && !a.pc_in_table);
    /* no PC address given */
    station_inputs(&in);
    mlxr_net_adapter_assess(&in, "wlanap", NULL, 0, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && !a.pc_known && !strstr(a.reason, "PC address"));

    /* two stations, the stronger signal is reported */
    make_inputs(&in, true, "up", "1", IW_TWO, NULL);
    mlxr_net_adapter_assess(&in, "wlanap", NULL, 0, &a);
    CHECK(a.stations == 2 && a.signal_dbm == -52);
    CHECK_EQ_STR(a.reason, "wlanap up, 2 stations (-52 dBm)");

    /* iw not available: nl80211's count, then the neighbour table, then "cannot tell" */
    make_inputs(&in, true, "up", "1", NULL, ARP_PC);
    in.nl_stations = 0;
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_LINK_ONLY && !strcmp(a.stations_from, "nl80211"));
    in.nl_stations = 1;
    in.nl_signal = -55;
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && a.signal_dbm == -55);
    in.nl_stations = -1;
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && !strcmp(a.stations_from, "neighbours") && a.stations == 1);
    make_inputs(&in, true, "up", "1", NULL, "");   /* nothing says anything: the old behaviour */
    mlxr_net_adapter_assess(&in, "wlanap", pc, 1, &a);
    CHECK(a.state == MLXR_ADAPTER_STATION && !a.stations_known);
    CHECK(strstr(a.reason, "assumed present") && mlxr_net_adapter_usable(&a));

    /* not checked: usable, as before */
    memset(&a, 0, sizeof a);
    CHECK(mlxr_net_adapter_usable(&a));

    /* another interface name */
    make_inputs(&in, true, "up", "1", "", NULL);
    mlxr_net_adapter_assess(&in, "wlan9", NULL, 0, &a);
    CHECK_EQ_STR(a.reason, "wlan9 up, no station");

    /* the real reader on an interface that is not there; this works on any machine */
    mlxr_net_adapter_detect("mlxr-no-such-if", NULL, 0, &a);
    CHECK(a.checked && a.state == MLXR_ADAPTER_NONE && !a.exists);
    CHECK_EQ_STR(a.reason, "mlxr-no-such-if missing");
}

/* select with no station: auto does not probe or pick the adapter; the forced adapter falls back */
static void test_adapter_plan(fake_host *h)
{
    uint16_t live = fake_host_http_port(h);
    mlxr_netifs t;
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_if(&t, "wlanap", "198.51.100.1", 24, true, 2, false);
    mlxr_adapter_inputs none, st, down;
    make_inputs(&none, true, "up", "1", "", ARP_PC);
    station_inputs(&st);
    make_inputs(&down, true, "down", "0", "", NULL);
    mlxr_net_plan p;

    /* auto, no station: the adapter candidate is skipped, Wi-Fi chosen, the log says so */
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &none;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    g_log[0] = 0;
    mlxr_set_log(capture_log, NULL);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    mlxr_set_log(NULL, NULL);
    CHECK(p.adapter.state == MLXR_ADAPTER_LINK_ONLY);
    CHECK(p.c[0].skipped && !p.c[0].probed && p.c[0].sent == 0);
    CHECK(p.c[1].probed && p.c[1].ok > 0 && !p.fell_back);
    CHECK(strstr(p.reason, "chose Wi-Fi: no Steam Frame Wireless Adapter found (wlanap up, no station)"));
    CHECK(strstr(g_log, "network path: Steam Frame Wireless Adapter: no adapter found (wlanap up, no station)"));
    CHECK(!p.adapter_absent_fallback);

    /* auto, wlanap down */
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &down;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    g_log[0] = 0;
    mlxr_set_log(capture_log, NULL);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    mlxr_set_log(NULL, NULL);
    CHECK(p.c[0].skipped && !p.c[0].probed);
    CHECK(strstr(g_log, "Steam Frame Wireless Adapter: no adapter found (wlanap down)"));

    /* auto, a station: the adapter is probed as before (nothing answers at the made-up address) */
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &st;
    p.budget_ms = 600;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    CHECK(!p.c[0].skipped && p.c[0].probed);   /* tried (cancelled once Wi-Fi answered) */
    CHECK(!strstr(p.reason, "no Steam Frame Wireless Adapter found"));

    /* the adapter forced, no station: no probe, no retry window, Wi-Fi with the toast */
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", 1, "", "");
    p.adapter_inputs = &none;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    g_log[0] = 0;
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    CHECK(ms < 2000);   /* the 4 s retry window was not waited */
    CHECK(p.fell_back && p.adapter_absent_fallback && p.chosen_path == MLXR_PATH_HOME);
    CHECK(!p.c[0].probed);
    CHECK_EQ_STR(p.toast, "Steam Frame Wireless Adapter not reachable (no adapter found: wlanap up, no station). Using Wi-Fi.");
    CHECK_EQ_STR(p.failure, "no adapter found: wlanap up, no station");

    /* the adapter forced and wlanap down */
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", 1, "", "");
    p.adapter_inputs = &down;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    CHECK_EQ_STR(p.toast, "Steam Frame Wireless Adapter not reachable (no adapter found: wlanap down). Using Wi-Fi.");

    /* Wi-Fi forced: unchanged, the adapter is not needed */
    mlxr_net_plan_init(&p, MLXR_NET_HOME, "test", 1, "", "");
    p.adapter_inputs = &none;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    CHECK(!p.fell_back && !p.toast[0]);

    /* the adapter forced and a station: the adapter is tried first, as before (it does not answer
     * here, the fall back is the old "not reachable" text) */
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", 1, "", "");
    p.adapter_inputs = &st;
    p.retry_ms = 300;
    mlxr_net_plan_add(&p, "198.51.100.7", 0);
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 1);
    CHECK(p.fell_back && !p.adapter_absent_fallback && p.c[0].probed);
    CHECK(strstr(p.toast, "not reachable (198.51.100.7: "));

    /* the pure choice: the adapter marked absent by hand */
    mlxr_net_cand home = mk(MLXR_PATH_HOME, "192.0.2.10", 7, 0, 3.0, 0.1, 98);
    mlxr_net_cand ad = mk(MLXR_PATH_ADAPTER, "198.51.100.7", 0, 0, 0, 0, 51);
    ad.probed = false;
    ad.skipped = true;
    p = plan2(MLXR_NET_ADAPTER, ad, home);
    mlxr_adapter_info a;
    make_inputs(&none, true, "up", "1", "", NULL);
    mlxr_net_adapter_assess(&none, "wlanap", NULL, 0, &a);
    p.adapter = a;
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(p.adapter_absent_fallback && strstr(p.toast, "no adapter found: wlanap up, no station"));
    /* an unchecked adapter keeps the old text */
    p = plan2(MLXR_NET_ADAPTER, mk(MLXR_PATH_ADAPTER, "198.51.100.7", 0, 4, 0, 0, 51), home);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(!p.adapter_absent_fallback && strstr(p.toast, "not reachable (198.51.100.7: no answer)"));
}

/* ---- Native (lane netpath-quality): the history file ---- */

static void put_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void test_history_file(void)
{
    const char *dir = make_tmpdir();
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, mlxr_nethist_filename());
    mlxr_nethist h;

    /* missing file: nothing */
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    CHECK(!h.e[0].valid && !h.e[1].valid);
    CHECK_EQ_INT(mlxr_nethist_load(NULL, &h), 0);
    CHECK_EQ_INT(mlxr_nethist_load("/nonexistent-mlxr-dir", &h), 0);

    /* write one path, read it back */
    mlxr_nethist_entry home = { true, 17510, 0, 1.25, 3.0, 1790000000 };
    CHECK(mlxr_nethist_record(dir, MLXR_NETHIST_HOME, &home));
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 1);
    CHECK(h.e[MLXR_NETHIST_HOME].valid && !h.e[MLXR_NETHIST_ADAPTER].valid);
    CHECK(h.e[0].frames == 17510 && h.e[0].lost == 0 && h.e[0].time_s == 1790000000);
    CHECK(fabs(h.e[0].jitter_ms - 1.25) < 1e-9 && fabs(h.e[0].rtt_ms - 3.0) < 1e-9);
    /* the second path is added, the first stays */
    mlxr_nethist_entry ad = { true, 4300, 99, 4.5, 6.0, 1790000500 };
    CHECK(mlxr_nethist_record(dir, MLXR_NETHIST_ADAPTER, &ad));
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 2);
    CHECK(h.e[0].frames == 17510 && h.e[1].frames == 4300 && h.e[1].lost == 99);
    CHECK(fabs(mlxr_nethist_loss_pct(&h.e[1]) - 100.0 * 99 / 4399) < 1e-9);
    CHECK(mlxr_nethist_loss_pct(&h.e[0]) == 0);
    /* a newer stream replaces its path's entry */
    mlxr_nethist_entry home2 = { true, 800, 40, 2.0, 5.0, 1790001000 };
    CHECK(mlxr_nethist_record(dir, MLXR_NETHIST_HOME, &home2));
    mlxr_nethist_load(dir, &h);
    CHECK(h.e[0].frames == 800 && h.e[0].lost == 40 && h.e[1].frames == 4300);
    CHECK(!mlxr_nethist_record(dir, 5, &home2));
    char tmp[620];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    CHECK(access(tmp, F_OK) != 0);   /* no temporary file left */

    /* the file holds the keyword, numbers and a time, and nothing else: no address, no name */
    char *text = slurp(path);
    CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "[home]") && strstr(text, "[adapter]") && strstr(text, "frames=800"));
        /* every line is a comment, "[word]" or key=number */
        char *save = NULL, *copy = strdup(text);
        for (char *l = strtok_r(copy, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            if (l[0] == '#' || l[0] == '[') continue;
            char *eq = strchr(l, '=');
            CHECK(eq != NULL);
            if (!eq) continue;
            for (char *v = eq + 1; *v; v++) CHECK((*v >= '0' && *v <= '9') || *v == '.');
        }
        free(copy);
        free(text);
    }
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 077) == 0);

    /* corrupt files: nothing is believed */
    put_file(path, "garbage\nnot an ini at all \x01\x02\n");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "[home]\nframes=abc\nlost=0\njitter_ms=1\nrtt_ms=2\ntime=1790000000\n");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "[home]\nframes=1000\nlost=-5\njitter_ms=1\nrtt_ms=2\ntime=1790000000\n");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "[home]\nframes=1000\nlost=5\njitter_ms=nan\nrtt_ms=2\ntime=1790000000\n");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "[home]\nframes=1000\nlost=5\njitter_ms=1\n");   /* cut short */
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "[home]\nframes=0\nlost=0\njitter_ms=1\nrtt_ms=2\ntime=1790000000\n");   /* empty stream */
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "frames=1000\nlost=5\njitter_ms=1\nrtt_ms=2\ntime=1790000000\n");   /* no section */
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    put_file(path, "");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 0);
    /* one good entry next to a bad one: the good one is kept; unknown sections and keys are ignored */
    put_file(path, "# comment\n[other]\nframes=5\n[home]\nframes=1000\nlost=5\njitter_ms=1.5\nrtt_ms=2\ntime=1790000000\nextra=1\n"
                   "[adapter]\nframes=x\n");
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 1);
    CHECK(h.e[0].valid && h.e[0].frames == 1000 && !h.e[1].valid);
    /* a corrupt file does not stop the next write */
    put_file(path, "garbage\n");
    CHECK(mlxr_nethist_record(dir, MLXR_NETHIST_ADAPTER, &ad));
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 1);
    CHECK(h.e[1].valid && h.e[1].frames == 4300);

    /* a stream ends: placed by its address on the real interfaces (loopback is "home"); short streams are not kept */
    unlink(path);
    CHECK(!mlxr_nethist_record_stream(dir, "127.0.0.1", 300, 100, 1, 2, 1790000000));   /* 400 frames */
    CHECK(access(path, F_OK) != 0);
    CHECK(mlxr_nethist_record_stream(dir, "127.0.0.1", 12000, 36, 0.8, 2.0, 1790000000));
    CHECK_EQ_INT(mlxr_nethist_load(dir, &h), 1);
    CHECK(h.e[MLXR_NETHIST_HOME].frames == 12000 && h.e[MLXR_NETHIST_HOME].lost == 36);
    CHECK(!mlxr_nethist_record_stream(NULL, "127.0.0.1", 12000, 36, 0.8, 2.0, 0));
    unlink(path);
    rmdir(dir);
}

/* A bare HTTP server on one loopback address: answers every request with a serverinfo-like page. The
 * fake host listens on 127.0.0.1 only; this one gives the second path an address of its own. */
typedef struct { int fd; volatile int stop; pthread_t th; } mini_srv;

static void *mini_loop(void *arg)
{
    mini_srv *m = arg;
    while (!m->stop) {
        struct pollfd pf = { m->fd, POLLIN, 0 };
        if (poll(&pf, 1, 20) <= 0) continue;
        int c = accept(m->fd, NULL, NULL);
        if (c < 0) continue;
        char buf[1024];
        struct pollfd cp = { c, POLLIN, 0 };
        if (poll(&cp, 1, 200) > 0) (void)recv(c, buf, sizeof buf, 0);
        const char *r = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: 62\r\nConnection: close\r\n\r\n"
                        "<root status_code=\"200\"><uniqueid>MINI-SRV-0001</uniqueid></root>";
        (void)send(c, r, strlen(r), MSG_NOSIGNAL);
        close(c);
    }
    return NULL;
}

static bool mini_start(mini_srv *m, const char *addr, uint16_t *port)
{
    m->stop = 0;
    m->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m->fd < 0) return false;
    struct sockaddr_in a = { .sin_family = AF_INET };
    inet_pton(AF_INET, addr, &a.sin_addr);
    socklen_t l = sizeof a;
    if (bind(m->fd, (struct sockaddr *)&a, sizeof a) || listen(m->fd, 16) || getsockname(m->fd, (struct sockaddr *)&a, &l)) {
        close(m->fd);
        return false;
    }
    *port = ntohs(a.sin_port);
    return pthread_create(&m->th, NULL, mini_loop, m) == 0;
}

static void mini_stop(mini_srv *m)
{
    m->stop = 1;
    pthread_join(m->th, NULL);
    close(m->fd);
}

/* The whole select with a fake interface table, two real servers (one per path) and a history file */
static void test_select_with_history(fake_host *h)
{
    mlxr_adapter_inputs with_station;
    station_inputs(&with_station);
    uint16_t home_port = fake_host_http_port(h), ad_port = 0;
    mini_srv m;
    if (!mini_start(&m, "127.0.0.2", &ad_port)) {
        fprintf(stderr, "note: 127.0.0.2 not available, history select test skipped\n");
        return;
    }
    mlxr_netifs t;
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 32, false, 0, false);
    add_if(&t, "wlanap", "127.0.0.2", 32, true, 2, false);
    const char *dir = make_tmpdir();
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, mlxr_nethist_filename());
    mlxr_net_plan p;
    const int64_t now = 1800000000;

    for (int round = 0; round < 4; round++) {
        mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "0123456789ABCDEF");
        p.adapter_inputs = &with_station;
        p.now_s = now;
        snprintf(p.history_dir, sizeof p.history_dir, "%s", dir);
        mlxr_net_plan_add(&p, "127.0.0.1", home_port);
        mlxr_net_plan_add(&p, "127.0.0.2", ad_port);
        mlxr_nethist hist;
        memset(&hist, 0, sizeof hist);
        if (round == 1) hist.e[MLXR_NETHIST_HOME] = (mlxr_nethist_entry){ true, 19000, 570, 3.0, 4, now - 3600 };       /* 2.9 percent */
        if (round == 2) hist.e[MLXR_NETHIST_ADAPTER] = (mlxr_nethist_entry){ true, 4000, 100, 6.0, 5, now - 86400 };    /* 2.4 percent */
        if (round == 1 || round == 2) mlxr_nethist_save(dir, &hist);
        if (round == 3) put_file(path, "this is not a history file\n");
        g_log[0] = 0;
        mlxr_set_log(capture_log, NULL);
        int pick = mlxr_net_select(&p, &t);
        mlxr_set_log(NULL, NULL);
        CHECK(p.c[0].path == MLXR_PATH_HOME && p.c[1].path == MLXR_PATH_ADAPTER);
        CHECK(p.c[0].sent >= 10 && p.c[1].sent >= 10);                 /* many probes per path */
        CHECK(p.c[0].ok == p.c[0].sent && p.c[1].ok == p.c[1].sent);   /* none lost on loopback */
        CHECK(p.c[0].p90_ms >= p.c[0].median_ms && mlxr_net_score(&p.c[0]) >= p.c[0].median_ms);
        CHECK(strstr(g_log, "score ") != NULL);
        if (round == 1) {
            CHECK_EQ_INT(pick, 1);   /* Wi-Fi lost 2.9 percent last time: the adapter */
            CHECK(p.c[0].hist_loss_pct > 2.8 && p.c[0].hist_penalty_ms > 55);
            CHECK(strstr(p.reason, "chose adapter") && strstr(p.reason, "the last stream's losses"));
            CHECK(strstr(g_log, "history: Wi-Fi lost 2.9% of 19570 frames last time") != NULL);
            CHECK(!strstr(g_log, "adapter lost"));
        } else if (round == 2) {
            CHECK_EQ_INT(pick, 0);   /* the adapter lost 2.4 percent: Wi-Fi (ties and the tail both point there) */
            CHECK(p.c[1].hist_penalty_ms > 45 && p.c[0].hist_loss_pct < 0);
            CHECK(strstr(g_log, "history: adapter lost 2.4% of 4100 frames last time"));
        } else {
            CHECK(p.c[0].hist_loss_pct < 0 && p.c[1].hist_loss_pct < 0);   /* none, or a damaged file: no penalty */
            CHECK(strstr(g_log, "history:") == NULL);
            CHECK(pick >= 0);
        }
        if (round == 0) unlink(path);
    }
    unlink(path);
    rmdir(dir);
    mini_stop(&m);
}

/* ---- Native (lane wave4c-usb): the USB cable ---- */

static void add_usb_if(mlxr_netifs *t, const char *name, const char *addr, int bits, bool up)
{
    add_if(t, name, addr, bits, false, 0, false);
    t->v[t->n - 1].usb = true;
    t->v[t->n - 1].up = up;
}

/* The Frame as the probe found it: Wi-Fi client with the default route, its own AP, and usb0 at the gadget's
 * /29 with the Frame on the first usable address; `cable` says whether a PC is plugged in (carrier) */
static void usb_table(mlxr_netifs *t, bool cable)
{
    frame_table(t, true);
    add_usb_if(t, "usb0", "198.51.100.233", 29, cable);
}

static void test_usb_peers(void)
{
    uint32_t v[16];
    int n;
    /* a /29 with the Frame on the first usable address: the PC's lease is the next one, then the pool */
    n = mlxr_net_subnet_peers(ip("198.51.100.233"), 0xfffffff8u, v, 16);
    CHECK_EQ_INT(n, MLXR_NET_USB_MAX_PEERS);
    CHECK(v[0] == ip("198.51.100.234") && v[1] == ip("198.51.100.235") && v[2] == ip("198.51.100.236"));
    n = mlxr_net_subnet_peers(ip("198.51.100.233"), 0xfffffff8u, v, 1);
    CHECK_EQ_INT(n, 1);
    CHECK(v[0] == ip("198.51.100.234"));
    /* the last usable address wraps to the bottom of the range, never onto the broadcast address */
    n = mlxr_net_subnet_peers(ip("198.51.100.238"), 0xfffffff8u, v, 16);
    CHECK_EQ_INT(n, 3);
    CHECK(v[0] == ip("198.51.100.233") && v[1] == ip("198.51.100.234") && v[2] == ip("198.51.100.235"));
    /* every address of the /29: the guesses are inside the usable range and never the Frame's own */
    uint32_t net = ip("198.51.100.232"), bcast = ip("198.51.100.239");
    for (uint32_t a = net + 1; a < bcast; a++) {
        n = mlxr_net_subnet_peers(a, 0xfffffff8u, v, 16);
        CHECK_EQ_INT(n, 3);
        for (int k = 0; k < n; k++) CHECK(v[k] != a && v[k] > net && v[k] < bcast);
    }
    /* a /30 has two usable addresses: the other one */
    n = mlxr_net_subnet_peers(ip("198.51.100.1"), 0xfffffffcu, v, 16);
    CHECK_EQ_INT(n, 1);
    CHECK(v[0] == ip("198.51.100.2"));
    n = mlxr_net_subnet_peers(ip("198.51.100.2"), 0xfffffffcu, v, 16);
    CHECK_EQ_INT(n, 1);
    CHECK(v[0] == ip("198.51.100.1"));
    /* a /31 (RFC 3021): both addresses are hosts */
    n = mlxr_net_subnet_peers(ip("198.51.100.4"), 0xfffffffeu, v, 16);
    CHECK(n == 1 && v[0] == ip("198.51.100.5"));
    n = mlxr_net_subnet_peers(ip("198.51.100.5"), 0xfffffffeu, v, 16);
    CHECK(n == 1 && v[0] == ip("198.51.100.4"));
    /* a /24 is guessed in the same way, the next three; the network and broadcast addresses are skipped */
    n = mlxr_net_subnet_peers(ip("192.168.1.233"), 0xffffff00u, v, 16);
    CHECK_EQ_INT(n, 3);
    CHECK(v[0] == ip("192.168.1.234") && v[1] == ip("192.168.1.235") && v[2] == ip("192.168.1.236"));
    n = mlxr_net_subnet_peers(ip("192.168.1.254"), 0xffffff00u, v, 16);
    CHECK_EQ_INT(n, 3);
    CHECK(v[0] == ip("192.168.1.1") && v[1] == ip("192.168.1.2") && v[2] == ip("192.168.1.3"));
    /* nothing to guess: a /32, a LAN larger than a /24, no mask, a mask that is no prefix, no room */
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("198.51.100.233"), 0xffffffffu, v, 16), 0);
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("192.168.1.233"), 0xffff0000u, v, 16), 0);
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("192.168.1.233"), 0, v, 16), 0);
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("192.168.1.233"), 0xff00ff00u, v, 16), 0);
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("198.51.100.233"), 0xfffffff8u, v, 0), 0);
    CHECK_EQ_INT(mlxr_net_subnet_peers(ip("198.51.100.233"), 0xfffffff8u, NULL, 4), 0);
}

static void test_usb_interfaces(void)
{
    mlxr_netifs t;
    mlxr_usb_info u;
    const mlxr_netif *via = NULL;
    mlxr_net_plan p;
    char b[96];

    CHECK_EQ_STR(mlxr_net_path_label(MLXR_PATH_USB), "USB cable");
    CHECK_EQ_STR(mlxr_net_pref_name(MLXR_NET_USB), "usb");

    /* usb0 DOWN with no carrier, as with nothing plugged in: not a candidate, no address goes onto it */
    usb_table(&t, false);
    mlxr_net_usb_assess(&t, &u);
    CHECK(u.checked && !u.present && !mlxr_net_usb_usable(&u));
    CHECK_EQ_STR(u.ifname, "usb0");
    CHECK_EQ_STR(u.reason, "usb0 down (no cable, or the PC end is not up)");
    CHECK(mlxr_net_classify(&t, "198.51.100.234", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "wlan0"));
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 47989, "", "");
    mlxr_net_plan_add(&p, "192.168.1.10", 0);
    CHECK_EQ_INT(mlxr_net_plan_add_usb(&p, &t), 0);
    CHECK_EQ_INT(p.n, 1);
    CHECK(p.usb.checked && !p.usb.present);

    /* no USB interface at all */
    frame_table(&t, true);
    mlxr_net_usb_assess(&t, &u);
    CHECK(u.checked && !u.present && !u.ifname[0]);
    CHECK_EQ_STR(u.reason, "no USB network interface found");

    /* usb0 UP with carrier: the other addresses of its /29 are candidates, the Frame's own is not */
    usb_table(&t, true);
    mlxr_net_usb_assess(&t, &u);
    CHECK(u.present && mlxr_net_usb_usable(&u) && u.peers == 3);
    CHECK_EQ_STR(u.reason, "usb0 up, carrier on, 3 peer candidates");
    CHECK(mlxr_net_classify(&t, "198.51.100.234", &via) == MLXR_PATH_USB && via && !strcmp(via->name, "usb0"));
    CHECK(mlxr_net_classify(&t, "198.51.100.241", NULL) == MLXR_PATH_HOME);   /* outside the /29 */
    CHECK(mlxr_net_classify(&t, "192.168.1.10", NULL) == MLXR_PATH_HOME);
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 47989, "", "");
    mlxr_net_plan_add(&p, "192.168.1.10", 0);
    CHECK_EQ_INT(mlxr_net_plan_add_usb(&p, &t), 3);
    CHECK_EQ_INT(p.n, 4);
    CHECK_EQ_STR(p.c[1].address, "198.51.100.234");
    CHECK_EQ_STR(p.c[2].address, "198.51.100.235");
    CHECK_EQ_STR(p.c[3].address, "198.51.100.236");
    CHECK_EQ_STR(p.c[1].source, "usb");
    for (int i = 0; i < p.n; i++) CHECK(strcmp(p.c[i].address, "198.51.100.233") != 0);
    CHECK_EQ_INT(mlxr_net_plan_add_usb(&p, &t), 0);   /* not added twice */
    CHECK_EQ_INT(p.n, 4);
    mlxr_net_plan_classify(&p, &t);
    CHECK(p.c[0].path == MLXR_PATH_HOME && !strcmp(p.c[0].ifname, "wlan0"));
    CHECK(p.c[1].path == MLXR_PATH_USB && !strcmp(p.c[1].ifname, "usb0") && p.c[1].onlink);
    mlxr_net_plan_read_links(&p, &t);
    CHECK(p.c[1].link.valid && !p.c[1].link.wifi && p.c[1].link_quality == 100.0);   /* a cable: no radio numbers */
    /* an address hosts.ini already knew is kept (and its source), the rest are added */
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 47989, "", "");
    mlxr_net_plan_add_src(&p, "198.51.100.234", 0, "hosts.ini");
    CHECK_EQ_INT(mlxr_net_plan_add_usb(&p, &t), 2);
    CHECK_EQ_STR(p.c[0].source, "hosts.ini");
    /* the cable's subnet is the only thing that makes an address "USB": a default route through a USB
     * tether is not a cable to the PC */
    usb_table(&t, true);
    t.v[1].default_route = false;
    t.v[3].default_route = true;
    CHECK(mlxr_net_classify(&t, "203.0.113.50", &via) == MLXR_PATH_HOME && via && !strcmp(via->name, "usb0"));

    /* the words */
    mlxr_net_link l;
    memset(&l, 0, sizeof l);
    l.valid = true;
    mlxr_net_path_describe(MLXR_PATH_USB, &l, b, sizeof b);
    CHECK_EQ_STR(b, "USB cable");
    mlxr_net_path_describe(MLXR_PATH_USB, NULL, b, sizeof b);
    CHECK_EQ_STR(b, "USB cable");
}

static void test_usb_watch(void)
{
    mlxr_usb_watch w;
    mlxr_usb_info u;
    mlxr_netifs off, on, gone;
    memset(&w, 0, sizeof w);
    usb_table(&off, false);
    usb_table(&on, true);
    frame_table(&gone, true);
    CHECK(mlxr_usb_watch_poll(&w, &off, &u) == MLXR_USB_EVENT_NONE);    /* the first poll without a cable says nothing */
    CHECK(mlxr_usb_watch_poll(&w, &off, &u) == MLXR_USB_EVENT_NONE);
    CHECK(mlxr_usb_watch_poll(&w, &on, &u) == MLXR_USB_EVENT_UP);       /* plugged in after the app started */
    CHECK(u.present && !strcmp(u.ifname, "usb0"));
    CHECK(mlxr_usb_watch_poll(&w, &on, &u) == MLXR_USB_EVENT_NONE);
    CHECK(mlxr_usb_watch_poll(&w, &off, &u) == MLXR_USB_EVENT_LOST);    /* pulled out */
    CHECK(!u.present && !strcmp(u.ifname, "usb0"));
    CHECK(mlxr_usb_watch_poll(&w, &off, NULL) == MLXR_USB_EVENT_NONE);
    CHECK(mlxr_usb_watch_poll(&w, &on, NULL) == MLXR_USB_EVENT_UP);
    CHECK(mlxr_usb_watch_poll(&w, &gone, &u) == MLXR_USB_EVENT_LOST);   /* the interface is gone altogether */
    CHECK_EQ_STR(u.ifname, "usb0");
    memset(&w, 0, sizeof w);
    CHECK(mlxr_usb_watch_poll(&w, &on, &u) == MLXR_USB_EVENT_UP);       /* already there at the first poll */
    CHECK_EQ_INT(MLXR_USB_WATCH_PERIOD_S, 10);
}

static void test_usb_choose(void)
{
    mlxr_net_cand usb = mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 1.0, 100);
    mlxr_net_cand wifi = mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 12.0, 60);
    mlxr_net_cand ad = mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 6.0, 38);
    mlxr_net_cand dead = mk(MLXR_PATH_USB, "198.51.100.234", 0, 4, 0, 0, 100);
    strcpy(dead.error, "connect timed out");
    mlxr_net_plan p;
    mlxr_netifs t;

    /* auto: the cable has the lower round trip and wins */
    p = plan2(MLXR_NET_AUTO, wifi, usb);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(p.chosen_path == MLXR_PATH_USB && !p.fell_back && !p.toast[0]);
    CHECK(strstr(p.reason, "chose USB cable: score 1.0 ms against Wi-Fi 12.0 ms (lower is better); decided by latency"));
    CHECK_EQ_STR(p.chosen_desc, "USB cable");
    CHECK_EQ_STR(p.connected, "Connected over USB cable.");
    CHECK(strstr(p.summary, "usb 198.51.100.234 (auto)"));
    p = plan2(MLXR_NET_AUTO, usb, wifi);   /* the order does not matter */
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    /* all three paths: the cable against the best of the others (the adapter here) */
    p = plan2(MLXR_NET_AUTO, wifi, usb);
    p.c[2] = ad;
    p.n = 3;
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose USB cable: score 1.0 ms against adapter 6.0 ms (lower is better); decided by latency"));
    /* Wi-Fi with the lower round trip beats the cable */
    p = plan2(MLXR_NET_AUTO, mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 2.0, 100), mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 9.0, 100));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose Wi-Fi: score 2.0 ms against USB cable 9.0 ms"));
    /* a tie: the better link decides (a cable has no radio), with no link data Wi-Fi, and the cable beats the adapter */
    p = plan2(MLXR_NET_AUTO, mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 4.0, 60), mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 3.0, 100));
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose USB cable: scores within 3 ms (Wi-Fi 4.0, USB cable 3.0), the better link breaks the tie (quality 100 against 60)"));
    p = plan2(MLXR_NET_AUTO, mkflat(MLXR_PATH_HOME, "192.168.1.10", 15, 0, 4.0, 0), mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 3.0, 0));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "Wi-Fi preferred on a tie"));
    p = plan2(MLXR_NET_AUTO, mkflat(MLXR_PATH_ADAPTER, "10.35.50.27", 15, 0, 5.0, 0), mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 6.0, 0));
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "USB cable preferred on a tie"));
    /* only one path answered */
    p = plan2(MLXR_NET_AUTO, usb, mk(MLXR_PATH_HOME, "192.168.1.10", 0, 4, 0, 0, 98));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "chose USB cable: the only path that answered (score 1.0 ms)"));
    p = plan2(MLXR_NET_AUTO, dead, wifi);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(strstr(p.reason, "chose Wi-Fi: the only path that answered"));
    /* the history charges the cable like the other paths: its last stream lost 2.3 percent of the frames */
    p = plan2(MLXR_NET_AUTO, wifi, mkflat(MLXR_PATH_USB, "198.51.100.234", 15, 0, 3.0, 100));
    p.now_s = 1800000000;
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    p.hist.e[MLXR_NETHIST_USB] = (mlxr_nethist_entry){ true, 9770, 230, 1.0, 4, p.now_s - 7200 };
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(strstr(p.reason, "decided by the last stream's losses (USB cable: 2.3% lost last time; Wi-Fi: no history)"));

    /* USB asked for and the cable answers: that path, no toast */
    p = plan2(MLXR_NET_USB, wifi, usb);
    CHECK_EQ_INT(mlxr_net_choose(&p), 1);
    CHECK(!p.fell_back && !p.toast[0] && p.chosen_path == MLXR_PATH_USB);
    CHECK(strstr(p.reason, "chose USB cable: the setting is usb"));
    /* USB asked for, a cable is in, the PC does not answer on it: the other path, with the failed address */
    usb_table(&t, true);
    p = plan2(MLXR_NET_USB, wifi, dead);
    mlxr_net_usb_assess(&t, &p.usb);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back && !p.usb_absent_fallback && p.chosen_path == MLXR_PATH_HOME);
    CHECK_EQ_STR(p.toast, "USB cable not reachable (198.51.100.234: connect timed out). Using Wi-Fi.");
    /* USB asked for and usb0 is DOWN: the notice says there is no cable */
    usb_table(&t, false);
    p = plan2(MLXR_NET_USB, wifi, mk(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 0, 0, 0, 0));
    p.c[1].probed = false;
    mlxr_net_usb_assess(&t, &p.usb);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(p.fell_back && p.usb_absent_fallback && !p.adapter_absent_fallback);
    CHECK_EQ_STR(p.failure, "no USB cable found: usb0 down (no cable, or the PC end is not up)");
    CHECK_EQ_STR(p.toast, "USB cable not reachable (no USB cable found: usb0 down (no cable, or the PC end is not up)). Using Wi-Fi.");
    /* ... and with no USB interface at all */
    frame_table(&t, true);
    p = plan2(MLXR_NET_USB, wifi, mk(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 0, 0, 0, 0));
    mlxr_net_usb_assess(&t, &p.usb);
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK_EQ_STR(p.toast, "USB cable not reachable (no USB cable found: no USB network interface found). Using Wi-Fi.");
    /* a plan nobody assessed keeps the generic words */
    p = plan2(MLXR_NET_USB, wifi, mk(MLXR_PATH_ADAPTER, "10.35.50.27", 0, 0, 0, 0, 0));
    CHECK_EQ_INT(mlxr_net_choose(&p), 0);
    CHECK(!p.usb_absent_fallback);
    CHECK_EQ_STR(p.toast, "USB cable not reachable (no address of the PC known on this network). Using Wi-Fi.");

    /* the history file knows the cable */
    const char *dir = make_tmpdir();
    mlxr_nethist_entry e = { true, 5000, 10, 0.5, 1.0, 1790000000 };
    mlxr_nethist hh;
    CHECK_EQ_STR(mlxr_nethist_key(MLXR_NETHIST_USB), "usb");
    CHECK_EQ_INT(mlxr_net_hist_index(MLXR_PATH_USB), MLXR_NETHIST_USB);
    CHECK(mlxr_nethist_record(dir, MLXR_NETHIST_USB, &e));
    CHECK_EQ_INT(mlxr_nethist_load(dir, &hh), 1);
    CHECK(hh.e[MLXR_NETHIST_USB].valid && !hh.e[MLXR_NETHIST_HOME].valid && hh.e[MLXR_NETHIST_USB].frames == 5000);
}

static void test_usb_select(fake_host *h)
{
    uint16_t live = fake_host_http_port(h);
    mlxr_adapter_inputs st;
    station_inputs(&st);
    mlxr_netifs t;
    mlxr_net_plan p;
    struct timespec t0, t1;

    /* USB asked for, usb0 DOWN: nothing on the cable is probed, no retry window, the other path with the notice */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_usb_if(&t, "usb0", "198.51.100.233", 29, false);
    mlxr_net_plan_init(&p, MLXR_NET_USB, "test", 1, "", "");
    p.adapter_inputs = &st;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    g_log[0] = 0;
    mlxr_set_log(capture_log, NULL);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    mlxr_set_log(NULL, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    CHECK(ms < 2000);   /* the 4 s retry window was not waited */
    CHECK_EQ_INT(p.n, 1);
    CHECK(p.fell_back && p.usb_absent_fallback && p.chosen_path == MLXR_PATH_HOME);
    CHECK_EQ_STR(p.toast, "USB cable not reachable (no USB cable found: usb0 down (no cable, or the PC end is not up)). Using Wi-Fi.");
    CHECK(strstr(g_log, "network path: USB cable: usb0 down (no cable, or the PC end is not up)"));

    /* automatic, usb0 UP: the three guesses join the candidates (nothing answers on them here), with the log line */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    add_usb_if(&t, "usb0", "198.51.100.233", 29, true);
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &st;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    g_log[0] = 0;
    mlxr_set_log(capture_log, NULL);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    mlxr_set_log(NULL, NULL);
    CHECK_EQ_INT(p.n, 4);
    CHECK(p.c[1].path == MLXR_PATH_USB && !strcmp(p.c[1].source, "usb") && !strcmp(p.c[1].ifname, "usb0"));
    CHECK(p.c[1].probed && p.c[1].ok == 0);
    CHECK(p.usb.present && p.usb.peers == 3);
    CHECK(strstr(g_log, "network path: USB cable: usb0 up, carrier on, 3 peer candidates"));
    CHECK(strstr(g_log, "198.51.100.234 (usb, USB cable via usb0)"));

    /* USB asked for, a cable is in and the PC does not answer on it: retried for retry_ms, then the other path */
    mlxr_net_plan_init(&p, MLXR_NET_USB, "test", 1, "", "");
    p.adapter_inputs = &st;
    p.retry_ms = 300;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    CHECK(p.fell_back && !p.usb_absent_fallback && p.c[1].probed);
    CHECK(strstr(p.toast, "USB cable not reachable (198.51.100.234: "));
}

/* Native (lane wave4b-abr): the path kind a start bitrate is chosen for */
static void test_bitrate_kind(void)
{
    mlxr_net_link wifi, wired, unknown;
    memset(&wifi, 0, sizeof wifi);
    memset(&wired, 0, sizeof wired);
    memset(&unknown, 0, sizeof unknown);
    wifi.valid = wifi.wifi = true;
    wired.valid = true;
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_HOME, &wifi), 0);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_HOME, &wired), 2);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_HOME, &unknown), 0);   /* nothing known about the link: Wi-Fi */
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_HOME, NULL), 0);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_ADAPTER, &wifi), 1);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_ADAPTER, NULL), 1);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_USB, &wired), 3);
    CHECK_EQ_INT(mlxr_net_bitrate_kind(MLXR_PATH_NONE, &wifi), -1);
    CHECK_EQ_INT(mlxr_net_kind_for_address(NULL), -1);
    CHECK_EQ_INT(mlxr_net_kind_for_address(""), -1);
    CHECK_EQ_INT(mlxr_net_kind_for_address("203.0.113.99"), -1);       /* never chosen */
}

static void test_bitrate_kind_select(fake_host *h)
{
    uint16_t live = fake_host_http_port(h);
    mlxr_adapter_inputs st;
    station_inputs(&st);
    mlxr_netifs t;
    mlxr_net_plan p;

    /* the fake PC reached over an ethernet-like interface: a wired network */
    memset(&t, 0, sizeof t);
    add_if(&t, "lo", "127.0.0.1", 8, false, 0, false);
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &st;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    CHECK(p.c[0].link.valid && !p.c[0].link.wifi);
    CHECK_EQ_INT(mlxr_net_kind_for_address("127.0.0.1"), 2);

    /* the same address through a Wi-Fi interface: Wi-Fi, the newest choice wins */
    memset(&t, 0, sizeof t);
    add_if(&t, "wlan0", "127.0.0.1", 8, true, 1, true);
    mlxr_net_plan_init(&p, MLXR_NET_AUTO, "test", 1, "", "");
    p.adapter_inputs = &st;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    CHECK_EQ_INT(mlxr_net_kind_for_address("127.0.0.1"), 0);

    /* the forced Steam Frame Wireless Adapter with no adapter present falls back to Wi-Fi: the path that carries it */
    memset(&t, 0, sizeof t);
    add_if(&t, "wlan0", "127.0.0.1", 8, true, 1, true);
    add_if(&t, "wlanap", "198.51.100.1", 24, true, 2, false);
    mlxr_adapter_inputs none;
    make_inputs(&none, true, "up", "1", "", ARP_PC);
    mlxr_net_plan_init(&p, MLXR_NET_ADAPTER, "test", 1, "", "");
    p.adapter_inputs = &none;
    mlxr_net_plan_add(&p, "127.0.0.1", live);
    CHECK_EQ_INT(mlxr_net_select(&p, &t), 0);
    CHECK(p.adapter_absent_fallback);
    CHECK_EQ_INT(mlxr_net_kind_for_address("127.0.0.1"), 0);
}

static void test_probe(void)
{
    /* Native (lane adapter-detect): select looks at the adapter; these tests say a station is there */
    mlxr_adapter_inputs with_station;
    station_inputs(&with_station);
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
    p.adapter_inputs = &with_station;
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
    p.adapter_inputs = &with_station;
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
    /* "gaming-pc.local" spelled out once, then pointed to */
    const uint8_t n1[] = { 9, 'G', 'a', 'm', 'i', 'n', 'g', '-', 'p', 'c', 5, 'l', 'o', 'c', 'a', 'l', 0 };
    const uint8_t ptr[] = { 0xC0, 12 };
    const uint8_t other[] = { 5, 'p', 'r', 'i', 'n', 't', 5, 'l', 'o', 'c', 'a', 'l', 0 };
    const uint8_t ip1[] = { 192, 168, 1, 10 }, ip2[] = { 10, 35, 50, 27 }, ip3[] = { 192, 168, 1, 50 };
    o = put_rr(m, o, n1, sizeof n1, ip1);
    o = put_rr(m, o, ptr, sizeof ptr, ip2);
    o = put_rr(m, o, other, sizeof other, ip3);
    char out[8][128];
    const char *src[8];
    int n = mlxr_net_mdns_parse(m, o, "gaming-pc.local", out, src, 0, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(out[0], "192.168.1.10");
    CHECK_EQ_STR(out[1], "10.35.50.27");
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
    mlxr_net_plan_add_src(&p, "10.35.50.27", 0, "launcher");
    mlxr_net_plan_add_record(&p, "10.35.50.27", "192.168.1.10,203.0.113.50");
    mlxr_net_plan_add_src(&p, "192.168.1.10", 0, "mdns");   /* a duplicate: the first source stays */
    CHECK_EQ_INT(p.n, 3);
    CHECK_EQ_STR(p.c[0].source, "launcher");
    CHECK_EQ_STR(p.c[1].source, "hosts.ini");
    mlxr_net_plan_classify(&p, &t);
    CHECK(p.c[0].onlink && p.c[1].onlink && !p.c[2].onlink);
    CHECK(mlxr_net_onlink_if(&t, "192.168.1.99") && !strcmp(mlxr_net_onlink_if(&t, "192.168.1.99")->name, "wlan0"));
    CHECK(mlxr_net_onlink_if(&t, "8.8.8.8") == NULL && mlxr_net_onlink_if(&t, "host.local") == NULL);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);   /* the in-process fake host writes to probes that hung up */
    test_pref();
    test_classify();
    test_candidates();
    test_stats_and_scoring();
    test_history_file();
    test_choose();
    test_describe();
    test_mdns_and_sources();
    test_adapter_assess();
    test_usb_peers();
    test_usb_interfaces();
    test_usb_watch();
    test_usb_choose();
    test_bitrate_kind();
    {
        fake_opts o = { 0 };
        fake_host *h = fake_host_start(&o);
        CHECK(h != NULL);
        if (h) {
            test_adapter_plan(h);
            test_select_with_history(h);
            test_usb_select(h);
            test_bitrate_kind_select(h);
            fake_host_stop(h);
        }
    }
    test_probe();
    TEST_DONE();
}
