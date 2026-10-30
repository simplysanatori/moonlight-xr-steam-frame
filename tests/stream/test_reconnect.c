// Moonlight XR Native (lane wave3-session): auto-reconnect (stream/reconnect.h)
// driven by a fake session on a clock the test moves. GPLv3.
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/reconnect.h"

// The fake session: what the app would do, recorded
typedef struct {
    int64_t now;
    MlxrReconnectOutcome script[4]; // what each try comes to
    int scripted;
    int tries, noticed, resumed, gaveUp;
    int noticeAttempt[8], noticeMax[8], noticeN;
    int tryAt[4];                   // the clock when each try began
    int gaveUpCode;
    MlxrReconnectOutcome gaveUpLast;
    int64_t tryTakes;               // how long a try runs on the clock
} Fake;

static void fkScheduled(void* u, int attempt, int max, int code) {
    Fake* f = u;
    f->noticed++;
    (void)code;
    if (f->noticeN < 8) {
        f->noticeAttempt[f->noticeN] = attempt;
        f->noticeMax[f->noticeN] = max;
        f->noticeN++;
    }
}
static MlxrReconnectOutcome fkAttempt(void* u, int attempt) {
    Fake* f = u;
    if (f->tries < 4) f->tryAt[f->tries] = (int)f->now;
    MlxrReconnectOutcome o = f->tries < f->scripted ? f->script[f->tries] : MLXR_RC_FAILED;
    f->tries++;
    (void)attempt;
    f->now += f->tryTakes;
    return o;
}
static void fkResumed(void* u, int attempt) { (void)attempt; ((Fake*)u)->resumed++; }
static void fkGaveUp(void* u, int code, MlxrReconnectOutcome last) {
    Fake* f = u;
    f->gaveUp++;
    f->gaveUpCode = code;
    f->gaveUpLast = last;
}
static int64_t fkNow(void* u) { return ((Fake*)u)->now; }

static MlxrReconnectOps opsFor(Fake* f, int withClock) {
    MlxrReconnectOps ops = { f, fkScheduled, fkAttempt, fkResumed, fkGaveUp, withClock ? fkNow : NULL };
    return ops;
}

// Runs the loop's polls every 100 ms until nothing is scheduled; returns the last event
static MlxrReconnectEvent runOut(MlxrReconnect* r, const MlxrReconnectOps* ops, Fake* f, int64_t limitMs) {
    MlxrReconnectEvent ev = MLXR_RC_EV_NONE, last = MLXR_RC_EV_NONE;
    int64_t end = f->now + limitMs;
    while (mlxr_reconnect_active(r) && f->now < end) {
        ev = mlxr_reconnect_poll(r, ops, f->now);
        if (ev != MLXR_RC_EV_WAITING && ev != MLXR_RC_EV_NONE) last = ev;
        if (ev == MLXR_RC_EV_RESUMED || ev == MLXR_RC_EV_GAVE_UP) break;
        f->now += 100;
    }
    return last;
}

static void testCodes(void) {
    // The ones the plan names, and the control stream's own
    CHECK(mlxr_reconnect_retryable(-100));   // no video traffic
    CHECK(mlxr_reconnect_retryable(-102));   // early termination
    CHECK(mlxr_reconnect_retryable(-101));   // no video frame
    CHECK(mlxr_reconnect_retryable(-1));     // control stream timeout, unannounced close
    CHECK(mlxr_reconnect_retryable(ETIMEDOUT));
    CHECK(mlxr_reconnect_retryable(ECONNRESET));
    CHECK(mlxr_reconnect_retryable(ENETUNREACH));
    CHECK(mlxr_reconnect_retryable(EHOSTUNREACH));
    CHECK(mlxr_reconnect_retryable(ENETDOWN));
    // The host ended it (the user quit the app on the PC, or the app exited), or cannot encode
    CHECK(!mlxr_reconnect_retryable(0));
    CHECK(!mlxr_reconnect_retryable(-103));  // protected content
    CHECK(!mlxr_reconnect_retryable(-104));  // frame conversion
    CHECK(!mlxr_reconnect_retryable(-2));
    CHECK(!mlxr_reconnect_retryable(-1000));
    CHECK(!mlxr_reconnect_retryable(EACCES));
    CHECK(!mlxr_reconnect_retryable(0x0100));
}

