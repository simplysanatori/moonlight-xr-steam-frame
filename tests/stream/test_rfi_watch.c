// Moonlight XR Native (lane perf): the RFI watchdog (stream/rfi_watch.h) fed
// moonlight-common-c's log lines as the owner's 23:46 run had them. GPLv3.
#include <stdio.h>

#include "../xrcore/check.h"
#include "../../stream/rfi_watch.h"

#define REQ "Invalidate reference frame request sent (28 to 28)"
#define WAIT "Waiting for RFI frame"
#define PFRAME "Next post-invalidation frame is: 18 (P-frame)"
#define IFRAME "Next post-invalidation frame is: 492 (I-frame)"

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
    return checksDone("stream rfi watchdog");
}
