// Moonlight XR Native, tests/keyboard: the Full keyboard's modifier state
// machine, auto-repeat and Caps (xr_kbfull.c), and what the host is sent
// through VrKeyboard (stream/vr_keyboard.c) the way xr_session.c hands it
// over: the key slot first, then the modifiers. GPLv3.
#include <Limelight.h>
#include <string.h>

#include "check.h"
#include "../../stream/vr_keyboard.h"
#include "xr_kbfull.h"

#define MS 1000000LL

// ---- the host, as a log of "+vk/modifiers" and "-vk/modifiers" ----
static char g_log[4096];
static void logKey(void* user, short code, char action, char mods) {
    (void)user;
    char b[32];
    snprintf(b, sizeof b, "%s%X/%d ", action == KEY_ACTION_DOWN ? "+" : "-", (unsigned short)code, mods);
    strncat(g_log, b, sizeof g_log - strlen(g_log) - 1);
}
static void logText(void* user, const char* s) {
    (void)user;
    strncat(g_log, "text:", sizeof g_log - strlen(g_log) - 1);
    strncat(g_log, s, sizeof g_log - strlen(g_log) - 1);
}

typedef struct {
    KbFull k;
    VrKeyboard v;
    int64_t now;
    int trigger;     // the trigger is down
    int lastMods;    // IN_KB_MODS as last handed down
} Rig;

static void send(Rig* r, const KbfEvent* ev) {
    if (ev->kind == KBF_EV_DOWN || ev->kind == KBF_EV_UP) {
        vrkb_type(&r->v, KB_CODE_VK + ev->vk,
                  ev->mods | (ev->kind == KBF_EV_DOWN ? KBF_FLAG_DOWN : KBF_FLAG_UP));
    }
    int mods = kbfDownMods(&r->k);
    if (mods != r->lastMods) {
        r->lastMods = mods;
        vrkb_hold(&r->v, mods);
    }
}

static void rigInit(Rig* r) {
    memset(r, 0, sizeof *r);
    kbfInit(&r->k);
    VrKeyboardSink sink = { NULL, logKey, logText };
    vrkb_init(&r->v, sink);
    g_log[0] = '\0';
}

static int key(int vk) { return kbfFindVk(vk); }

static void press(Rig* r, int vk) {
    KbfEvent ev;
    r->trigger = 1;
    kbfPress(&r->k, key(vk), 0, r->now, &ev);
    send(r, &ev);
}
static void release(Rig* r) {
    KbfEvent ev;
    r->trigger = 0;
    kbfRelease(&r->k, r->now, &ev);
    send(r, &ev);
}
static void tick(Rig* r, int64_t advanceNs) {
    KbfEvent ev;
    r->now += advanceNs;
    kbfTick(&r->k, r->now, &ev);
    send(r, &ev);
}
static void tap(Rig* r, int vk) {
    press(r, vk);
    r->now += 60 * MS;
    release(r);
    r->now += 60 * MS;
}
static void hold(Rig* r, int vk, int64_t ns) {
    press(r, vk);
    for (int64_t t = 0; t < ns; t += 11 * MS) tick(r, 11 * MS);
    release(r);
    r->now += 60 * MS;
}
static void leaveAll(Rig* r) {
    KbfEvent ev;
    kbfReleaseAll(&r->k, &ev);
    send(r, &ev);
    vrkb_release_all(&r->v);
}
static int is(const char* want) {
    int ok = strcmp(g_log, want) == 0;
    if (!ok) fprintf(stderr, "got  \"%s\"\nwant \"%s\"\n", g_log, want);
    g_log[0] = '\0';
    return ok;
}

