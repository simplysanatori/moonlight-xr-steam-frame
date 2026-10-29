// gamepads: the physical pad core with a fake source and a recording sink:
// arrival / removal / active mask, controller numbers shared with gamepad
// mode's pad, gating (focus and stream), dedup, the disconnect chord, rumble
// routing, motion (gyro / accelerometer) requests and samples, battery,
// touchpad and LED, and a threaded run (also built under ThreadSanitizer).
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "check.h"
#include "Limelight.h"
#include "gamepads.h"

#define MAX_EVENTS 512

typedef struct {
    int kind; // 1 arrival, 2 state
    int number;
    int mask;
    int type;
    unsigned buttons;
    int caps;
    PadOutPacket pk;
} Event;

static Event events[MAX_EVENTS];
static int eventCount;
static pthread_mutex_t evLock = PTHREAD_MUTEX_INITIALIZER;

static int recArrival(void* user, uint8_t n, uint16_t mask, uint8_t type, uint32_t buttons, uint16_t caps) {
    pthread_mutex_lock(&evLock);
    if (eventCount < MAX_EVENTS) {
        Event* e = &events[eventCount++];
        memset(e, 0, sizeof(*e));
        e->kind = 1;
        e->number = n;
        e->mask = mask;
        e->type = type;
        e->buttons = buttons;
        e->caps = caps;
    }
    pthread_mutex_unlock(&evLock);
    return 0;
}

static int recState(void* user, short n, short mask, int flags, unsigned char lt, unsigned char rt,
                    short lx, short ly, short rx, short ry) {
    pthread_mutex_lock(&evLock);
    if (eventCount < MAX_EVENTS) {
        Event* e = &events[eventCount++];
        memset(e, 0, sizeof(*e));
        e->kind = 2;
        e->number = n;
        e->mask = (uint16_t)mask;
        e->pk = (PadOutPacket){ flags, lt, rt, lx, ly, rx, ry };
    }
    pthread_mutex_unlock(&evLock);
    return 0;
}

static struct { int calls; int id; unsigned low, high; int tcalls; unsigned tl, tr; } hap;

static void hapRumble(void* user, int id, uint16_t low, uint16_t high) {
    hap.calls++;
    hap.id = id;
    hap.low = low;
    hap.high = high;
}

static void hapTriggers(void* user, int id, uint16_t l, uint16_t r) {
    hap.tcalls++;
    hap.tl = l;
    hap.tr = r;
}

// What the core asks of the source's sensors and LED, and what it sends beyond state
static struct { int id, type, hz; } sensLog[32];
static int sensCount;
static struct { int calls, id, r, g, b; } led;

static void hapSensor(void* user, int id, int type, int hz) {
    if (sensCount >= 32) return;
    sensLog[sensCount].id = id;
    sensLog[sensCount].type = type;
    sensLog[sensCount].hz = hz;
    sensCount++;
}

static void hapLed(void* user, int id, uint8_t r, uint8_t g, uint8_t b) {
    led.calls++;
    led.id = id;
    led.r = r;
    led.g = g;
    led.b = b;
}

typedef struct {
    int kind; // 1 motion, 2 battery, 3 touch
    int number;
    int type; // motion type, battery state, touch event
    float v[3];
    int pct;
    uint32_t finger;
} Extra;
static Extra extras[256];
static int extraCount;

static void addExtra(Extra e) {
    if (extraCount < 256) extras[extraCount++] = e;
}

static int recMotion(void* user, uint8_t n, uint8_t type, float x, float y, float z) {
    addExtra((Extra){ 1, n, type, { x, y, z }, 0, 0 });
    return 0;
}

static int recBattery(void* user, uint8_t n, uint8_t state, uint8_t pct) {
    addExtra((Extra){ 2, n, state, { 0, 0, 0 }, pct, 0 });
    return 0;
}

static int recTouch(void* user, uint8_t n, uint8_t type, uint32_t finger, float x, float y, float pressure) {
    addExtra((Extra){ 3, n, type, { x, y, pressure }, 0, finger });
    return 0;
}

static int countExtras(int kind) {
    int n = 0;
    for (int i = 0; i < extraCount; i++) n += extras[i].kind == kind;
    return n;
}

static char logLines[16][512];
static int logCount;

static void logHook(void* user, char level, const char* line) {
    if (logCount < 16) snprintf(logLines[logCount++], sizeof(logLines[0]), "%c %s", level, line);
}

static int hooks;
static void onDisconnect(void* user) { hooks++; }

static void reset(Gamepads* g) {
    PadOutSink sink = { recArrival, recState, NULL };
    GpHaptics h = { hapRumble, hapTriggers, hapSensor, hapLed, NULL };
    GpExtraSink ex = { recMotion, recBattery, recTouch, NULL };
    gpInit(g, &sink, &h, logHook, NULL);
    gpSetExtraSink(g, &ex);
    gpSetDisconnectHook(g, onDisconnect, NULL);
    extraCount = 0;
    sensCount = 0;
    memset(&led, 0, sizeof(led));
    eventCount = 0;
    logCount = 0;
    hooks = 0;
    memset(&hap, 0, sizeof(hap));
}

static GpInfo xbox(const char* name, const char* serial) {
    GpInfo i;
    memset(&i, 0, sizeof(i));
    snprintf(i.name, sizeof(i.name), "%s", name);
    snprintf(i.guid, sizeof(i.guid), "030000005e040000ea02000000000000");
    snprintf(i.serial, sizeof(i.serial), "%s", serial);
    snprintf(i.mapping, sizeof(i.mapping), "a:b0,b:b1");
    i.vendor = 0x045e;
    i.product = 0x02ea;
    i.type = LI_CTYPE_XBOX;
    i.buttons = A_FLAG | B_FLAG | PLAY_FLAG;
    i.caps = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE | LI_CCAP_TRIGGER_RUMBLE;
    return i;
}

