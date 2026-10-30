// Native (lane wave1-failover): the failover loop (stream/failover.h) driven by a fake session on a clock the
// test moves: when it switches, what it forces and abandons, what it says, and that it never flaps. GPLv3.
#include <stdio.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/failover.h"

typedef struct {
    int enabled, burstMs;
    uint64_t connected;                 // 0: no stream
    uint64_t rx, lost;                  // cumulative frame counts
    mlxr_failover_history hist;
    int haveOpen;
    mlxr_failover_burst open;
    mlxr_net_quality q;
    // what the loop did
    int noted, forced, abandoned, noticed, logged;
    char logLine[400];
    char noteFrom[48], noteTo[48], notice[200];
    mlxr_net_path forcedTo, abandonedFrom;
} Fake;

static uint64_t fkConnected(void* u) { return ((Fake*)u)->connected; }
static int fkFigures(void* u, uint64_t* rx, uint64_t* lost) {
    Fake* f = u;
    *rx = f->rx;
    *lost = f->lost;
    return f->rx + f->lost > 0;
}
static int fkLoss(void* u, uint64_t windowMs, mlxr_failover_window* w) {
    Fake* f = u;
    if (!f->connected) return 0;
    uint64_t now = f->open.endMs ? f->open.endMs : 0;
    (void)now;
    mlxr_failover_history_window(&f->hist, f->haveOpen ? &f->open : NULL, f->open.endMs, windowMs, w);
    return 1;
}
static void fkQuality(void* u, mlxr_net_quality* q) { *q = ((Fake*)u)->q; }
static int fkEnabled(void* u, int* burstMs) {
    Fake* f = u;
    *burstMs = f->burstMs;
    return f->enabled;
}
static void fkNote(void* u, const char* from, const char* to) {
    Fake* f = u;
    f->noted++;
    snprintf(f->noteFrom, sizeof f->noteFrom, "%s", from);
    snprintf(f->noteTo, sizeof f->noteTo, "%s", to);
}
static void fkForce(void* u, mlxr_net_path to) { ((Fake*)u)->forced++; ((Fake*)u)->forcedTo = to; }
static void fkAbandon(void* u, mlxr_net_path from) { ((Fake*)u)->abandoned++; ((Fake*)u)->abandonedFrom = from; }
static void fkNotice(void* u, const char* t) { Fake* f = u; f->noticed++; snprintf(f->notice, sizeof f->notice, "%s", t); }

static void fkLog(void* u, const char* l) { Fake* f = u; f->logged++; snprintf(f->logLine, sizeof f->logLine, "%s", l); }

static MlxrFailoverOps opsFor(Fake* f) {
    MlxrFailoverOps o = { f, fkConnected, fkFigures, fkLoss, fkQuality, fkEnabled, fkNote, fkForce, fkAbandon, fkNotice, fkLog };
    return o;
}

// Wi-Fi chosen at the start (6 ms), the adapter measured 22 ms, no cable; probed at ms 0
static void startFake(Fake* f, int64_t probedMs) {
    memset(f, 0, sizeof(*f));
    f->enabled = 1;
    f->burstMs = 3000;
    f->connected = 5000;
    mlxr_failover_history_init(&f->hist);
    f->q.valid = 1;
    f->q.pref_auto = 1;
    f->q.chosen = MLXR_PATH_HOME;
    for (int i = 0; i < 4; i++) f->q.p[i].score_ms = -1;
    f->q.p[MLXR_PATH_HOME] = (mlxr_net_path_quality){ 1, 6.0, probedMs, "192.0.2.10" };
    f->q.p[MLXR_PATH_ADAPTER] = (mlxr_net_path_quality){ 1, 22.0, probedMs, "198.51.100.7" };
}

// One second of stream at 90 fps: lostPerSec frames lost; inLoss keeps a burst open from lossStart
static void second(Fake* f, int64_t t, int lostPerSec, int64_t* lossStart) {
    f->rx += 90 - (uint64_t)lostPerSec;
    f->lost += (uint64_t)lostPerSec;
    if (lostPerSec > 0) {
        if (!f->haveOpen) { f->haveOpen = 1; *lossStart = t; f->open.startMs = (uint64_t)t; }
        f->open.endMs = (uint64_t)t;
        f->open.frames += (uint64_t)lostPerSec;
    } else if (f->haveOpen) {
        mlxr_failover_history_add(&f->hist, f->open.startMs, f->open.endMs, f->open.frames);
        f->haveOpen = 0;
        memset(&f->open, 0, sizeof f->open);
    }
    if (!f->haveOpen) f->open.endMs = (uint64_t)t;   // the fake's clock for lossWindow
}

