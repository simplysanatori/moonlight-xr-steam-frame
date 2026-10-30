// Moonlight XR Native (lane deploy-res-fix): the rate limit on common-c's "Unrecoverable frame"
// log lines (stream/log_throttle.h), driven the way session.c's clLog does: the parsers see every
// line first, then the throttle decides what is written. GPLv3.
#include <stdio.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/log_throttle.h"
#include "../../stream/rfi_watch.h"

#define UNREC "Unrecoverable frame 1234: 6+0=6 received < 71 needed"
#define WAIT "Waiting for RFI frame"
#define REQ "Invalidate reference frame request sent (28 to 28)"

typedef struct {
    MlxrLogThrottle thr;
    MlxrRfiWatch watch;
    uint64_t parsedUnrec, parsedWait; // what a clLog-style parser counts
    int written;                      // lines that reached the log
    char lastWritten[600];
    char firstWritten[600];
} Rig;

static void feed(Rig* r, const char* line, uint64_t nowMs) {
    char out[600];
    // parsers first, as in clLog
    if (strstr(line, "Unrecoverable frame")) r->parsedUnrec++;
    if (strstr(line, WAIT)) r->parsedWait++;
    mlxr_rfi_watch_line(&r->watch, line, nowMs);
    if (mlxr_log_throttle_line(&r->thr, line, nowMs, out, sizeof(out))) {
        if (!r->written) snprintf(r->firstWritten, sizeof(r->firstWritten), "%s", out);
        snprintf(r->lastWritten, sizeof(r->lastWritten), "%s", out);
        r->written++;
    }
}

int main(void) {
    Rig r;
    memset(&r, 0, sizeof(r));
    mlxr_log_throttle_init(&r.thr, 1000);
    mlxr_rfi_watch_init(&r.watch, 100, 3, 300);

    // A burst of 500 lines in one second (2 ms apart), mixed with the lines the parsers act on
    for (int i = 0; i < 500; i++) {
        uint64_t t = 10000 + 2 * (uint64_t)i;
        feed(&r, UNREC, t);
        if (i % 50 == 0) {
            feed(&r, REQ, t);
            feed(&r, WAIT, t);
        }
    }
    CHECK(r.parsedUnrec == 500);          // the parsers saw every line
    CHECK(r.parsedWait == 10);
    CHECK(r.thr.seen == 500);             // so did the throttle's own count
    CHECK(r.watch.waits >= 1 && r.watch.waits <= 10); // rfi_watch still saw its lines
    CHECK(r.watch.waitMsMax == 0);        // (no wait closed in this burst)
    // the log holds the first line of the burst, verbatim, the passing lines, and nothing else
    CHECK(strcmp(r.firstWritten, UNREC) == 0);
    CHECK(r.written == 1 + 10 + 10);      // 1 unrecoverable + 10 REQ + 10 WAIT
    CHECK(r.thr.suppressed == 499);

    // the burst continues past the window: the next line carries the count
    feed(&r, UNREC, 11000);
    CHECK(strstr(r.lastWritten, UNREC " ... (+499 more)") != NULL);
    CHECK(r.thr.suppressed == 0 && r.parsedUnrec == 501);

    // inside the new window: dropped again; a quiet spell leaves a tail the stats tick flushes
    for (int i = 1; i <= 5; i++) feed(&r, UNREC, 11000 + 100 * (uint64_t)i);
    CHECK(r.thr.suppressed == 5);
    CHECK(mlxr_log_throttle_flush(&r.thr, 11600) == 0);   // window still open
    CHECK(mlxr_log_throttle_flush(&r.thr, 12100) == 5);   // closed: report and reset
    CHECK(mlxr_log_throttle_flush(&r.thr, 12200) == 0);
    CHECK(mlxr_log_throttle_window(&r.thr) == 506);       // the stats line's per-window count
    CHECK(mlxr_log_throttle_window(&r.thr) == 0);

    // a later, isolated line is written verbatim (no suffix)
    int before = r.written;
    feed(&r, UNREC, 20000);
    CHECK(r.written == before + 1 && strcmp(r.lastWritten, UNREC) == 0);

    // other lines are never touched
    char out[600];
    CHECK(mlxr_log_throttle_line(&r.thr, "Network dropped 3 frames (frames 4 to 6)", 20001, out, sizeof(out)) == 1);
    CHECK(strcmp(out, "Network dropped 3 frames (frames 4 to 6)") == 0);
    CHECK(mlxr_log_throttle_matches("Unrecoverable frame 9 (block 1 of 2): 1+0=1 received < 4 needed"));
    CHECK(!mlxr_log_throttle_matches("Sending RFI request for unrecoverable frame 28"));

    // windowMs 0 turns it off
    MlxrLogThrottle off;
    mlxr_log_throttle_init(&off, 0);
    for (int i = 0; i < 5; i++) CHECK(mlxr_log_throttle_line(&off, UNREC, 5, out, sizeof(out)) == 1);

    // a tiny output buffer is cut, never overrun
    char small[8];
    MlxrLogThrottle t2;
    mlxr_log_throttle_init(&t2, 1000);
    CHECK(mlxr_log_throttle_line(&t2, UNREC, 1, small, sizeof(small)) == 1);
    CHECK(strlen(small) == 7);

    return checksDone("log_throttle");
}