// A DualSense: everything moonlight-qt looks for (caps from the same rule the SDL source uses)
static GpInfo dualsense(const char* name, const char* serial) {
    GpInfo i = xbox(name, serial);
    GpDeviceFacts f = { 1, 1, 1, 1, 1, 1, 1, 1 };
    i.vendor = 0x054c;
    i.product = 0x0ce6;
    i.type = LI_CTYPE_PS;
    i.caps = gpCapsFor(&f);
    return i;
}

static PadOutPacket pkt(int flags, int lt, int lx) {
    PadOutPacket p;
    memset(&p, 0, sizeof(p));
    p.buttonFlags = flags;
    p.leftTrigger = (unsigned char)lt;
    p.leftStickX = (short)lx;
    return p;
}

static void testArrivalAndState(void) {
    Gamepads g;
    reset(&g);
    GpInfo a = xbox("Pad A", "");
    CHECK(gpAdd(&g, 100, &a) == 0);
    CHECK(eventCount == 0); // no stream, nothing is sent
    PadOutPacket p = pkt(A_FLAG, 0, 0);
    gpState(&g, 100, &p);
    CHECK(eventCount == 0);

    gpStreamStart(&g);
    gpSetActive(&g, 1);
    // Arrival then the state as it stands, with the mask that has the pad
    CHECK(eventCount == 2);
    CHECK(events[0].kind == 1 && events[0].number == 0 && events[0].mask == 1);
    CHECK(events[0].type == LI_CTYPE_XBOX && events[0].buttons == (A_FLAG | B_FLAG | PLAY_FLAG));
    CHECK(events[0].caps == (LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE | LI_CCAP_TRIGGER_RUMBLE));
    CHECK(events[1].kind == 2 && events[1].number == 0 && events[1].mask == 1);
    CHECK(events[1].pk.buttonFlags == A_FLAG);
    CHECK(gpAnnouncedMask(&g) == 1);
    CHECK(logCount >= 1 && strstr(logLines[0], "Pad A") && strstr(logLines[0], "030000005e040000ea02000000000000")
          && strstr(logLines[0], "a:b0,b:b1"));

    // Unchanged is not sent again, a change is, with the values
    gpState(&g, 100, &p);
    CHECK(eventCount == 2);
    p = pkt(A_FLAG | Y_FLAG, 200, -32768);
    gpState(&g, 100, &p);
    CHECK(eventCount == 3);
    CHECK(events[2].pk.buttonFlags == (A_FLAG | Y_FLAG) && events[2].pk.leftTrigger == 200
          && events[2].pk.leftStickX == -32768);

    // Unknown ids are ignored
    gpState(&g, 999, &p);
    CHECK(eventCount == 3);

    // Unplugging: a last packet at rest with the mask that no longer has it
    gpRemove(&g, 100);
    CHECK(eventCount == 4);
    CHECK(events[3].kind == 2 && events[3].number == 0 && events[3].mask == 0);
    CHECK(events[3].pk.buttonFlags == 0 && events[3].pk.leftTrigger == 0 && events[3].pk.leftStickX == 0);
    CHECK(gpAnnouncedMask(&g) == 0);
    gpRemove(&g, 100);
    CHECK(eventCount == 4);
    gpDestroy(&g);
}

static void testNumbersAndMask(void) {
    Gamepads g;
    reset(&g);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    GpInfo a = xbox("A", ""), b = xbox("B", ""), c = xbox("C", "");
    CHECK(gpAdd(&g, 1, &a) == 0);
    CHECK(gpAdd(&g, 2, &b) == 1);
    CHECK(events[0].mask == 1);
    // arrival of the second: mask has both
    int arrivals = 0, lastMask = 0, lastNumber = -1;
    for (int i = 0; i < eventCount; i++)
        if (events[i].kind == 1) { arrivals++; lastMask = events[i].mask; lastNumber = events[i].number; }
    CHECK(arrivals == 2 && lastMask == 3 && lastNumber == 1);
    CHECK(gpAnnouncedMask(&g) == 3);

    // Gamepad mode's pad takes the next free number, and the physical pads keep theirs
    CHECK(gpClaimFramePad(&g) == 2);
    CHECK(gpClaimFramePad(&g) == 2);
    eventCount = 0;
    CHECK(gpAdd(&g, 3, &c) == 3);
    CHECK(events[0].kind == 1 && events[0].number == 3 && events[0].mask == 0xf); // 0,1,2(frame),3
    // Removing pad 0 frees the number for the next arrival; the frame pad's bit stays in the mask
    eventCount = 0;
    gpRemove(&g, 1);
    CHECK(events[0].number == 0 && events[0].mask == 0xe);
    gpReleaseFramePad(&g);
    GpInfo d = xbox("D", "");
    eventCount = 0;
    CHECK(gpAdd(&g, 4, &d) == 0);
    CHECK(events[0].mask == 0xb); // 0,1,3
    // With a physical pad at 0 and 1, the frame pad takes 2
    gpRemove(&g, 3);
    CHECK(gpClaimFramePad(&g) == 2);
    gpReleaseFramePad(&g);
    gpDestroy(&g);

    // Sixteen pads fill the numbers, the 17th is refused
    reset(&g);
    for (int i = 0; i < GP_MAX; i++) {
        GpInfo x = xbox("X", "");
        CHECK(gpAdd(&g, 50 + i, &x) == i);
    }
    GpInfo y = xbox("Y", "");
    CHECK(gpAdd(&g, 99, &y) == -1);
    gpDestroy(&g);
}