int main(void) {
    Rig r;

    // A plain key: down at the press, up at the let go, with nothing else
    rigInit(&r);
    tap(&r, 0x41);
    CHECK(is("+41/0 -41/0 "));
    CHECK(kbfDownMods(&r.k) == 0);

    // Shift one-shot: lit and down at the tap, the next key goes with it, then it comes up
    rigInit(&r);
    tap(&r, 0xA0);
    CHECK(is("+A0/1 "));
    CHECK(r.k.oneshot == KBF_LSHIFT && r.k.locked == 0);
    tap(&r, 0x41);
    CHECK(is("+41/1 -41/1 -A0/0 "));
    CHECK(kbfDownMods(&r.k) == 0);
    tap(&r, 0x41);
    CHECK(is("+41/0 -41/0 "));

    // Tapping a lit one-shot cancels it
    rigInit(&r);
    tap(&r, 0xA2);
    CHECK(is("+A2/2 "));
    tap(&r, 0xA2);
    CHECK(is("-A2/0 "));
    CHECK(kbfDownMods(&r.k) == 0);

    // A long press locks: stays down over several keys, the lock shows as locked
    rigInit(&r);
    hold(&r, 0xA2, 600 * MS);
    CHECK(is("+A2/2 "));
    CHECK(r.k.locked == KBF_LCTRL && r.k.oneshot == 0);
    tap(&r, 0x43);
    tap(&r, 0x56);
    CHECK(is("+43/2 -43/2 +56/2 -56/2 "));
    CHECK(r.k.locked == KBF_LCTRL);
    // tapped again: unlocked
    tap(&r, 0xA2);
    CHECK(is("-A2/0 "));
    CHECK(kbfDownMods(&r.k) == 0);

    // Just short of the threshold is a tap, just past it a lock; the threshold is configurable
    rigInit(&r);
    hold(&r, 0xA4, 480 * MS);
    CHECK(r.k.oneshot == KBF_LALT && r.k.locked == 0);
    tap(&r, 0xA4);                 // cancel
    kbfSetHoldMs(&r.k, 1000);
    hold(&r, 0xA4, 900 * MS);
    CHECK(r.k.oneshot == KBF_LALT && r.k.locked == 0);
    tap(&r, 0xA4);
    hold(&r, 0xA4, 1100 * MS);
    CHECK(r.k.locked == KBF_LALT);
    leaveAll(&r);
    kbfInit(&r.k);
    kbfSetHoldMs(&r.k, 5);
    CHECK(r.k.holdNs >= 100 * MS);

    // A lit one-shot held long becomes locked, and a locked one held long stays locked
    rigInit(&r);
    tap(&r, 0xA0);
    hold(&r, 0xA0, 700 * MS);
    CHECK(r.k.locked == KBF_LSHIFT && r.k.oneshot == 0);
    hold(&r, 0xA0, 700 * MS);
    CHECK(r.k.locked == KBF_LSHIFT);

    // Ctrl+Alt+Del
    rigInit(&r);
    tap(&r, 0xA2);
    tap(&r, 0xA4);
    tap(&r, 0x2E);
    CHECK(is("+A2/2 +A4/6 +2E/6 -2E/6 -A4/2 -A2/0 "));
    // Win+R
    tap(&r, 0x5B);
    tap(&r, 0x52);
    CHECK(is("+5B/8 +52/8 -52/8 -5B/0 "));
    // Ctrl+Shift+Esc, with the right-hand keys
    tap(&r, 0xA3);
    tap(&r, 0xA1);
    tap(&r, 0x1B);
    CHECK(is("+A3/2 +A1/3 +1B/3 -1B/3 -A3/1 -A1/0 "));
    CHECK(kbfDownMods(&r.k) == 0);
    // Right Alt and Win
    tap(&r, 0xA5);
    tap(&r, 0x5C);
    tap(&r, 0x45);
    CHECK(is("+A5/4 +5C/12 +45/12 -45/12 -5C/4 -A5/0 "));

    // Modifier keys do not use the one-shot up: a second modifier joins the first
    rigInit(&r);
    tap(&r, 0xA0);
    tap(&r, 0xA2);
    CHECK(r.k.oneshot == (KBF_LSHIFT | KBF_LCTRL));
    tap(&r, 0x5A);
    CHECK(is("+A0/1 +A2/3 +5A/3 -5A/3 -A2/1 -A0/0 "));

    // Locked modifiers survive keys; the one-shot ones next to them do not
    rigInit(&r);
    hold(&r, 0xA2, 600 * MS);
    tap(&r, 0xA0);
    tap(&r, 0x41);
    CHECK(is("+A2/2 +A0/3 +41/3 -41/3 -A0/2 "));
    CHECK(r.k.locked == KBF_LCTRL && r.k.oneshot == 0);
    leaveAll(&r);
    CHECK(is("-A2/0 "));

    // Auto-repeat: 0.45 s delay, then about 30 a second; modifiers and Caps do not repeat
    rigInit(&r);
    press(&r, 0x41);
    CHECK(is("+41/0 "));
    for (int i = 0; i < 38; i++) tick(&r, 11 * MS);            // 0.418 s
    CHECK(is(""));
    tick(&r, 11 * MS);                                           // 0.429 s
    tick(&r, 11 * MS);                                           // 0.440 s
    tick(&r, 11 * MS);                                           // 0.451 s: first repeat
    CHECK(is("+41/0 "));
    int downs = 0;
    for (int i = 0; i < 90; i++) {                               // one more second at 90 Hz
        tick(&r, 11111111LL);
        for (const char* p = g_log; (p = strstr(p, "+41")) != NULL; p++) downs++;
        g_log[0] = '\0';
    }
    CHECK(downs >= 28 && downs <= 31);
    release(&r);
    CHECK(is("-41/0 "));
    tick(&r, 500 * MS);
    CHECK(is(""));
    // repeats carry the locked modifiers
    rigInit(&r);
    hold(&r, 0xA2, 600 * MS);
    g_log[0] = '\0';
    press(&r, 0x4A);
    for (int i = 0; i < 60; i++) tick(&r, 11 * MS);
    CHECK(strstr(g_log, "+4A/2 +4A/2 ") != NULL);
    release(&r);
    leaveAll(&r);
    // Caps: toggles on the press, no repeat, the key goes down and up
    rigInit(&r);
    press(&r, 0x14);
    CHECK(r.k.caps == 1);
    for (int i = 0; i < 100; i++) tick(&r, 11 * MS);
    release(&r);
    CHECK(is("+14/0 -14/0 "));
    tap(&r, 0x14);
    CHECK(r.k.caps == 0);
    tap(&r, 0x14);
    CHECK(r.k.caps == 1);
    leaveAll(&r);
    CHECK(r.k.caps == 1);           // the host's, not ours to drop
    g_log[0] = '\0';
    // Held modifier, held long: no repeat event from it
    rigInit(&r);
    press(&r, 0xA2);
    for (int i = 0; i < 60; i++) tick(&r, 11 * MS);
    release(&r);
    CHECK(is("+A2/2 "));
    leaveAll(&r);
    g_log[0] = '\0';

    // Nothing gets stuck: leaving with a key held, a one-shot and a lock
    rigInit(&r);
    hold(&r, 0xA3, 600 * MS);
    tap(&r, 0x5B);
    press(&r, 0x57);
    g_log[0] = '\0';
    leaveAll(&r);
    CHECK(is("-57/10 -A3/8 -5B/0 "));
    CHECK(kbfDownMods(&r.k) == 0 && r.k.held < 0 && r.v.held == 0 && r.v.heldKey == 0);
    // ... and the stream ending while a key is down (VrKeyboard alone)
    rigInit(&r);
    press(&r, 0x57);
    g_log[0] = '\0';
    vrkb_release_all(&r.v);
    CHECK(is("-57/0 "));
    CHECK(r.v.heldKey == 0);
    // a second press while one is held is not taken
    rigInit(&r);
    press(&r, 0x41);
    press(&r, 0x42);
    CHECK(is("+41/0 "));
    release(&r);
    g_log[0] = '\0';

    // The layout key is a request, not a key
    rigInit(&r);
    KbfEvent ev;
    int layoutKey = -1;
    for (int i = 0; i < KBF_KEY_COUNT; i++) if (kbfKey(i)->kind == KBF_LAYOUT) layoutKey = i;
    kbfPress(&r.k, layoutKey, 0, 0, &ev);
    CHECK(ev.kind == KBF_EV_LAYOUT && r.k.held < 0);

    // The old Compact paths are as they were (old tests cover the sequences)
    rigInit(&r);
    vrkb_type(&r.v, 'A', 0);
    CHECK(is("+10/0 +41/1 -41/1 -10/0 "));
    vrkb_type(&r.v, 'c', KB_MOD_CTRL);
    CHECK(is("+A2/2 +43/2 -43/2 "));
    vrkb_release_all(&r.v);
    CHECK(is("-A2/0 "));
    return checksDone("kb rules");
}
