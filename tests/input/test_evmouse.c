// evmouse: a fake evdev source (pipes carrying struct input_event) and a recording
// sink: motion accumulation per packet and the carried remainder, buttons, the
// vertical / horizontal wheel with and without hi-res, gating, grab / ungrab calls,
// SYN_DROPPED, device classification, and the filter that parks the controller's
// absolute pointer after the mouse was used. GPLv3.
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "Limelight.h"
#include "evmouse.h"

#define MAXREC 256
typedef struct { int kind, a, b; } Rec; // 1 move, 2 button, 3 scroll, 4 hscroll
static Rec rec[MAXREC];
static int nrec;
static void add(int k, int a, int b) { if (nrec < MAXREC) rec[nrec++] = (Rec){k, a, b}; }
static void onMove(int dx, int dy, void* u) { (void)u; add(1, dx, dy); }
static void onButton(int b, int down, void* u) { (void)u; add(2, b, down); }
static void onScroll(int a, void* u) { (void)u; add(3, a, 0); }
static void onHScroll(int a, void* u) { (void)u; add(4, a, 0); }

static int grabCalls, ungrabCalls, grabFail;
static int fakeGrab(int fd, int on) {
    (void)fd;
    if (on) grabCalls++;
    else ungrabCalls++;
    return on && grabFail ? -1 : 0;
}

static void put(int fd, int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = (unsigned short)type;
    e.code = (unsigned short)code;
    e.value = value;
    CHECK(write(fd, &e, sizeof(e)) == (ssize_t)sizeof(e));
}
static void syn(int fd) { put(fd, EV_SYN, SYN_REPORT, 0); }
static void pump(void) { while (evmouse_step(5) > 0) {} }