static void testDedup(void) {
    Gamepads g;
    reset(&g);
    GpInfo a = xbox("Pad", "AA:BB:CC:00:11:22");
    GpInfo same = xbox("Pad (Steam Input)", "AA:BB:CC:00:11:22");
    GpInfo other = xbox("Pad 2", "AA:BB:CC:00:11:33");
    GpInfo none1 = xbox("Twin", ""), none2 = xbox("Twin", "");
    CHECK(gpAdd(&g, 1, &a) == 0);
    CHECK(gpAdd(&g, 2, &same) == -1); // the same physical pad twice
    CHECK(gpAdd(&g, 3, &other) == 1);
    CHECK(gpAdd(&g, 4, &none1) == 2); // no serial: two identical pads are two pads
    CHECK(gpAdd(&g, 5, &none2) == 3);
    CHECK(gpAdd(&g, 1, &a) == 0); // the same event twice
    int skipped = 0;
    for (int i = 0; i < logCount; i++) skipped += strstr(logLines[i], "same pad") != NULL;
    CHECK(skipped == 1);
    gpDestroy(&g);
}

static void testMirror(void) {
    Gamepads g;
    reset(&g);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    GpInfo real = xbox("Real", "");
    GpInfo virt = xbox("Steam Virtual Gamepad", "");
    virt.mirror = 1;
    virt.vendor = 0x28de;
    virt.product = 0x11ff;
    CHECK(gpAdd(&g, 1, &real) == 0);
    CHECK(gpAdd(&g, 2, &virt) == 0); // its copy: the same pad, no second arrival
    int arrivals = 0;
    for (int i = 0; i < eventCount; i++) arrivals += events[i].kind == 1;
    CHECK(arrivals == 1 && gpAnnouncedMask(&g) == 1);
    int n = eventCount;
    // Idle noise on the copy is not input
    PadOutPacket noise = pkt(0, 0, 0);
    noise.leftStickX = -1;
    noise.leftStickY = 1;
    gpState(&g, 2, &noise);
    CHECK(eventCount == n);
    // Either one carries the input: the real pad while it is in use, else the copy
    PadOutPacket p = pkt(A_FLAG, 0, 0);
    gpState(&g, 2, &p);
    CHECK(eventCount == n + 1 && events[n].number == 0 && events[n].pk.buttonFlags == A_FLAG);
    p = pkt(B_FLAG, 90, 3000);
    gpState(&g, 1, &p); // the real pad takes over, the copy's A is an echo
    CHECK(events[eventCount - 1].pk.buttonFlags == B_FLAG && events[eventCount - 1].pk.leftTrigger == 90
          && events[eventCount - 1].pk.leftStickX == 3000);
    p = pkt(X_FLAG, 200, 30000);
    n = eventCount;
    gpState(&g, 2, &p); // still the real pad's
    CHECK(eventCount == n);
    p = pkt(0, 0, 100); // real pad let go: the copy's state shows
    gpState(&g, 1, &p);
    CHECK(events[eventCount - 1].pk.buttonFlags == X_FLAG && events[eventCount - 1].pk.leftTrigger == 200
          && events[eventCount - 1].pk.leftStickX == 30000);
    p = pkt(0, 0, 0); // both at rest: the real pad's rest
    gpState(&g, 2, &p);
    CHECK(events[eventCount - 1].pk.buttonFlags == 0 && events[eventCount - 1].pk.leftStickX == 100);
    p = pkt(0, 200, 30000);
    gpState(&g, 2, &p); // Steam took the real pad over: only the copy talks
    CHECK(events[eventCount - 1].pk.leftTrigger == 200 && events[eventCount - 1].pk.leftStickX == 30000);
    // Rumble goes to both devices
    hap.calls = 0;
    CHECK(gpRumble(&g, 0, 10, 20) == 1 && hap.calls == 2);
    // The real pad going leaves the copy as the pad; no removal on the host
    n = eventCount;
    gpRemove(&g, 1);
    CHECK(gpAnnouncedMask(&g) == 1);
    CHECK(eventCount == n && gpAnnouncedMask(&g) == 1); // the copy's state was already what the host sees
    n = eventCount;
    gpRemove(&g, 2);
    CHECK(eventCount == n + 1 && events[n].mask == 0 && events[n].pk.buttonFlags == 0);
    gpDestroy(&g);

    // The copy first, the real pad second, same result; a second copy with no real pad is a pad
    reset(&g);
    CHECK(gpAdd(&g, 2, &virt) == 0);
    CHECK(gpAdd(&g, 3, &virt) == 1);
    CHECK(gpAdd(&g, 1, &real) == 0);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    CHECK(gpAnnouncedMask(&g) == 3);
    gpDestroy(&g);
}

static void testGating(void) {
    Gamepads g;
    reset(&g);
    GpInfo a = xbox("A", "");
    gpAdd(&g, 1, &a);
    gpStreamStart(&g);
    CHECK(eventCount == 0); // connected but not focused: the host is not told
    PadOutPacket p = pkt(X_FLAG, 10, 500);
    gpState(&g, 1, &p);
    CHECK(eventCount == 0);
    gpSetActive(&g, 1); // focus: arrival, and the state it holds
    CHECK(eventCount == 2 && events[0].kind == 1 && events[1].pk.buttonFlags == X_FLAG);
    // Focus lost: everything let go on the host, rumble stopped, nothing more sent
    gpSetActive(&g, 0);
    CHECK(eventCount == 3 && events[2].pk.buttonFlags == 0 && events[2].pk.leftStickX == 0);
    CHECK(hap.calls == 1 && hap.low == 0 && hap.high == 0 && hap.tcalls == 1);
    p = pkt(B_FLAG, 0, 0);
    gpState(&g, 1, &p);
    CHECK(eventCount == 3);
    gpSetActive(&g, 0);
    CHECK(eventCount == 3);
    // Focus again: no second arrival, the held state goes
    gpSetActive(&g, 1);
    CHECK(eventCount == 4 && events[3].kind == 2 && events[3].pk.buttonFlags == B_FLAG);
    // A pad that arrives while unfocused is announced at focus
    gpSetActive(&g, 0);
    eventCount = 0;
    GpInfo b = xbox("B", "");
    CHECK(gpAdd(&g, 2, &b) == 1);
    CHECK(eventCount == 0 && gpAnnouncedMask(&g) == 1);
    gpDestroy(&g);
}

