// Moonlight XR Native (lane perf): the RFI watchdog (stream/rfi_watch.h) fed
// moonlight-common-c's log lines as the owner's 23:46 run had them. GPLv3.
#include <stdio.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/rfi_watch.h"

#define REQ "Invalidate reference frame request sent (28 to 28)"
#define WAIT "Waiting for RFI frame"
#define PFRAME "Next post-invalidation frame is: 18 (P-frame)"
#define IFRAME "Next post-invalidation frame is: 492 (I-frame)"

// Native (lane wave1-burst): the adaptive wait, table-driven. Each case is a run of synthetic
// common-c lines: one RFI request at 0, a "Waiting for RFI frame" every waitEvery ms (frames flow
// and are dropped) and an "Unrecoverable frame" every lossEvery ms between lossFrom and lossTo (the
// burst still losing packets). At equal times the loss line comes first.
typedef struct {
    const char* name;
    int timeoutMs;
    uint32_t rtt;
    int lossFrom, lossTo, lossEvery; // lossEvery 0: a clean link
    int waitEvery, end;
    int asks[4];                     // when the watchdog asks, 0 ends the list
    int heldByLoss;
} AdaptCase;

static const AdaptCase ADAPT[] = {
    // A clean LAN: the floor, then every 300 ms (as before the lane)
    {"clean, rtt 5 ms", 100, 5, 0, 0, 0, 20, 700, {100, 400, 700}, 0},
    // A long round trip: three of them before asking
    {"clean, rtt 60 ms", 100, 60, 0, 0, 0, 20, 400, {180}, 0},
    // A burst losing frames until 250 ms: the ask waits for 100 ms without loss (350, the
    // first dropped frame after it at 360), then 300 ms between asks
    {"burst until 250 ms", 100, 5, 50, 250, 50, 20, 700, {360, 660}, 1},
    // A burst that does not stop: asks at the ceiling (1000 ms), then again a ceiling later
    {"burst that does not stop", 100, 5, 50, 2400, 50, 20, 2400, {1000, 2000}, 1},
    // The burst's rtt term and its loss term together: base 150 ms after the last loss (400)
    {"burst, rtt 50 ms", 100, 50, 100, 400, 100, 20, 600, {560}, 1},
    // Off: never asks
    {"off", 0, 5, 50, 500, 50, 20, 2000, {0}, 0},
};

static void test_adaptive(void) {
    for (size_t c = 0; c < sizeof(ADAPT) / sizeof(ADAPT[0]); c++) {
        const AdaptCase* k = &ADAPT[c];
        MlxrRfiWatch w;
        mlxr_rfi_watch_init(&w, k->timeoutMs, 3, 300);
        mlxr_rfi_watch_set_rtt(&w, k->rtt);
        int got[8] = {0}, n = 0;
        mlxr_rfi_watch_line(&w, REQ, 0);
        for (int t = 1; t <= k->end; t++) {
            if (k->lossEvery && t >= k->lossFrom && t <= k->lossTo && t % k->lossEvery == 0)
                CHECK(mlxr_rfi_watch_line(&w, "Unrecoverable frame 40: 10+2=12 received < 14 needed", (uint64_t)t) == 0);
            if (t % k->waitEvery == 0 && mlxr_rfi_watch_line(&w, WAIT, (uint64_t)t) && n < 8) got[n++] = t;
        }
        int want = 0;
        while (want < 4 && k->asks[want]) want++;
        int same = n == want;
        for (int i = 0; same && i < n; i++) same = got[i] == k->asks[i];
        if (!same) {
            fprintf(stderr, "adaptive case \"%s\": asked at", k->name);
            for (int i = 0; i < n; i++) fprintf(stderr, " %d", got[i]);
            fprintf(stderr, " (want %d asks)\n", want);
        }
        CHECK(same);
        CHECK(w.heldByLoss == (uint64_t)k->heldByLoss);
    }
    // the wait the log shows: the base on a clean link, and longer while the burst loses frames
    MlxrRfiWatch w;
    mlxr_rfi_watch_init(&w, 100, 3, 300);
    mlxr_rfi_watch_set_rtt(&w, 60);
    CHECK(mlxr_rfi_watch_base_ms(&w) == 180);
    CHECK(mlxr_rfi_watch_wait_ms(&w, 0) == 180);
    mlxr_rfi_watch_line(&w, REQ, 1000);
    CHECK(mlxr_rfi_watch_wait_ms(&w, 1000) == 180);
    mlxr_rfi_watch_line(&w, "Unrecoverable frame 41: lost FEC blocks 1 to 2", 1500);
    CHECK(mlxr_rfi_watch_wait_ms(&w, 1500) == 680);
    mlxr_rfi_watch_line(&w, "Network dropped 4 frames (frames 41 to 44)", 1900);
    CHECK(mlxr_rfi_watch_wait_ms(&w, 1900) == 1000); // the ceiling
    // a round trip beyond the ceiling is held to it; the ceiling never under the floor
    mlxr_rfi_watch_set_rtt(&w, 900);
    CHECK(mlxr_rfi_watch_base_ms(&w) == 1000);
    mlxr_rfi_watch_set_ceiling(&w, 50);
    CHECK(w.ceilMs == 100);
}