int main(void) {
    static const EvMouseSink sink = {onMove, onButton, onScroll, onHScroll};
    EvMouseCfg cfg = {fakeGrab, 0};

    // classification
    {
        unsigned long ev[2] = {0}, rel[2] = {0}, key[16] = {0};
        CHECK(!evmouse_is_mouse(ev, rel, key));
        ev[0] |= 1ul << EV_REL | 1ul << EV_KEY;
        rel[0] |= 1ul << REL_X | 1ul << REL_Y;
        CHECK(!evmouse_is_mouse(ev, rel, key)); // no BTN_LEFT: a keyboard's or a sensor's REL
        key[BTN_LEFT / (8 * sizeof(long))] |= 1ul << (BTN_LEFT % (8 * sizeof(long)));
        CHECK(evmouse_is_mouse(ev, rel, key));
        rel[0] &= ~(1ul << REL_Y);
        CHECK(!evmouse_is_mouse(ev, rel, key));
        CHECK(evmouse_button(BTN_LEFT) == BUTTON_LEFT && evmouse_button(BTN_RIGHT) == BUTTON_RIGHT);
        CHECK(evmouse_button(BTN_MIDDLE) == BUTTON_MIDDLE);
        CHECK(evmouse_button(BTN_SIDE) == BUTTON_X1 && evmouse_button(BTN_BACK) == BUTTON_X1);
        CHECK(evmouse_button(BTN_EXTRA) == BUTTON_X2 && evmouse_button(BTN_FORWARD) == BUTTON_X2);
        CHECK(evmouse_button(BTN_TOUCH) == 0 && evmouse_button(KEY_A) == 0);
        // Native (lane artemis-settings): --no-mouse-nav-buttons drops back/forward only
        evmouse_set_nav_buttons(0);
        CHECK(evmouse_button(BTN_SIDE) == 0 && evmouse_button(BTN_EXTRA) == 0 && evmouse_button(BTN_FORWARD) == 0);
        CHECK(evmouse_button(BTN_LEFT) == BUTTON_LEFT && evmouse_button(BTN_MIDDLE) == BUTTON_MIDDLE);
        evmouse_set_nav_buttons(1);
        CHECK(evmouse_button(BTN_SIDE) == BUTTON_X1);
    }

    // two mice: 0 with a hi-res wheel, 1 without
    int p0[2], p1[2];
    CHECK(pipe(p0) == 0 && pipe(p1) == 0);
    evmouse_init(&sink, NULL, &cfg);
    CHECK(evmouse_add_fd(p0[0], "fake hi-res mouse", 1) == 0);
    CHECK(evmouse_add_fd(p1[0], "fake clicky mouse", 0) == 1);
    CHECK(evmouse_device_count() == 2);

    // not active: nothing forwarded, nothing grabbed
    put(p0[1], EV_REL, REL_X, 5);
    put(p0[1], EV_REL, REL_Y, 5);
    put(p0[1], EV_KEY, BTN_LEFT, 1);
    syn(p0[1]);
    pump();
    CHECK(nrec == 0 && grabCalls == 0);
    put(p0[1], EV_KEY, BTN_LEFT, 0);
    syn(p0[1]);
    pump();
    CHECK(nrec == 0); // the press never reached the host, so no release either

    // active: grabbed once per device
    evmouse_set_active(1);
    pump();
    CHECK(grabCalls == 2 && evmouse_grabbed(0) == 1 && evmouse_grabbed(1) == 1);
    pump();
    CHECK(grabCalls == 2);

    // motion: one packet is one send, summed
    put(p0[1], EV_REL, REL_X, 3);
    put(p0[1], EV_REL, REL_Y, -2);
    put(p0[1], EV_REL, REL_X, 4);
    syn(p0[1]);
    pump();
    CHECK(nrec == 1 && rec[0].kind == 1 && rec[0].a == 7 && rec[0].b == -2);
    nrec = 0;

    // buttons
    static const int btns[5] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
    static const int want[5] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_MIDDLE, BUTTON_X1, BUTTON_X2};
    for (int i = 0; i < 5; i++) {
        put(p0[1], EV_KEY, btns[i], 1);
        syn(p0[1]);
        put(p0[1], EV_KEY, btns[i], 2); // repeat: ignored
        put(p0[1], EV_KEY, btns[i], 0);
        syn(p0[1]);
    }
    pump();
    CHECK(nrec == 10);
    for (int i = 0; i < 5; i++) {
        CHECK(rec[2 * i].kind == 2 && rec[2 * i].a == want[i] && rec[2 * i].b == 1);
        CHECK(rec[2 * i + 1].kind == 2 && rec[2 * i + 1].a == want[i] && rec[2 * i + 1].b == 0);
    }
    nrec = 0;

    // wheel on the hi-res mouse: REL_WHEEL (legacy) is ignored, the hi-res counts
    put(p0[1], EV_REL, REL_WHEEL, 1);
    put(p0[1], EV_REL, REL_WHEEL_HI_RES, 120);
    syn(p0[1]);
    put(p0[1], EV_REL, REL_WHEEL, -1);
    put(p0[1], EV_REL, REL_WHEEL_HI_RES, -15);
    put(p0[1], EV_REL, REL_WHEEL_HI_RES, -15);
    syn(p0[1]);
    put(p0[1], EV_REL, REL_HWHEEL, 1);
    put(p0[1], EV_REL, REL_HWHEEL_HI_RES, 40);
    syn(p0[1]);
    pump();
    CHECK(nrec == 3);
    CHECK(rec[0].kind == 3 && rec[0].a == 120);
    CHECK(rec[1].kind == 3 && rec[1].a == -30);
    CHECK(rec[2].kind == 4 && rec[2].a == 40);
    nrec = 0;
    // a mouse without hi-res: a notch is 120
    put(p1[1], EV_REL, REL_WHEEL, 2);
    put(p1[1], EV_REL, REL_HWHEEL, -1);
    syn(p1[1]);
    pump();
    CHECK(nrec == 2 && rec[0].kind == 3 && rec[0].a == 240 && rec[1].kind == 4 && rec[1].a == -120);
    nrec = 0;

    // a pending packet that straddles SYN_DROPPED is discarded
    put(p0[1], EV_REL, REL_X, 9);
    put(p0[1], EV_SYN, SYN_DROPPED, 0);
    put(p0[1], EV_REL, REL_X, 8);
    syn(p0[1]);
    put(p0[1], EV_REL, REL_X, 1);
    syn(p0[1]);
    pump();
    CHECK(nrec == 1 && rec[0].kind == 1 && rec[0].a == 1);
    nrec = 0;

    // buttons held when forwarding stops are let go on the host; the ungrab happens
    put(p0[1], EV_KEY, BTN_LEFT, 1);
    put(p0[1], EV_KEY, BTN_SIDE, 1);
    syn(p0[1]);
    pump();
    CHECK(nrec == 2);
    nrec = 0;
    evmouse_set_active(0);
    pump();
    CHECK(ungrabCalls == 2 && evmouse_grabbed(0) == 0);
    CHECK(nrec == 2 && rec[0].kind == 2 && rec[0].b == 0 && rec[1].kind == 2 && rec[1].b == 0);
    CHECK(rec[0].a != rec[1].a);
    nrec = 0;
    put(p0[1], EV_KEY, BTN_LEFT, 0); // the physical release after that: nothing more to send
    put(p0[1], EV_REL, REL_X, 50);
    syn(p0[1]);
    pump();
    CHECK(nrec == 0);

    // active again: grabbed again
    evmouse_set_active(1);
    pump();
    CHECK(grabCalls == 4);
    evmouse_set_active(0);
    pump();

    // a failed grab still forwards, and is not retried every pass
    close(p0[1]); close(p1[1]);
    pump(); // both ends hung up: devices removed
    CHECK(evmouse_device_count() == 0);
    grabFail = 1;
    grabCalls = 0;
    int p2[2];
    CHECK(pipe(p2) == 0);
    evmouse_add_fd(p2[0], "ungrabbable", 0);
    evmouse_set_active(1);
    put(p2[1], EV_REL, REL_X, 2);
    syn(p2[1]);
    pump();
    pump();
    CHECK(grabCalls == 1 && evmouse_grabbed(0) == -1);
    CHECK(nrec == 1 && rec[0].a == 2);
    nrec = 0;
    close(p2[1]);
    evmouse_set_active(0);
    pump();

    // scale with a carried remainder (MLXR_MOUSE_SCALE=0.4): 10 packets of 1 -> 4 pixels
    {
        setenv("MLXR_MOUSE_SCALE", "0.4", 1);
        evmouse_init(&sink, NULL, &cfg);
        unsetenv("MLXR_MOUSE_SCALE");
        int p3[2];
        CHECK(pipe(p3) == 0);
        evmouse_add_fd(p3[0], "scaled", 0);
        evmouse_set_active(1);
        for (int i = 0; i < 10; i++) {
            put(p3[1], EV_REL, REL_X, 1);
            syn(p3[1]);
        }
        pump();
        int sum = 0;
        for (int i = 0; i < nrec; i++) sum += rec[i].a;
        CHECK(sum == 4);
        nrec = 0;
        close(p3[1]); close(p3[0]);
    }

    // ---- the controller pointer yields to the mouse ----
    {
        const int64_t S = 1000000000LL;
        evmouse_filter_config(1, 1000, 0.03f);
        evmouse_filter_reset();
        // never used: passes
        CHECK(evmouse_abs_filter(100, 100, 1920, 1080, 10 * S) == 0);
        CHECK(!evmouse_filter_parked());
        // the mouse moved at t=20 s: dropped while within 1 s
        evmouse_note_use_at(20 * S);
        CHECK(evmouse_abs_filter(101, 100, 1920, 1080, 20 * S + S / 10) == 1);
        CHECK(evmouse_filter_parked());
        CHECK(evmouse_abs_filter(900, 500, 1920, 1080, 20 * S + S / 2) == 1); // even a big move, inside the hold
        // after the hold: the aim must move away from where it was at the last mouse use
        CHECK(evmouse_abs_filter(905, 502, 1920, 1080, 21 * S + S / 10) == 1);
        CHECK(evmouse_abs_filter(930, 502, 1920, 1080, 22 * S) == 1); // 30 px of 1920: under 3%
        CHECK(evmouse_abs_filter(990, 502, 1920, 1080, 22 * S) == 0); // 90 px: resumes
        CHECK(!evmouse_filter_parked());
        CHECK(evmouse_abs_filter(991, 502, 1920, 1080, 22 * S) == 0);
        // the mouse again re-parks
        evmouse_note_use_at(30 * S);
        CHECK(evmouse_abs_filter(991, 503, 1920, 1080, 30 * S + 1000) == 1);
        // no controller update during the hold: the first one afterwards does not resume by itself
        evmouse_note_use_at(40 * S);
        CHECK(evmouse_abs_filter(10, 10, 1920, 1080, 50 * S) == 1);
        CHECK(evmouse_abs_filter(12, 10, 1920, 1080, 50 * S) == 1);
        CHECK(evmouse_abs_filter(400, 10, 1920, 1080, 50 * S) == 0);
        // switched off: always passes
        evmouse_filter_config(0, 1000, 0.03f);
        evmouse_note_use_at(60 * S);
        CHECK(evmouse_abs_filter(5, 5, 1920, 1080, 60 * S) == 0);
    }

    return checksDone("evmouse");
}