static void testGatingLate(void) {
    Gamepads g;
    reset(&g);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    gpSetActive(&g, 0);
    GpInfo b = xbox("B", "");
    CHECK(gpAdd(&g, 2, &b) == 0);
    CHECK(eventCount == 0);
    gpSetActive(&g, 1);
    CHECK(eventCount == 2 && events[0].kind == 1 && events[0].number == 0);
    // The stream ending takes the pad off the host, the next one starts clean
    eventCount = 0;
    gpStreamStop(&g);
    CHECK(eventCount == 1 && events[0].kind == 2 && events[0].mask == 0 && events[0].pk.buttonFlags == 0);
    CHECK(hap.calls >= 1);
    PadOutPacket p = pkt(A_FLAG, 0, 0);
    gpState(&g, 2, &p);
    CHECK(eventCount == 1);
    gpStreamStart(&g);
    CHECK(eventCount == 3 && events[1].kind == 1 && events[2].pk.buttonFlags == A_FLAG);
    gpDestroy(&g);
}

static void testChord(void) {
    Gamepads g;
    reset(&g);
    GpInfo a = xbox("A", "");
    gpAdd(&g, 1, &a);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    eventCount = 0;
    int chord = PLAY_FLAG | BACK_FLAG | LB_FLAG | RB_FLAG;
    PadOutPacket p = pkt(PLAY_FLAG | BACK_FLAG | LB_FLAG, 0, 0);
    gpState(&g, 1, &p);
    CHECK(hooks == 0 && eventCount == 1);
    p = pkt(chord | A_FLAG, 0, 0); // an extra button is not the chord
    gpState(&g, 1, &p);
    CHECK(hooks == 0);
    p = pkt(chord, 0, 0);
    gpState(&g, 1, &p);
    CHECK(hooks == 1);
    // The host saw the pad at rest, and keeps seeing it while the chord is held
    CHECK(events[eventCount - 1].kind == 2 && events[eventCount - 1].pk.buttonFlags == 0);
    int n = eventCount;
    p = pkt(chord, 0, 20000);
    gpState(&g, 1, &p);
    p = pkt(PLAY_FLAG | LB_FLAG, 0, 20000);
    gpState(&g, 1, &p);
    CHECK(hooks == 1 && eventCount == n);
    // Everything let go arms it again
    p = pkt(0, 0, 0);
    gpState(&g, 1, &p);
    p = pkt(chord, 0, 0);
    gpState(&g, 1, &p);
    CHECK(hooks == 2);
    // Off by NO_GAMEPAD_QUIT
    p = pkt(0, 0, 0);
    gpState(&g, 1, &p);
    gpSetChordEnabled(&g, 0);
    p = pkt(chord, 0, 0);
    gpState(&g, 1, &p);
    CHECK(hooks == 2 && events[eventCount - 1].pk.buttonFlags == chord);
    CHECK(gpIsChord(chord) && !gpIsChord(chord | A_FLAG));
    gpDestroy(&g);
}

static void testRumble(void) {
    Gamepads g;
    reset(&g);
    GpInfo a = xbox("A", ""), b = xbox("B", "");
    gpAdd(&g, 70, &a);
    gpAdd(&g, 71, &b);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    CHECK(gpRumble(&g, 1, 1000, 2000) == 1);
    CHECK(hap.calls == 1 && hap.id == 71 && hap.low == 1000 && hap.high == 2000);
    CHECK(gpRumbleTriggers(&g, 0, 300, 400) == 1 && hap.tcalls == 1 && hap.tl == 300 && hap.tr == 400);
    // Not a physical pad's number (gamepad mode's, say): left to gamepad mode
    CHECK(gpClaimFramePad(&g) == 2);
    CHECK(gpRumble(&g, 2, 5, 5) == 0 && hap.calls == 1);
    CHECK(gpRumble(&g, 9, 5, 5) == 0);
    gpRemove(&g, 70);
    CHECK(gpRumble(&g, 0, 5, 5) == 0);
    // Unfocused: rumble does not start, but a stop goes through
    gpSetActive(&g, 0);
    int calls = hap.calls;
    CHECK(gpRumble(&g, 1, 100, 100) == 1 && hap.calls == calls);
    CHECK(gpRumble(&g, 1, 0, 0) == 1 && hap.calls == calls + 1);
    gpDestroy(&g);
}


// ---- motion, battery, touchpad, LED ---------------------------------------

#define MOTION_CAPS (LI_CCAP_ACCEL | LI_CCAP_GYRO)
#define DEG 57.2957795f

static int lastArrivalCaps(int number) {
    for (int i = eventCount - 1; i >= 0; i--)
        if (events[i].kind == 1 && events[i].number == number) return events[i].caps;
    return -1;
}