// Native (lane wave1-burst): at most one IDR request of ours per rfi_idr_ms
static void test_idr_cap(void) {
    MlxrIdrCap c;
    mlxr_idr_cap_init(&c, 500);
    CHECK(mlxr_idr_cap_take(&c, 0) == 1); // the first one goes, even at 0 ms
    CHECK(mlxr_idr_cap_take(&c, 100) == 0);
    CHECK(mlxr_idr_cap_take(&c, 499) == 0);
    CHECK(mlxr_idr_cap_take(&c, 500) == 1);
    CHECK(mlxr_idr_cap_take(&c, 900) == 0);
    CHECK(mlxr_idr_cap_take(&c, 1000) == 1);
    CHECK(c.passed == 3 && c.held == 3);
    mlxr_idr_cap_init(&c, 0); // off: every ask goes
    for (int i = 0; i < 5; i++) CHECK(mlxr_idr_cap_take(&c, 10) == 1);
    CHECK(c.passed == 5 && c.held == 0);
    mlxr_idr_cap_init(&c, -3);
    CHECK(c.minGapMs == 0);
}

// Native (lane wave1-burst): the burst tracker on the lines one lossy second gives
static void test_bursts(void) {
    int n = -1;
    CHECK(mlxr_burst_classify("Network dropped 1 frame (frame 5)", &n) == MLXR_BL_NET_DROPPED && n == 1);
    CHECK(mlxr_burst_classify("Network dropped 30 frames (frames 6 to 35)", &n) == MLXR_BL_NET_DROPPED && n == 30);
    CHECK(mlxr_burst_classify("Unrecoverable frame 7: lost FEC blocks 1 to 2", &n) == MLXR_BL_UNRECOVERABLE && n == 0);
    CHECK(mlxr_burst_classify("Sending speculative RFI request for predicted loss of frame 9", NULL) == MLXR_BL_RFI_SEND);
    CHECK(mlxr_burst_classify("Sending RFI request for unrecoverable frame 9", NULL) == MLXR_BL_RFI_SEND);
    CHECK(mlxr_burst_classify(REQ, NULL) == MLXR_BL_RFI_SENT);
    CHECK(mlxr_burst_classify(WAIT, NULL) == MLXR_BL_WAIT_RFI);
    CHECK(mlxr_burst_classify("Waiting for IDR frame", NULL) == MLXR_BL_WAIT_IDR);
    CHECK(mlxr_burst_classify("IDR frame request sent", NULL) == MLXR_BL_IDR_SENT);
    CHECK(mlxr_burst_classify("Requesting IDR frame on behalf of DR", NULL) == MLXR_BL_IDR_FOR_DR);
    CHECK(mlxr_burst_classify(PFRAME, NULL) == MLXR_BL_REC_P);
    CHECK(mlxr_burst_classify(IFRAME, NULL) == MLXR_BL_REC_I);
    CHECK(mlxr_burst_classify("Received first audio packet after 300 ms", NULL) == MLXR_BL_NONE);
    CHECK(mlxr_burst_classify(NULL, NULL) == MLXR_BL_NONE);

    MlxrBurst b;
    MlxrBurstSummary s;
    int closed = 0;
    mlxr_burst_init(&b, 1000);
    // outside a burst a request line is written and starts nothing
    CHECK(mlxr_burst_line(&b, "IDR frame request sent", 500, &s, &closed) == 1 && !b.active && !closed);
    CHECK(mlxr_burst_line(&b, "Starting video stream", 500, &s, &closed) == 1);

    // one burst: a loss recovered by RFI, then another loss the host answers with an I-frame
    static const struct {
        uint64_t t;
        const char* line;
        int write;
    } SEQ[] = {
        {1000, "Unrecoverable frame 100: 10+2=12 received < 14 needed", 1}, // opens it; its own throttle
        {1000, "Sending RFI request for unrecoverable frame 100", 1},
        {1001, "Invalidate reference frame request sent (100 to 100)", 1},
        {1011, WAIT, 1},
        {1022, WAIT, 0}, // folded: the same kind again in this burst
        {1022, "Invalidate reference frame request sent (100 to 101)", 0},
        {1030, "Network dropped 3 frames (frames 102 to 104)", 1},
        {1040, "Next post-invalidation frame is: 105 (P-frame)", 1},
        {1200, "Unrecoverable frame 120: lost FEC blocks 3 to 4", 1},
        {1200, "Sending RFI request for unrecoverable frame 120", 0},
        {1400, "IDR frame request sent", 1},
        {1450, WAIT, 0},
        {1500, "Next post-invalidation frame is: 200 (I-frame)", 1},
    };
    for (size_t i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++) {
        if (SEQ[i].t == 1400) {
            mlxr_burst_idr_ask(&b, 1); // the watchdog's ask went through the cap
            mlxr_burst_idr_ask(&b, 0); // and a decoder ask right after was held back
        }
        int w = mlxr_burst_line(&b, SEQ[i].line, SEQ[i].t, &s, &closed);
        if (w != SEQ[i].write) fprintf(stderr, "burst line %zu (%s): write %d\n", i, SEQ[i].line, w);
        CHECK(w == SEQ[i].write && !closed);
    }
    CHECK(b.active);
    CHECK(mlxr_burst_tick(&b, 2449, &s) == 0); // a second without a lost or dropped frame (the last: 1450)
    CHECK(mlxr_burst_tick(&b, 2450, &s) == 1 && !b.active); // closes it
    CHECK(s.startMs == 1000 && s.durMs == 450); // first to last lost or dropped frame
    CHECK(s.netLost == 3 && s.unrecoverable == 2 && s.rfiDrops == 3 && s.idrDrops == 0);
    CHECK(s.rfiRequests == 2 && s.idrSent == 1 && s.idrOurs == 1 && s.idrHeld == 1 && s.idrForDecoder == 0);
    CHECK(s.recovery == MLXR_REC_IDR && !s.stillBroken && s.folded == 4);
    char text[512];
    mlxr_burst_text(&s, "14:03:21.512", 500, text, sizeof(text));
    static const char WANT[] =
        "burst at 14:03:21.512 for 450 ms: frames lost on the network 3 (unrecoverable 2), dropped waiting for RFI 3 / "
        "IDR 0, RFI waits 2, IDR requests sent 1 (asked here 1, held back 1 by rfi_idr_ms 500, for the decoder 0), "
        "recovered by IDR; 4 per-frame lines folded";
    if (strcmp(text, WANT) != 0) fprintf(stderr, "burst text:\n%s\n", text);
    CHECK(strcmp(text, WANT) == 0);

    // our decoder refuses a frame: common-c waits for an IDR frame, which the decoder then gets.
    // The next line long after closes it first, and is written as ever.
    CHECK(mlxr_burst_line(&b, "Requesting IDR frame on behalf of DR", 5000, &s, &closed) == 1 && b.active);
    CHECK(mlxr_burst_line(&b, "Waiting for IDR frame", 5010, &s, &closed) == 1);
    CHECK(mlxr_burst_line(&b, "Waiting for IDR frame", 5020, &s, &closed) == 0);
    mlxr_burst_idr_frame(&b);
    CHECK(mlxr_burst_line(&b, "IDR frame request sent", 7000, &s, &closed) == 1 && closed && !b.active);
    CHECK(s.durMs == 20 && s.idrDrops == 2 && s.idrForDecoder == 1 && s.recovery == MLXR_REC_IDR);

    // frames lost and nothing after: closed still waiting
    CHECK(mlxr_burst_line(&b, "Network dropped 30 frames (frames 300 to 329)", 9000, &s, &closed) == 1);
    CHECK(mlxr_burst_tick(&b, 10000, &s) == 1 && s.stillBroken && s.netLost == 30 && s.durMs == 0);
    mlxr_burst_text(&s, "t", 0, text, sizeof(text));
    CHECK(strstr(text, "not recovered") != NULL && strstr(text, "rfi_idr_ms off") != NULL);
    // an RFI wait whose P-frame came is "recovered by RFI"; one with no recovery line, "neither"
    CHECK(mlxr_burst_line(&b, "Sending RFI request for unrecoverable frame 400", 12000, &s, &closed) == 1);
    CHECK(mlxr_burst_line(&b, PFRAME, 12030, &s, &closed) == 1);
    CHECK(mlxr_burst_tick(&b, 13000, &s) == 1 && s.recovery == MLXR_REC_RFI);
    mlxr_burst_text(&s, "t", 500, text, sizeof(text));
    CHECK(strstr(text, "recovered by RFI") != NULL);
    s.recovery = MLXR_REC_NONE;
    mlxr_burst_text(&s, "t", 500, text, sizeof(text));
    CHECK(strstr(text, "recovered by neither") != NULL);
    CHECK(b.bursts == 4 && b.burstMsMax == 450);
}