static void testNoRetryForOtherCodes(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 0);
    // The host closed the stream: straight to the old way out, nothing scheduled, no notice
    CHECK(mlxr_reconnect_on_drop(&r, &ops, 0, 5000) == MLXR_RC_EV_GAVE_UP);
    CHECK(!mlxr_reconnect_active(&r) && f.noticed == 0 && f.gaveUp == 1 && f.gaveUpCode == 0);
    CHECK(mlxr_reconnect_on_drop(&r, &ops, -103, 6000) == MLXR_RC_EV_GAVE_UP);
    CHECK(f.gaveUp == 2 && f.gaveUpCode == -103 && f.tries == 0);
    CHECK(mlxr_reconnect_poll(&r, &ops, 99000) == MLXR_RC_EV_NONE);
}

static void testBackoffAndSuccess(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.script[0] = MLXR_RC_OK;
    f.scripted = 1;
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 1);
    f.now = 10000;
    CHECK(mlxr_reconnect_on_drop(&r, &ops, -100, f.now) == MLXR_RC_EV_SCHEDULED);
    CHECK(f.noticeN == 1 && f.noticeAttempt[0] == 1 && f.noticeMax[0] == 2);   // "(1/2)"
    CHECK(r.state == MLXR_RC_WAITING && r.dueMs == 12000);
    CHECK(mlxr_reconnect_poll(&r, &ops, 11999) == MLXR_RC_EV_WAITING && f.tries == 0);
    f.now = 12000;
    CHECK(mlxr_reconnect_poll(&r, &ops, f.now) == MLXR_RC_EV_RESUMED);
    CHECK(f.tries == 1 && f.tryAt[0] == 12000 && f.resumed == 1 && f.gaveUp == 0);
    CHECK(!mlxr_reconnect_active(&r));
    CHECK(f.noticeN == 1);   // success is silent: no further notice
}

static void testBackoffSecondTry(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.script[0] = MLXR_RC_FAILED;
    f.script[1] = MLXR_RC_OK;
    f.scripted = 2;
    f.tryTakes = 1500;   // a try takes a moment: the 5 s counts from its end
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 1);
    f.now = 0;
    mlxr_reconnect_on_drop(&r, &ops, -102, 0);
    MlxrReconnectEvent last = runOut(&r, &ops, &f, 60000);
    CHECK(last == MLXR_RC_EV_RESUMED);
    CHECK(f.tries == 2 && f.tryAt[0] == 2000);
    CHECK(f.tryAt[1] == 2000 + 1500 + 5000);
    CHECK(f.noticeN == 2 && f.noticeAttempt[0] == 1 && f.noticeAttempt[1] == 2 && f.noticeMax[1] == 2);   // "(2/2)"
    CHECK(f.resumed == 1 && f.gaveUp == 0);
}

static void testGiveUpAfterTwo(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.scripted = 0;   // every try fails
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 1);
    mlxr_reconnect_on_drop(&r, &ops, ETIMEDOUT, 0);
    MlxrReconnectEvent last = runOut(&r, &ops, &f, 60000);
    CHECK(last == MLXR_RC_EV_GAVE_UP);
    CHECK(f.tries == 2 && f.gaveUp == 1 && f.gaveUpCode == ETIMEDOUT && f.gaveUpLast == MLXR_RC_FAILED);
    CHECK(f.tryAt[0] == 2000 && f.tryAt[1] == 7000);   // 2 s after the drop, 5 s after the first failure
    CHECK(!mlxr_reconnect_active(&r) && f.resumed == 0);
    CHECK(mlxr_reconnect_poll(&r, &ops, 100000) == MLXR_RC_EV_NONE && f.tries == 2);
}

static void testFatalStopsAtOnce(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.script[0] = MLXR_RC_FATAL;   // not paired / the certificate changed / the host's session is gone
    f.scripted = 1;
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 1);
    mlxr_reconnect_on_drop(&r, &ops, -1, 0);
    CHECK(runOut(&r, &ops, &f, 60000) == MLXR_RC_EV_GAVE_UP);
    CHECK(f.tries == 1 && f.gaveUp == 1 && f.gaveUpLast == MLXR_RC_FATAL && f.noticeN == 1);
}