static void testMotionCaps(void) {
    GpDeviceFacts none;
    memset(&none, 0, sizeof(none));
    CHECK(gpCapsFor(&none) == 0);
    GpDeviceFacts ds = { 1, 1, 1, 1, 1, 1, 1, 1 };
    CHECK(gpCapsFor(&ds) == (LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE | LI_CCAP_TRIGGER_RUMBLE | LI_CCAP_TOUCHPAD
                             | LI_CCAP_ACCEL | LI_CCAP_GYRO | LI_CCAP_BATTERY_STATE | LI_CCAP_RGB_LED));
    GpDeviceFacts xb = { 1, 1, 1, 0, 0, 0, 1, 0 }; // an Xbox pad: no sensors, no touchpad
    CHECK((gpCapsFor(&xb) & (MOTION_CAPS | LI_CCAP_TOUCHPAD)) == 0);
    GpDeviceFacts gyroOnly = { 0, 0, 0, 0, 0, 1, 0, 0 };
    CHECK(gpCapsFor(&gyroOnly) == LI_CCAP_GYRO);

    // Arrival carries what the source said, per pad
    Gamepads g;
    reset(&g);
    GpInfo d = dualsense("DualSense", ""), x = xbox("Xbox", "");
    CHECK(gpAdd(&g, 10, &d) == 0);
    CHECK(gpAdd(&g, 11, &x) == 1);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    CHECK((lastArrivalCaps(0) & MOTION_CAPS) == MOTION_CAPS);
    CHECK((lastArrivalCaps(1) & MOTION_CAPS) == 0);

    // The Frame pad (gamepad mode) never advertises motion: PADOUT_CAPS is its arrival word
    CHECK((PADOUT_CAPS & (MOTION_CAPS | LI_CCAP_TOUCHPAD | LI_CCAP_BATTERY_STATE | LI_CCAP_RGB_LED)) == 0);
    PadOut po;
    PadOutSink sink = { recArrival, recState, NULL };
    padOutInit(&po, &sink);
    padOutSetNumber(&po, gpClaimFramePad(&g), gpAnnouncedMask(&g));
    PadState rest;
    padRest(&rest);
    CHECK(padOutFrame(&po, 1, &rest, 0) == 2);
    CHECK(lastArrivalCaps(2) == PADOUT_CAPS);
    gpDestroy(&g);
}

static void testMotionFlow(void) {
    Gamepads g;
    reset(&g);
    GpInfo d = dualsense("DualSense", ""), x = xbox("Xbox", "");
    gpAdd(&g, 10, &d);
    gpAdd(&g, 11, &x);

    // Nothing is on before the host asks, or before the pad is on the host
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 1, 1, 1, 100);
    CHECK(extraCount == 0 && sensCount == 0);
    CHECK(gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 250) == 1);
    CHECK(sensCount == 0); // asked early: held until the pad is on the host
    gpStreamStart(&g);
    CHECK(gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 250) == 1); // a new stream: the host asks again
    CHECK(sensCount == 0);
    gpSetActive(&g, 1);
    CHECK(sensCount == 1 && sensLog[0].id == 10 && sensLog[0].type == LI_MOTION_TYPE_GYRO && sensLog[0].hz == 250);

    // Samples: gyro rad/s to deg/s on pad 0, throttled to 1000/250 = 4 ms, no repeats
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 1.0f, 2.0f, -0.5f, 1000);
    CHECK(countExtras(1) == 1);
    CHECK(extras[0].number == 0 && extras[0].type == LI_MOTION_TYPE_GYRO);
    CHECK_NEAR(extras[0].v[0], DEG, 1e-3);
    CHECK_NEAR(extras[0].v[1], 2 * DEG, 1e-3);
    CHECK_NEAR(extras[0].v[2], -0.5f * DEG, 1e-3);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 3.0f, 0, 0, 1002); // too soon
    CHECK(countExtras(1) == 1);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 3.0f, 0, 0, 1004);
    CHECK(countExtras(1) == 2);
    CHECK_NEAR(extras[1].v[0], 3 * DEG, 1e-3);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 3.0f, 0, 0, 1020); // same data again
    CHECK(countExtras(1) == 2);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 4.0f, 0, 0, 1021);
    CHECK(countExtras(1) == 3);
    // The accelerometer was not asked for
    gpMotion(&g, 10, LI_MOTION_TYPE_ACCEL, 0, 9.81f, 0, 2000);
    CHECK(countExtras(1) == 3);

    // Accelerometer at 100 Hz: m/s^2 as it is, 10 ms apart, on the same number
    CHECK(gpMotionState(&g, 0, LI_MOTION_TYPE_ACCEL, 100) == 1);
    CHECK(sensCount == 2 && sensLog[1].type == LI_MOTION_TYPE_ACCEL && sensLog[1].hz == 100);
    gpMotion(&g, 10, LI_MOTION_TYPE_ACCEL, 0.1f, 9.81f, 0.2f, 3000);
    CHECK(countExtras(1) == 4 && extras[3].type == LI_MOTION_TYPE_ACCEL && extras[3].number == 0);
    CHECK_NEAR(extras[3].v[1], 9.81f, 1e-4);
    gpMotion(&g, 10, LI_MOTION_TYPE_ACCEL, 0.1f, 9.7f, 0.2f, 3005);
    CHECK(countExtras(1) == 4);
    gpMotion(&g, 10, LI_MOTION_TYPE_ACCEL, 0.1f, 9.7f, 0.2f, 3010);
    CHECK(countExtras(1) == 5);

    // A faster rate than the clock can tell apart, a slow one, and a wrapping clock
    gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 4000); // 1000/4000 = 0: every sample
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 1, 0, 0, 5000);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 2, 0, 0, 5001);
    CHECK(countExtras(1) == 7);
    gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 3); // 333 ms
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 5, 0, 0, 0xFFFFFF00u);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 6, 0, 0, 0xFFFFFFF0u);
    CHECK(countExtras(1) == 8); // 224 ms after the first: not yet
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 6, 0, 0, 0x00000070u); // 368 ms after it, across the wrap
    CHECK(countExtras(1) == 9);

    // Stop: the sensor goes off and samples are dropped
    int before = sensCount;
    CHECK(gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 0) == 1);
    CHECK(sensCount == before + 1 && sensLog[before].type == LI_MOTION_TYPE_GYRO && sensLog[before].hz == 0);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 7, 7, 7, 9000);
    CHECK(countExtras(1) == 9);

    // A pad without sensors ignores the request: no sensor call, no samples
    before = sensCount;
    CHECK(gpMotionState(&g, 1, LI_MOTION_TYPE_GYRO, 250) == 1);
    CHECK(gpMotionState(&g, 1, LI_MOTION_TYPE_ACCEL, 250) == 1);
    CHECK(sensCount == before);
    gpMotion(&g, 11, LI_MOTION_TYPE_GYRO, 1, 1, 1, 9100);
    gpMotion(&g, 11, LI_MOTION_TYPE_ACCEL, 1, 1, 1, 9100);
    CHECK(countExtras(1) == 9);
    // An unknown type is not a request either
    CHECK(gpMotionState(&g, 0, 9, 250) == 1 && sensCount == before);

    // The Frame pad's number (gamepad mode) and numbers nobody holds are not ours
    CHECK(gpClaimFramePad(&g) == 2);
    CHECK(gpMotionState(&g, 2, LI_MOTION_TYPE_GYRO, 250) == 0);
    CHECK(gpMotionState(&g, 9, LI_MOTION_TYPE_GYRO, 250) == 0);
    CHECK(sensCount == before);
    gpReleaseFramePad(&g);
    gpDestroy(&g);
}