static void testSwitch(void) {
    Fake f;
    startFake(&f, 0);
    MlxrFailover fo;
    memset(&fo, 0, sizeof fo);
    MlxrReconnect rc;
    mlxr_reconnect_init(&rc);
    MlxrFailoverOps ops = opsFor(&f);
    int64_t lossStart = 0, switchedAt = -1;
    // 60 s of clean video, then a burst of 33 percent loss that does not stop
    for (int64_t t = 1000; t <= 100000 && switchedAt < 0; t += 1000) {
        int lossy = t >= 60000;
        second(&f, t, lossy ? 30 : 0, &lossStart);
        if (mlxr_failover_step(&fo, &rc, &ops, t)) switchedAt = t;
    }
    // the loss began at 60 s; 3000 ms of it is summed at 63 s
    CHECK(switchedAt == 63000);
    CHECK(f.forced == 1 && f.forcedTo == MLXR_PATH_ADAPTER && f.abandoned == 1 && f.abandonedFrom == MLXR_PATH_HOME);
    CHECK(f.noted == 1 && !strcmp(f.noteFrom, "Wi-Fi") && !strcmp(f.noteTo, "Steam Frame Wireless Adapter"));
    CHECK(f.logged == 1 && !strncmp(f.logLine, "network path: switching from Wi-Fi to Steam Frame Wireless Adapter after 3000 ms of loss", 80));
    CHECK(f.noticed == 1 && strstr(f.notice, "Steam Frame Wireless Adapter") && strstr(f.notice, "Wi-Fi"));
    CHECK(rc.state == MLXR_RC_WAITING && rc.attempt == 1 && rc.dueMs == 63000 + MLXR_RECONNECT_SWITCH_DELAY_MS);
    CHECK(fo.switches == 1 && fo.hasSwitched && fo.lastSwitchMs == 63000);

    // nothing more while the reconnect is on its way or the series is waiting, however bad it is
    for (int64_t t = 64000; t < 70000; t += 1000) {
        second(&f, t, 30, &lossStart);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);
    }
    CHECK(f.forced == 1 && f.noted == 1);

    // the reconnect worked: a new stream on the adapter (probed again for the forced choice), loss goes on
    mlxr_reconnect_cancel(&rc);
    f.connected = 71000;
    f.rx = f.lost = 0;
    f.haveOpen = 0;
    memset(&f.open, 0, sizeof f.open);
    mlxr_failover_history_init(&f.hist);
    f.q.chosen = MLXR_PATH_ADAPTER;
    f.q.p[MLXR_PATH_HOME].present = 0;   // abandoned
    for (int64_t t = 72000; t < 72000 + 280000; t += 1000) {
        second(&f, t, 30, &lossStart);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);   // young stream, then the cooldown, then nowhere to go
    }
    CHECK(f.forced == 1);
    CHECK(strstr(fo.lastWhy, "no other path") != NULL || strstr(fo.lastWhy, "switched lately") != NULL);

    // the cable (measured at the start) is the way out, once the five minutes are over
    f.q.p[MLXR_PATH_USB] = (mlxr_net_path_quality){ 1, 4.0, 0, "198.51.100.234" };
    int64_t again = -1;
    for (int64_t t = 352000; t < 400000 && again < 0; t += 1000) {
        second(&f, t, 30, &lossStart);
        if (mlxr_failover_step(&fo, &rc, &ops, t)) again = t;
    }
    CHECK(again >= 63000 + MLXR_FAILOVER_MIN_GAP_MS);   // not before five minutes after the first move
    CHECK(f.forced == 2 && f.forcedTo == MLXR_PATH_USB && f.abandonedFrom == MLXR_PATH_ADAPTER && fo.switches == 2);
}

