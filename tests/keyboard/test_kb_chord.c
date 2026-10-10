// Moonlight XR Native, tests/keyboard: the gamepad mode disconnect chord, Start +
// Select + both triggers (xr_padexit.c), through the pad bridge to the host
// (xr_pad_bridge.c): edge triggered, neutral pad first, then the hook. GPLv3.
#include <string.h>

#include "check.h"
#include "xr_pad_bridge.h"
#include "xr_padexit.h"

static PadState pad(int buttons, int lt, int rt) {
    PadState s;
    padRest(&s);
    s.buttons = buttons;
    s.leftTrigger = lt;
    s.rightTrigger = rt;
    return s;
}

// ---- the host and the hook, as one ordered log ----
static char g_log[2048];
static int onState(void* user, short n, short mask, int buttons, unsigned char lt, unsigned char rt,
                   short lx, short ly, short rx, short ry) {
    (void)user; (void)n; (void)mask; (void)lx; (void)ly; (void)rx; (void)ry;
    char b[48];
    snprintf(b, sizeof b, "S%04x/%d/%d ", buttons, lt, rt);
    strncat(g_log, b, sizeof g_log - strlen(g_log) - 1);
    return 0;
}
static int onArrival(void* user, uint8_t n, uint16_t mask, uint8_t type, uint32_t flags, uint16_t caps) {
    (void)user; (void)n; (void)mask; (void)type; (void)flags; (void)caps;
    return 0;
}
static void onHook(void* user) {
    (*(int*)user)++;
    strncat(g_log, "HOOK ", sizeof g_log - strlen(g_log) - 1);
}

static void detector(void) {
    PadExit e;
    padExitReset(&e);
    const int SB = PAD_START | PAD_BACK;
    // Start + Select alone, triggers alone, three of the four: nothing
    PadState s = pad(SB, 0, 0);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(0, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 255, 0);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 0, 255);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(PAD_START, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(PAD_BACK, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(PAD_LB | PAD_RB | PAD_A, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    // Thresholds: 80 percent of the travel (204 of 255)
    s = pad(SB, PADEXIT_TRIGGER_ON - 1, 255);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 255, PADEXIT_TRIGGER_ON - 1);
    CHECK(!padExitUpdate(&e, &s));
    CHECK(PADEXIT_TRIGGER_ON == 204);
    // The chord fires on the frame it completes, once
    s = pad(SB, 200, 230);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 204, 230);
    CHECK(padExitUpdate(&e, &s) == 1);
    CHECK(padExitLatched(&e));
    s = pad(SB, 255, 255);
    for (int i = 0; i < 30; i++) CHECK(padExitUpdate(&e, &s) == 0);
    // Not again until everything is let go: one trigger and a button still down
    s = pad(PAD_START, 255, 0);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    // A trigger wobbling between 50 and 80 percent does not re-arm it
    s = pad(0, 140, 140);
    CHECK(!padExitUpdate(&e, &s));
    s = pad(SB, 255, 255);
    CHECK(!padExitUpdate(&e, &s));
    // All let go, then the chord again fires again
    s = pad(0, 0, 0);
    CHECK(!padExitUpdate(&e, &s));
    CHECK(!padExitLatched(&e));
    s = pad(SB, 255, 255);
    CHECK(padExitUpdate(&e, &s) == 1);
    // Chord with other buttons and the sticks moving still counts
    padExitReset(&e);
    s = pad(SB | PAD_A | PAD_LB, 255, 255);
    s.leftX = 12000;
    CHECK(padExitUpdate(&e, &s) == 1);
}

static void bridge(void) {
    PadOutSink sink = { onArrival, onState, NULL };
    xrPadBridgeStartWithSink(&sink);
    int hooks = 0;
    xrPadBridgeSetDisconnectHook(onHook, &hooks);
    const int SB = PAD_START | PAD_BACK;

    // Ordinary play reaches the host as it is
    g_log[0] = '\0';
    PadState s = pad(PAD_A, 0, 0);
    xrPadBridgeFrame(1, &s, 1000000);
    CHECK(strstr(g_log, "S1000/0/0") != NULL && hooks == 0);
    // Start + Select alone is sent as it is, and does nothing else
    s = pad(SB, 0, 0);
    xrPadBridgeFrame(1, &s, 2000000);
    CHECK(strstr(g_log, "S0030/0/0") != NULL && hooks == 0);
    // The chord: the host sees the pad at rest first, then the hook
    g_log[0] = '\0';
    s = pad(SB | PAD_A, 255, 255);
    xrPadBridgeFrame(1, &s, 3000000);
    CHECK(hooks == 1);
    CHECK(strcmp(g_log, "S0000/0/0 HOOK ") == 0);
    // Still held: rest to the host, no more hooks
    g_log[0] = '\0';
    for (int i = 0; i < 20; i++) xrPadBridgeFrame(1, &s, 4000000 + i * 11000000LL);
    CHECK(hooks == 1);
    CHECK(strstr(g_log, "S0030") == NULL && strstr(g_log, "S1030") == NULL && strstr(g_log, "HOOK") == NULL);
    // Let go, and play goes through again
    s = pad(0, 0, 0);
    xrPadBridgeFrame(1, &s, 300000000);
    s = pad(PAD_B, 0, 0);
    g_log[0] = '\0';
    xrPadBridgeFrame(1, &s, 311000000);
    CHECK(strstr(g_log, "S2000/0/0") != NULL);
    // A second press fires a second time
    s = pad(SB, 255, 255);
    xrPadBridgeFrame(1, &s, 322000000);
    CHECK(hooks == 2);
    // No hook registered: still neutral, and nothing breaks
    s = pad(0, 0, 0);
    xrPadBridgeFrame(1, &s, 333000000);
    xrPadBridgeSetDisconnectHook(NULL, NULL);
    s = pad(SB, 255, 255);
    g_log[0] = '\0';
    xrPadBridgeFrame(1, &s, 344000000);
    CHECK(hooks == 2 && strstr(g_log, "S0030") == NULL);
    // The hook may call back into the bridge (the launcher stops the stream)
    s = pad(0, 0, 0);
    xrPadBridgeFrame(1, &s, 355000000);
    xrPadBridgeSetDisconnectHook(onHook, &hooks);
    s = pad(SB, 255, 255);
    xrPadBridgeFrame(1, &s, 366000000);
    CHECK(hooks == 3);
    xrPadBridgeStop();
    // Stopped: a chord does nothing
    s = pad(0, 0, 0);
    xrPadBridgeFrame(1, &s, 377000000);
    s = pad(SB, 255, 255);
    xrPadBridgeFrame(1, &s, 388000000);
    CHECK(hooks == 3);
}

int main(void) {
    detector();
    bridge();
    return checksDone("kb chord");
}