static void testMotionGating(void) {
    Gamepads g;
    reset(&g);
    GpInfo d = dualsense("DualSense", "");
    gpAdd(&g, 10, &d);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 250);
    gpMotionState(&g, 0, LI_MOTION_TYPE_ACCEL, 100);
    CHECK(sensCount == 2);

    // Focus lost: both sensors off, samples dropped; focus back: on again at the same rates
    gpSetActive(&g, 0);
    CHECK(sensCount == 4 && sensLog[2].hz == 0 && sensLog[3].hz == 0);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 1, 1, 1, 100);
    CHECK(extraCount == 0);
    gpSetActive(&g, 1);
    CHECK(sensCount == 6);
    int gyroOn = 0, accelOn = 0;
    for (int i = 4; i < 6; i++) {
        gyroOn |= sensLog[i].type == LI_MOTION_TYPE_GYRO && sensLog[i].hz == 250;
        accelOn |= sensLog[i].type == LI_MOTION_TYPE_ACCEL && sensLog[i].hz == 100;
    }
    CHECK(gyroOn && accelOn);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 1, 1, 1, 200); // the first sample after a restart is not throttled
    CHECK(countExtras(1) == 1);

    // The stream ends: off, and the next stream starts with no requests
    gpStreamStop(&g);
    CHECK(sensCount == 8);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 2, 2, 2, 300);
    CHECK(countExtras(1) == 1);
    gpStreamStart(&g);
    gpSetActive(&g, 0);
    gpSetActive(&g, 1);
    CHECK(sensCount == 8);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 3, 3, 3, 400);
    CHECK(countExtras(1) == 1);

    // The pad unplugged while on: its sensor is told to stop
    gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 250);
    CHECK(sensCount == 9 && sensLog[8].hz == 250);
    gpRemove(&g, 10);
    CHECK(sensCount == 10 && sensLog[9].id == 10 && sensLog[9].hz == 0);
    gpMotion(&g, 10, LI_MOTION_TYPE_GYRO, 4, 4, 4, 500);
    CHECK(countExtras(1) == 1);
    gpDestroy(&g);
}

static void testMotionMirror(void) {
    Gamepads g;
    reset(&g);
    GpInfo real = dualsense("DualSense", ""), copy = xbox("Steam Virtual Gamepad", "");
    copy.vendor = 0x28de;
    copy.product = 0x11ff;
    copy.mirror = 1;
    CHECK(gpAdd(&g, 20, &real) == 0);
    CHECK(gpAdd(&g, 21, &copy) == 0); // merged into pad 0
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    CHECK((lastArrivalCaps(0) & MOTION_CAPS) == MOTION_CAPS);
    gpMotionState(&g, 0, LI_MOTION_TYPE_GYRO, 250);
    CHECK(sensCount == 1 && sensLog[0].id == 20); // the real pad's sensor, not the copy's
    gpMotion(&g, 21, LI_MOTION_TYPE_GYRO, 1, 1, 1, 100); // Steam's copy has none; whatever it says is ignored
    CHECK(extraCount == 0);
    gpMotion(&g, 20, LI_MOTION_TYPE_GYRO, 1, 1, 1, 100);
    CHECK(countExtras(1) == 1 && extras[0].number == 0);
    // The real pad going leaves the copy, which has no sensors: nothing more is sent
    gpRemove(&g, 20);
    gpMotion(&g, 21, LI_MOTION_TYPE_GYRO, 2, 2, 2, 200);
    CHECK(countExtras(1) == 1);
    gpDestroy(&g);
}