static void testCancel(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 0);
    // The user presses Exit or Disconnect while a try waits: nothing more is tried
    mlxr_reconnect_on_drop(&r, &ops, -100, 0);
    CHECK(mlxr_reconnect_active(&r));
    mlxr_reconnect_cancel(&r);
    CHECK(!mlxr_reconnect_active(&r));
    CHECK(mlxr_reconnect_poll(&r, &ops, 50000) == MLXR_RC_EV_NONE && f.tries == 0 && f.gaveUp == 0);

    // Between the two tries as well
    memset(&f, 0, sizeof(f));
    mlxr_reconnect_init(&r);
    mlxr_reconnect_on_drop(&r, &ops, -100, 0);
    CHECK(mlxr_reconnect_poll(&r, &ops, 2000) == MLXR_RC_EV_SCHEDULED && f.tries == 1);
    CHECK(r.state == MLXR_RC_WAITING && r.attempt == 2);
    mlxr_reconnect_cancel(&r);
    CHECK(mlxr_reconnect_poll(&r, &ops, 60000) == MLXR_RC_EV_NONE && f.tries == 1 && f.gaveUp == 0);

    // A cancelled series is forgotten: the next drop starts from the first try
    CHECK(mlxr_reconnect_drop(&r, -100, 70000) == 1 && r.attempt == 1 && r.dueMs == 72000);
}

static void testFlappingLinkDoesNotLoopForever(void) {
    Fake f;
    memset(&f, 0, sizeof(f));
    f.script[0] = MLXR_RC_OK;
    f.script[1] = MLXR_RC_OK;
    f.script[2] = MLXR_RC_OK;
    f.scripted = 3;
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    MlxrReconnectOps ops = opsFor(&f, 1);
    // Up, drops, comes back, drops again within the stable time: the second drop has only one try left
    f.now = 0;
    mlxr_reconnect_on_drop(&r, &ops, -100, f.now);
    f.now = 2000;
    CHECK(mlxr_reconnect_poll(&r, &ops, f.now) == MLXR_RC_EV_RESUMED);
    f.now = 8000;   // 6 s later it drops again
    CHECK(mlxr_reconnect_on_drop(&r, &ops, -100, f.now) == MLXR_RC_EV_SCHEDULED);
    CHECK(r.attempt == 2 && r.dueMs == 13000);   // try 2 of 2, with its 5 s
    f.now = 13000;
    CHECK(mlxr_reconnect_poll(&r, &ops, f.now) == MLXR_RC_EV_RESUMED);
    f.now = 15000;   // and again: the tries are used
    CHECK(mlxr_reconnect_on_drop(&r, &ops, -100, f.now) == MLXR_RC_EV_GAVE_UP);
    CHECK(f.gaveUp == 1 && !mlxr_reconnect_active(&r));

    // A link that held for the stable time is a recovery: a fresh series of two
    f.now = 15000 + 40000;
    CHECK(mlxr_reconnect_on_drop(&r, &ops, -100, f.now) == MLXR_RC_EV_SCHEDULED && r.attempt == 1);
}

static void testStableThenDrop(void) {
    MlxrReconnect r;
    mlxr_reconnect_init(&r);
    CHECK(mlxr_reconnect_drop(&r, -100, 0) == 1);
    CHECK(mlxr_reconnect_due(&r, 2000) == 1);
    CHECK(mlxr_reconnect_result(&r, MLXR_RC_OK, 2500) == 0);
    CHECK(r.upMs == 2500 && r.state == MLXR_RC_IDLE);
    CHECK(mlxr_reconnect_drop(&r, -100, 2500 + MLXR_RECONNECT_STABLE_MS - 1) == 1 && r.attempt == 2);
    mlxr_reconnect_cancel(&r);
    CHECK(mlxr_reconnect_drop(&r, -100, 2500 + MLXR_RECONNECT_STABLE_MS) == 1 && r.attempt == 1);
    // due() before the time, or when idle, starts nothing
    CHECK(mlxr_reconnect_due(&r, 2500 + MLXR_RECONNECT_STABLE_MS) == 0);
    mlxr_reconnect_cancel(&r);
    CHECK(mlxr_reconnect_due(&r, 999999) == 0);
    CHECK(mlxr_reconnect_result(&r, MLXR_RC_OK, 5) == 0 && r.upMs == 2500);   // a result with no try under way is ignored
}

int main(void) {
    testCodes();
    testNoRetryForOtherCodes();
    testBackoffAndSuccess();
    testBackoffSecondTry();
    testGiveUpAfterTwo();
    testFatalStopsAtOnce();
    testCancel();
    testFlappingLinkDoesNotLoopForever();
    testStableThenDrop();
    return checksDone("stream reconnect");
}