static void testQuiet(void) {
    Fake f;
    MlxrFailover fo;
    MlxrReconnect rc;
    int64_t ls = 0;

    // off: the worst loss does nothing
    startFake(&f, 0);
    f.enabled = 0;
    memset(&fo, 0, sizeof fo);
    mlxr_reconnect_init(&rc);
    MlxrFailoverOps ops = opsFor(&f);
    for (int64_t t = 1000; t < 120000; t += 1000) {
        second(&f, t, t > 30000 ? 60 : 0, &ls);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);
    }
    CHECK(f.forced == 0 && !mlxr_reconnect_active(&rc));

    // a path set by hand (Automatic is the only one a stream leaves)
    startFake(&f, 0);
    f.q.pref_auto = 0;
    memset(&fo, 0, sizeof fo);
    ops = opsFor(&f);
    for (int64_t t = 1000; t < 120000; t += 1000) {
        second(&f, t, t > 30000 ? 60 : 0, &ls);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);
    }
    CHECK(f.forced == 0 && strstr(fo.lastWhy, "by hand"));

    // loss in short bursts that never reach 3 s inside 10 s: 1 s of loss, 4 s clean, again and again
    startFake(&f, 0);
    memset(&fo, 0, sizeof fo);
    ops = opsFor(&f);
    for (int64_t t = 1000; t < 200000; t += 1000) {
        second(&f, t, (t / 1000) % 5 == 0 ? 40 : 0, &ls);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);
    }
    CHECK(f.forced == 0 && strstr(fo.lastWhy, "not enough loss"));

    // the other path was never there (no adapter): nowhere to go
    startFake(&f, 0);
    f.q.p[MLXR_PATH_ADAPTER].present = 0;
    memset(&fo, 0, sizeof fo);
    ops = opsFor(&f);
    for (int64_t t = 1000; t < 120000; t += 1000) {
        second(&f, t, t > 30000 ? 60 : 0, &ls);
        CHECK(mlxr_failover_step(&fo, &rc, &ops, t) == 0);
    }
    CHECK(f.forced == 0 && strstr(fo.lastWhy, "no other path"));

    // the setting turned on in the middle of a bad stretch takes hold at the next second; a lower threshold too
    startFake(&f, 0);
    f.enabled = 0;
    f.burstMs = 1000;
    memset(&fo, 0, sizeof fo);
    mlxr_reconnect_init(&rc);
    ops = opsFor(&f);
    int64_t at = -1;
    for (int64_t t = 1000; t < 120000 && at < 0; t += 1000) {
        second(&f, t, t > 30000 ? 60 : 0, &ls);
        if (t == 50000) f.enabled = 1;
        if (mlxr_failover_step(&fo, &rc, &ops, t)) at = t;
    }
    CHECK(at == 50000);   // the second the setting was turned on

    // no stream connected, or the choice never made: nothing
    startFake(&f, 0);
    f.connected = 0;
    memset(&fo, 0, sizeof fo);
    mlxr_reconnect_init(&rc);
    ops = opsFor(&f);
    CHECK(mlxr_failover_step(&fo, &rc, &ops, 100000) == 0 && fo.streamKey == 0);
    f.connected = 7;
    f.q.valid = 0;
    CHECK(mlxr_failover_step(&fo, &rc, &ops, 200000) == 0);
}

static void testPollRate(void) {
    Fake f;
    startFake(&f, 0);
    MlxrFailover fo;
    memset(&fo, 0, sizeof fo);
    MlxrReconnect rc;
    mlxr_reconnect_init(&rc);
    MlxrFailoverOps ops = opsFor(&f);
    f.rx = 90;
    mlxr_failover_step(&fo, &rc, &ops, 10000);
    CHECK(fo.samples.n == 1);
    mlxr_failover_step(&fo, &rc, &ops, 10500);   // less than a second later: nothing
    mlxr_failover_step(&fo, &rc, &ops, 10999);
    CHECK(fo.samples.n == 1);
    mlxr_failover_step(&fo, &rc, &ops, 11000);
    CHECK(fo.samples.n == 2);
    // a new stream starts the counts again
    f.connected = 9999;
    mlxr_failover_step(&fo, &rc, &ops, 12000);
    CHECK(fo.samples.n == 1 && fo.streamStartMs == 12000);
}

static void testText(void) {
    char log[200], note[200];
    mlxr_failover_switch_text(MLXR_PATH_HOME, MLXR_PATH_ADAPTER, 3450, log, sizeof log, note, sizeof note);
    CHECK(!strcmp(log, "network path: switching from Wi-Fi to Steam Frame Wireless Adapter after 3450 ms of loss"));
    mlxr_failover_switch_text(MLXR_PATH_ADAPTER, MLXR_PATH_USB, 3000, log, sizeof log, NULL, 0);
    CHECK(!strcmp(log, "network path: switching from Steam Frame Wireless Adapter to USB cable after 3000 ms of loss"));
}

int main(void) {
    testSwitch();
    testQuiet();
    testPollRate();
    testText();
    return checksDone("stream failover");
}