static void testBatteryTouchLed(void) {
    uint8_t st, pct;
    CHECK(gpBatteryFromPowerLevel(-1, &st, &pct) && st == LI_BATTERY_STATE_UNKNOWN && pct == LI_BATTERY_PERCENTAGE_UNKNOWN);
    CHECK(gpBatteryFromPowerLevel(0, &st, &pct) && st == LI_BATTERY_STATE_DISCHARGING && pct == 5);
    CHECK(gpBatteryFromPowerLevel(1, &st, &pct) && st == LI_BATTERY_STATE_DISCHARGING && pct == 20);
    CHECK(gpBatteryFromPowerLevel(2, &st, &pct) && st == LI_BATTERY_STATE_DISCHARGING && pct == 50);
    CHECK(gpBatteryFromPowerLevel(3, &st, &pct) && st == LI_BATTERY_STATE_DISCHARGING && pct == 90);
    CHECK(gpBatteryFromPowerLevel(4, &st, &pct) && st == LI_BATTERY_STATE_CHARGING && pct == LI_BATTERY_PERCENTAGE_UNKNOWN);
    CHECK(!gpBatteryFromPowerLevel(5, &st, &pct) && !gpBatteryFromPowerLevel(-2, &st, &pct));

    Gamepads g;
    reset(&g);
    GpInfo d = dualsense("DualSense", ""), x = xbox("Xbox", "");
    x.caps = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE; // no battery, touchpad or LED
    gpAdd(&g, 10, &d);
    gpAdd(&g, 11, &x);

    // Battery: known before the stream, told at arrival, then on change only
    gpBattery(&g, 10, LI_BATTERY_STATE_DISCHARGING, 50);
    gpBattery(&g, 11, LI_BATTERY_STATE_DISCHARGING, 50);
    CHECK(countExtras(2) == 0);
    gpStreamStart(&g);
    gpSetActive(&g, 1);
    CHECK(countExtras(2) == 1 && extras[0].number == 0 && extras[0].type == LI_BATTERY_STATE_DISCHARGING && extras[0].pct == 50);
    gpBattery(&g, 10, LI_BATTERY_STATE_DISCHARGING, 50);
    CHECK(countExtras(2) == 1);
    gpBattery(&g, 10, LI_BATTERY_STATE_DISCHARGING, 20);
    CHECK(countExtras(2) == 2 && extras[1].pct == 20);
    gpBattery(&g, 11, LI_BATTERY_STATE_DISCHARGING, 20); // the pad did not advertise it
    CHECK(countExtras(2) == 2);

    // Touchpad: only a pad that advertises one; fingers still down are let go with focus
    extraCount = 0;
    gpTouch(&g, 10, LI_TOUCH_EVENT_DOWN, 0, 0.25f, 0.5f, 1.0f);
    gpTouch(&g, 10, LI_TOUCH_EVENT_MOVE, 0, 0.3f, 0.5f, 1.0f);
    CHECK(countExtras(3) == 2 && extras[0].number == 0 && extras[0].type == LI_TOUCH_EVENT_DOWN && extras[0].finger == 0);
    CHECK_NEAR(extras[0].v[0], 0.25, 1e-6);
    CHECK_NEAR(extras[1].v[0], 0.3, 1e-6);
    gpTouch(&g, 10, LI_TOUCH_EVENT_UP, 0, 0.3f, 0.5f, 0.0f);
    CHECK(countExtras(3) == 3);
    gpTouch(&g, 11, LI_TOUCH_EVENT_DOWN, 0, 0.5f, 0.5f, 1.0f);
    CHECK(countExtras(3) == 3);
    gpTouch(&g, 10, LI_TOUCH_EVENT_DOWN, 1, 0.5f, 0.5f, 1.0f);
    gpSetActive(&g, 0);
    CHECK(countExtras(3) == 5 && extras[extraCount - 1].type == LI_TOUCH_EVENT_CANCEL_ALL && extras[extraCount - 1].number == 0);
    gpTouch(&g, 10, LI_TOUCH_EVENT_MOVE, 1, 0.5f, 0.5f, 1.0f); // unfocused: dropped
    CHECK(countExtras(3) == 5);
    gpSetActive(&g, 1);

    // LED: a pad with one gets the host's colour while it is on the host
    CHECK(gpSetLed(&g, 0, 1, 2, 3) == 1);
    CHECK(led.calls == 1 && led.id == 10 && led.r == 1 && led.g == 2 && led.b == 3);
    CHECK(gpSetLed(&g, 1, 9, 9, 9) == 1 && led.calls == 1); // the Xbox pad has none
    CHECK(gpSetLed(&g, 5, 9, 9, 9) == 0 && led.calls == 1);
    gpSetActive(&g, 0);
    CHECK(gpSetLed(&g, 0, 4, 5, 6) == 1 && led.calls == 1);
    gpDestroy(&g);
}

// The app-wide instance: before init everything is a no-op
static void testGlobal(void) {
    CHECK(!gamepadsReady());
    gamepadsHostMotionState(0, LI_MOTION_TYPE_GYRO, 250); // no instance yet: dropped
    gamepadsHostSetLed(0, 1, 2, 3);
    CHECK(gamepadsClaimFramePad() == 0 && gamepadsAnnouncedMask() == 0);
    gamepadsStreamStart();
    gamepadsSetActive(1);
    gamepadsHostRumble(0, 1, 1); // falls on input/rumble, no crash

    PadOutSink sink = { recArrival, recState, NULL };
    GpHaptics h = { hapRumble, hapTriggers, NULL, NULL, NULL };
    eventCount = 0;
    memset(&hap, 0, sizeof(hap));
    gamepadsInit(&sink, &h, logHook, NULL);
    CHECK(gamepadsReady());
    GpInfo a = xbox("G", "");
    gamepadsStreamStart();
    gamepadsSetActive(1);
    CHECK(gpAdd(gamepadsGlobal(), 5, &a) == 0);
    CHECK(gamepadsAnnouncedMask() == 1);
    CHECK(gamepadsClaimFramePad() == 1);
    gamepadsHostRumble(0, 7, 8);
    CHECK(hap.calls == 1 && hap.low == 7 && hap.high == 8);
    gamepadsHostRumble(1, 7, 8); // gamepad mode's number: not ours
    CHECK(hap.calls == 1);
    gamepadsHostRumbleTriggers(0, 9, 10);
    CHECK(hap.tcalls == 1);
    gamepadsHostMotionState(1, LI_MOTION_TYPE_GYRO, 250); // gamepad mode's number: nothing happens
    gamepadsHostMotionState(0, LI_MOTION_TYPE_GYRO, 250); // the Xbox pad has no gyroscope
    gamepadsHostSetLed(0, 1, 2, 3);
    gamepadsReleaseFramePad();
    gamepadsStreamStop();
    CHECK(gamepadsAnnouncedMask() == 0);
    gpDestroy(gamepadsGlobal());
}

