// Moonlight XR Native (lane wave3-session): the headset-off and idle timers
// (xr/core/xr_presence.h) on a clock the test moves. GPLv3.
#include "check.h"
#include "xr_presence.h"

#define SEC 1000000000LL
#define MIN (60LL * SEC)

static void testDoffThenDisconnect(void) {
    PresenceState p;
    presenceInit(&p, 60, 0, 1000 * SEC);
    CHECK(p.present == 1);
    CHECK(presenceTick(&p, 1100 * SEC, 1) == PRESENCE_NONE);   // on: nothing, however long

    CHECK(presenceEvent(&p, 0, 1200 * SEC) == PRESENCE_DOFF);
    CHECK(presenceEvent(&p, 0, 1201 * SEC) == PRESENCE_NONE);  // a repeat says nothing
    CHECK(presenceTick(&p, 1200 * SEC, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 1259 * SEC, 1) == PRESENCE_NONE);   // 59 s
    CHECK_NEAR(presenceOffSeconds(&p, 1230 * SEC), 30.0, 1e-9);
    CHECK(presenceTick(&p, 1260 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);   // exactly 60 s
    CHECK(presenceTick(&p, 1261 * SEC, 1) == PRESENCE_NONE);   // said once
    CHECK(presenceTick(&p, 1400 * SEC, 1) == PRESENCE_NONE);

    // It goes back on after the disconnect, then off again: a new count
    CHECK(presenceEvent(&p, 1, 1500 * SEC) == PRESENCE_DON);
    CHECK_NEAR(presenceOffSeconds(&p, 1501 * SEC), 0.0, 1e-9);
    CHECK(presenceEvent(&p, 0, 1600 * SEC) == PRESENCE_DOFF);
    CHECK(presenceTick(&p, 1659 * SEC, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 1660 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);
}

static void testDonCancels(void) {
    PresenceState p;
    presenceInit(&p, 60, 0, 0);
    CHECK(presenceEvent(&p, 0, 100 * SEC) == PRESENCE_DOFF);
    CHECK(presenceTick(&p, 150 * SEC, 1) == PRESENCE_NONE);
    CHECK(presenceEvent(&p, 1, 155 * SEC) == PRESENCE_DON);    // inside the window
    CHECK(presenceEvent(&p, 1, 156 * SEC) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 170 * SEC, 1) == PRESENCE_NONE);    // 70 s after the doff: nothing
    CHECK(presenceTick(&p, 1000 * SEC, 1) == PRESENCE_NONE);

    // The next doff counts from its own start, not the first one
    CHECK(presenceEvent(&p, 0, 1000 * SEC) == PRESENCE_DOFF);
    CHECK(presenceTick(&p, 1059 * SEC, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 1060 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);
}

static void testDoffZeroIsNever(void) {
    PresenceState p;
    presenceInit(&p, 0, 0, 0);
    CHECK(presenceEvent(&p, 0, 10 * SEC) == PRESENCE_DOFF);
    CHECK(presenceTick(&p, 100000 * SEC, 1) == PRESENCE_NONE);
    // Changed while it is off: the new limit applies from the doff
    presenceSetLimits(&p, 30, 0);
    CHECK(presenceTick(&p, 100000 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);
}

static void testNotStreaming(void) {
    PresenceState p;
    presenceInit(&p, 60, 10, 0);
    CHECK(presenceEvent(&p, 0, 5 * SEC) == PRESENCE_DOFF);
    // In the launcher's lists there is no stream to disconnect
    CHECK(presenceTick(&p, 500 * SEC, 0) == PRESENCE_NONE);
    // A stream starting with the headset off counts from its start
    presenceReset(&p, 600 * SEC);
    CHECK(presenceTick(&p, 659 * SEC, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 660 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);
}

static void testIdle(void) {
    PresenceState p;
    presenceInit(&p, 60, 5, 1000 * SEC);                       // 5 minutes
    CHECK(presenceTick(&p, 1000 * SEC + 5 * MIN - 1, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 1000 * SEC + 5 * MIN, 1) == PRESENCE_DISCONNECT_IDLE);
    CHECK(presenceTick(&p, 1000 * SEC + 6 * MIN, 1) == PRESENCE_NONE);   // once

    // Input starts the clock over, and arms it again
    presenceInput(&p, 2000 * SEC);
    CHECK(presenceTick(&p, 2000 * SEC + 4 * MIN, 1) == PRESENCE_NONE);
    presenceInput(&p, 2000 * SEC + 4 * MIN);                   // just before
    CHECK(presenceTick(&p, 2000 * SEC + 8 * MIN, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 2000 * SEC + 9 * MIN, 1) == PRESENCE_DISCONNECT_IDLE);

    // An old input (a late report) does not move the clock back
    presenceInput(&p, 100 * SEC);
    CHECK(p.lastInputNs == 2000 * SEC + 4 * MIN);

    // 0 minutes is off
    presenceSetLimits(&p, 60, 0);
    presenceInput(&p, 3000 * SEC);
    CHECK(presenceTick(&p, 900000 * SEC, 1) == PRESENCE_NONE);
}

static void testDonCountsAsInput(void) {
    PresenceState p;
    presenceInit(&p, 0, 2, 0);                                 // idle 2 min, doff never
    CHECK(presenceEvent(&p, 0, 60 * SEC) == PRESENCE_DOFF);
    CHECK(presenceEvent(&p, 1, 100 * SEC) == PRESENCE_DON);
    CHECK(presenceTick(&p, 100 * SEC + 2 * MIN - 1, 1) == PRESENCE_NONE);
    CHECK(presenceTick(&p, 100 * SEC + 2 * MIN, 1) == PRESENCE_DISCONNECT_IDLE);

    // With the headset off and doff never, the idle clock still runs: sleep when idle
    presenceReset(&p, 1000 * SEC);
    CHECK(presenceEvent(&p, 0, 1001 * SEC) == PRESENCE_DOFF);
    CHECK(presenceTick(&p, 1000 * SEC + 2 * MIN, 1) == PRESENCE_DISCONNECT_IDLE);
}

static void testBoth(void) {
    PresenceState p;
    presenceInit(&p, 60, 1, 0);                                // both at 60 s
    presenceEvent(&p, 0, 0);
    CHECK(presenceTick(&p, 60 * SEC, 1) == PRESENCE_DISCONNECT_DOFF);   // the doff one first
    CHECK(presenceTick(&p, 60 * SEC, 1) == PRESENCE_DISCONNECT_IDLE);
    CHECK(presenceTick(&p, 61 * SEC, 1) == PRESENCE_NONE);
}

static void testLimitsClamp(void) {
    PresenceState p;
    presenceInit(&p, -5, -1, 0);
    CHECK(p.doffSeconds == 0 && p.idleMinutes == 0);
    presenceSetLimits(&p, 999999, 999999);
    CHECK(p.doffSeconds == 3600 && p.idleMinutes == 1440);
}

static void testActivityClock(void) {
    CHECK(presenceActivityNs() == 0);
    int64_t before = presenceNowNs();
    presenceNoteActivity();
    int64_t a = presenceActivityNs();
    CHECK(a >= before && a <= presenceNowNs());
    presenceNoteActivity();
    CHECK(presenceActivityNs() >= a);
}

int main(void) {
    testDoffThenDisconnect();
    testDonCancels();
    testDoffZeroIsNever();
    testNotStreaming();
    testIdle();
    testDonCountsAsInput();
    testBoth();
    testLimitsClamp();
    testActivityClock();
    return checksDone("presence");
}