int main(void) {
    MlxrRfiWatch w;
    mlxr_rfi_watch_init(&w, 100, 3, 300);

    // A quick recovery as most were: request, 1 frame dropped, P-frame at 22 ms
    CHECK(mlxr_rfi_watch_line(&w, "Sending RFI request for unrecoverable frame 28", 1000) == 0);
    CHECK(mlxr_rfi_watch_line(&w, REQ, 1000) == 0);
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 1011) == 0);
    CHECK(mlxr_rfi_watch_line(&w, PFRAME, 1022) == 0);
    CHECK(w.waits == 1 && w.recovered == 1 && w.idrAsked == 0 && w.waitMsMax == 22);

    // 3 drops inside 45 ms (seen in the run, then a P-frame): no IDR either
    mlxr_rfi_watch_line(&w, REQ, 2000);
    for (int i = 1; i <= 3; i++) CHECK(mlxr_rfi_watch_line(&w, WAIT, 2000 + 15 * (uint64_t)i) == 0);
    mlxr_rfi_watch_line(&w, PFRAME, 2045);
    CHECK(w.idrAsked == 0);

    // A wait that drags on (as the 2.5 s one at 23:46:08): frames flow and
    // get dropped; one IDR request once 100 ms and 3 drops are past, then one
    // every 300 ms, until the I-frame
    mlxr_rfi_watch_line(&w, REQ, 5000);
    int asked = 0;
    for (uint64_t t = 5020; t < 6000; t += 20) asked += mlxr_rfi_watch_line(&w, WAIT, t);
    CHECK(asked == 3); // at 5100, 5400 and 5700
    CHECK(mlxr_rfi_watch_line(&w, IFRAME, 6000) == 0);
    CHECK(w.waits == 3 && w.recovered == 3);

    // An outage: the wait is old but no complete frame came yet (drops < 3):
    // no request into a dead link
    mlxr_rfi_watch_line(&w, REQ, 10000);
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 10400) == 0);
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 10500) == 0);
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 10600) == 1);

    // common-c went over to an IDR wait: the RFI wait closes without counting
    // as recovered, and a later wait starts afresh
    CHECK(mlxr_rfi_watch_line(&w, "Waiting for IDR frame", 10700) == 0);
    CHECK(!w.waiting && w.recovered == 3 && w.closed == 4);
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 20000) == 0); // opens a wait by itself
    CHECK(w.waiting && w.drops == 1);
    // and a wait left open with no line for a second is not carried over
    CHECK(mlxr_rfi_watch_line(&w, WAIT, 22000) == 0);
    CHECK(w.drops == 1 && w.startMs == 22000);

    // Off: never asks
    mlxr_rfi_watch_init(&w, 0, 3, 300);
    mlxr_rfi_watch_line(&w, REQ, 0);
    asked = 0;
    for (uint64_t t = 10; t < 5000; t += 10) asked += mlxr_rfi_watch_line(&w, WAIT, t);
    CHECK(asked == 0);

    test_adaptive();
    test_idr_cap();
    test_bursts();
    return checksDone("stream rfi watchdog");
}