// Threads: the source's events, the connection's rumble callbacks, the frame
// loop's focus changes and gamepad mode's claims all at once
typedef struct { Gamepads* g; atomic_int* stop; int which; } Worker;

static void* worker(void* arg) {
    Worker* w = arg;
    int i = 0;
    while (!atomic_load(w->stop)) {
        i++;
        switch (w->which) {
        case 0: { PadOutPacket p = pkt(i & 1 ? A_FLAG : B_FLAG, i & 255, i); gpState(w->g, 1, &p); break; }
        case 1: gpRumble(w->g, 0, (unsigned short)i, 0); gpRumbleTriggers(w->g, 1, 1, 2); break;
        case 2: gpSetActive(w->g, i & 1); break;
        case 3: gpClaimFramePad(w->g); gpAnnouncedMask(w->g); gpReleaseFramePad(w->g); break;
        case 5:
            gpMotionState(w->g, 0, (unsigned char)(1 + (i & 1)), (unsigned short)(i & 1 ? 250 : 0));
            gpMotion(w->g, 1, 1 + (i & 1), (float)i, 0, 0, (uint32_t)i * 4);
            gpBattery(w->g, 1, 2, (uint8_t)(i & 63));
            gpTouch(w->g, 1, 1 + (i & 1), 0, 0.5f, 0.5f, 1.0f);
            break;
        case 4: {
            GpInfo x = xbox("T", "");
            gpAdd(w->g, 2, &x);
            gpRemove(w->g, 2);
            break;
        }
        }
    }
    return NULL;
}

static void testThreads(void) {
    static Gamepads g;
    PadOutSink sink = { recArrival, recState, NULL };
    GpHaptics h = { hapRumble, hapTriggers, hapSensor, hapLed, NULL };
    gpInit(&g, &sink, &h, NULL, NULL);
    GpInfo a = dualsense("A", "");
    gpAdd(&g, 1, &a);
    gpStreamStart(&g);
    atomic_int stop = 0;
    pthread_t th[6];
    Worker w[6];
    for (int i = 0; i < 6; i++) {
        w[i] = (Worker){ &g, &stop, i };
        pthread_create(&th[i], NULL, worker, &w[i]);
    }
    for (int i = 0; i < 30; i++) {
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    atomic_store(&stop, 1);
    for (int i = 0; i < 6; i++) pthread_join(th[i], NULL);
    gpStreamStop(&g);
    CHECK(gpAnnouncedMask(&g) == 0);
    gpDestroy(&g);
}

// Native (lane artemis-settings): Artemis's dead zone and face button flip
static void testStickOptions(void) {
    PadOutPacket pk;
    memset(&pk, 0, sizeof(pk));
    pk.leftStickX = 1000;                     // inside a 5 % dead zone (1638)
    pk.rightStickX = 32767;
    pk.buttonFlags = A_FLAG | X_FLAG | PLAY_FLAG;
    PadOutPacket o = pk;
    gpApplyStickOptions(&o, 0, 0);
    CHECK(memcmp(&o, &pk, sizeof(o)) == 0);   // off: as read
    gpApplyStickOptions(&o, 5, 0);
    CHECK(o.leftStickX == 0 && o.leftStickY == 0);
    CHECK(o.rightStickX >= 32766);            // full deflection stays full
    o = pk;
    o.leftStickX = 16384;
    gpApplyStickOptions(&o, 10, 0);
    CHECK(o.leftStickX > 14000 && o.leftStickX < 16384);  // rescaled past the dead zone
    o = pk;
    gpApplyStickOptions(&o, -10, 0);          // compensation: a small push starts past 10 %
    CHECK(o.leftStickX > 3276 && o.leftStickX < 4400);
    CHECK(o.leftStickY == 0);
    o = pk;
    o.leftStickX = 0;
    gpApplyStickOptions(&o, -10, 0);
    CHECK(o.leftStickX == 0);                 // rest stays rest
    o = pk;
    gpApplyStickOptions(&o, 0, 1);
    CHECK(o.buttonFlags == (B_FLAG | Y_FLAG | PLAY_FLAG));
    gpApplyStickOptions(&o, 0, 1);
    CHECK(o.buttonFlags == pk.buttonFlags);
    // What the launcher set is readable, for the Frame controllers' dead zone
    CHECK(gamepadsGetDeadzone() == 0);
    gamepadsSetDeadzone(9);
    CHECK(gamepadsGetDeadzone() == 9);
    gamepadsSetDeadzone(99);
    CHECK(gamepadsGetDeadzone() == 20);
    gamepadsSetDeadzone(-99);
    CHECK(gamepadsGetDeadzone() == -20);
    gamepadsSetDeadzone(0);
    CHECK(gamepadsGetDeadzone() == 0);
}

int main(void) {
    testStickOptions();
    testArrivalAndState();
    testNumbersAndMask();
    testDedup();
    testMirror();
    testGating();
    testGatingLate();
    testChord();
    testRumble();
    testMotionCaps();
    testMotionFlow();
    testMotionGating();
    testMotionMirror();
    testBatteryTouchLed();
    testThreads();
    testGlobal();
    return checksDone("input_gamepads");
}
