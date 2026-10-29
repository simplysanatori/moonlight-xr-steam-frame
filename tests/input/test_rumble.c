// rumble: the host's motors to per-hand haptic requests, the handshake, stop
// on zero, and the callback from another thread (also built under TSan).
#include <pthread.h>
#include <string.h>

#include "check.h"
#include "rumble.h"

#define MS 1000000LL

static const int BOTH[2] = { 1, 1 };

static void poll(Rumble* r, const int live[2], int64_t ms, RumbleRequest out[2]) {
    rumblePoll(r, live, ms * MS, out);
}

static void testSplit(void) {
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);

    // Nothing asked: nothing to do
    poll(&r, BOTH, 0, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);

    // Low frequency motor only: the left controller, the right left alone
    rumbleOnHost(&r, 0, 65535, 0);
    poll(&r, BOTH, 1, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY);
    CHECK_NEAR(q[0].amplitude, 1.0, 1e-6);
    CHECK(q[1].action == PAD_RUMBLE_KEEP);
    CHECK(q[0].durationNs == PAD_RUMBLE_PULSE_NS);
    CHECK(q[0].frequency == XR_FREQUENCY_UNSPECIFIED);

    // High frequency motor only: the right controller, and the left stops
    rumbleOnHost(&r, 0, 0, 32768);
    poll(&r, BOTH, 2, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP);
    CHECK(q[1].action == PAD_RUMBLE_APPLY);
    CHECK_NEAR(q[1].amplitude, 32768.0 / 65535.0, 1e-6);

    // Both, different levels
    rumbleOnHost(&r, 0, 16384, 49152);
    poll(&r, BOTH, 3, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    CHECK_NEAR(q[0].amplitude, 16384.0 / 65535.0, 1e-6);
    CHECK_NEAR(q[1].amplitude, 49152.0 / 65535.0, 1e-6);
    rumbleDestroy(&r);
}

static void testPulses(void) {
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);
    rumbleOnHost(&r, 0, 40000, 40000);
    poll(&r, BOTH, 0, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    // A pulse outlasts the frames in between and is armed again before it ends
    int applies = 0;
    for (int ms = 1; ms <= 49; ms++) {
        poll(&r, BOTH, ms, q);
        CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    }
    poll(&r, BOTH, 50, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    CHECK(PAD_RUMBLE_REARM_NS < PAD_RUMBLE_PULSE_NS);
    for (int ms = 51; ms <= 1000; ms++) {
        poll(&r, BOTH, ms, q);
        applies += q[0].action == PAD_RUMBLE_APPLY;
        CHECK(q[0].action != PAD_RUMBLE_STOP);
    }
    // One per 50 ms for the rest of the second
    CHECK(applies == 19);

    // The same values again still arm a pulse at once (a new word)
    poll(&r, BOTH, 1010, q);
    rumbleOnHost(&r, 0, 40000, 40000);
    poll(&r, BOTH, 1011, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    // And a word read is not read twice
    poll(&r, BOTH, 1012, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP);
    rumbleDestroy(&r);
}

static void testStop(void) {
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);

    // The host says zero: a stop, once, then quiet
    rumbleOnHost(&r, 0, 50000, 50000);
    poll(&r, BOTH, 0, q);
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 1, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);
    for (int ms = 2; ms < 300; ms++) {
        poll(&r, BOTH, ms, q);
        CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    }
    // Zero when nothing ran: no stop request
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 400, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);

    // A hand that stops being the pad's (focus lost, controller put down) is
    // stopped though the host never said, and does not restart by itself
    rumbleOnHost(&r, 0, 50000, 50000);
    poll(&r, BOTH, 500, q);
    const int rightOnly[2] = { 0, 1 };
    poll(&r, rightOnly, 501, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP);
    CHECK(q[1].action == PAD_RUMBLE_KEEP);
    poll(&r, rightOnly, 560, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP);
    CHECK(q[1].action == PAD_RUMBLE_APPLY);
    const int neither[2] = { 0, 0 };
    poll(&r, neither, 561, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_STOP);
    // Live again: the host's rumble still stands, so it picks up
    poll(&r, BOTH, 562, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);

    // The stream ending (rumbleStop) stops what the host never ended
    rumbleStop(&r);
    poll(&r, BOTH, 563, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);
    poll(&r, BOTH, 700, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP);

    // The pad coming out: what the host asked is dropped, without a stop of its
    // own, and the next pad starts silent until the host says again
    rumbleOnHost(&r, 0, 50000, 50000);
    poll(&r, BOTH, 800, q);
    rumbleReset(&r);
    poll(&r, BOTH, 801, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    poll(&r, BOTH, 900, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    rumbleOnHost(&r, 0, 50000, 0);
    poll(&r, BOTH, 901, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_KEEP);
    rumbleDestroy(&r);
}

static void testController(void) {
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);
    // Someone else's rumble is not ours
    rumbleOnHost(&r, 1, 65535, 65535);
    poll(&r, BOTH, 0, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    rumbleSetController(&r, 3);
    rumbleOnHost(&r, 0, 65535, 65535);
    poll(&r, BOTH, 1, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP);
    rumbleOnHost(&r, 3, 65535, 0);
    poll(&r, BOTH, 2, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_KEEP);
    // A stop for another pad does not stop this one
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 60, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY);
    rumbleDestroy(&r);
}

static void testVibration(void) {
    RumbleRequest req = { PAD_RUMBLE_APPLY, 0.25f, 0.0f, PAD_RUMBLE_PULSE_NS };
    XrHapticVibration v;
    memset(&v, 0xaa, sizeof(v));
    CHECK(rumbleVibration(&req, &v) == 1);
    CHECK(v.type == XR_TYPE_HAPTIC_VIBRATION);
    CHECK(v.next == NULL);
    CHECK(v.duration == 100000000LL);
    CHECK(v.frequency == XR_FREQUENCY_UNSPECIFIED);
    CHECK_NEAR(v.amplitude, 0.25, 1e-9);
    req.amplitude = 1.7f;
    rumbleVibration(&req, &v);
    CHECK(v.amplitude == 1.0f);
    req.amplitude = -0.5f;
    rumbleVibration(&req, &v);
    CHECK(v.amplitude == 0.0f);
    req.frequency = 160.0f;
    rumbleVibration(&req, &v);
    CHECK(v.frequency == 160.0f);
    // Keep and stop leave the structure alone
    memset(&v, 0x55, sizeof(v));
    req.action = PAD_RUMBLE_KEEP;
    CHECK(rumbleVibration(&req, &v) == 0);
    req.action = PAD_RUMBLE_STOP;
    CHECK(rumbleVibration(&req, &v) == 0);
    CHECK(((unsigned char*)&v)[0] == 0x55);

    // The frequency setting comes out in the requests
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);
    rumbleSetFrequency(&r, 80.0f, 200.0f);
    rumbleOnHost(&r, 0, 65535, 65535);
    poll(&r, BOTH, 0, q);
    CHECK(q[0].frequency == 80.0f && q[1].frequency == 200.0f);
    rumbleDestroy(&r);
}

static void testGlobal(void) {
    Rumble* g = rumbleGlobal();
    CHECK(g == rumbleGlobal());
    RumbleRequest q[2];
    rumbleHostCallback(0, 65535, 0);
    poll(g, BOTH, 0, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_KEEP);
    // Trigger rumble adds to its own side: the right hand starts, the left already runs at full
    rumbleTriggersCallback(0, 0, 65535);
    poll(g, BOTH, 1, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    CHECK_NEAR(q[1].amplitude, 1.0, 1e-6);
    rumbleHostCallback(0, 0, 0);
    rumbleTriggersCallback(0, 0, 0);
    poll(g, BOTH, 2, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);
}

static void testMix(void) {
    Rumble r;
    RumbleRequest q[2];
    rumbleInit(&r);
    // The default is the upstream split at full strength
    rumbleOnHost(&r, 0, 65535, 0);
    poll(&r, BOTH, 0, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_KEEP);
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 1, q);

    // Both hands: one motor is felt on both, at the intensity
    rumbleSetMix(&r, PAD_RUMBLE_MIX_BOTH, 50);
    rumbleOnHost(&r, 0, 0, 65535);
    poll(&r, BOTH, 100, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    CHECK_NEAR(q[0].amplitude, 0.5, 1e-6);
    CHECK_NEAR(q[1].amplitude, 0.5, 1e-6);
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 101, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);

    // Trigger motors, on their own side, from the trigger callback's own word
    rumbleTriggersOnHost(&r, 0, 65535, 0);
    poll(&r, BOTH, 200, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_KEEP);
    CHECK_NEAR(q[0].amplitude, 0.5, 1e-6);
    // Someone else's trigger motors are not ours
    rumbleTriggersOnHost(&r, 2, 0, 65535);
    poll(&r, BOTH, 201, q);
    CHECK(q[1].action == PAD_RUMBLE_KEEP);
    // Intensity 0 is silence
    rumbleTriggersOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, 202, q);
    rumbleSetMix(&r, PAD_RUMBLE_MIX_SPLIT, 0);
    rumbleOnHost(&r, 0, 65535, 65535);
    poll(&r, BOTH, 300, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    // A stream ending stops the trigger motors too
    rumbleSetMix(&r, PAD_RUMBLE_MIX_SPLIT, 100);
    rumbleTriggersOnHost(&r, 0, 65535, 65535);
    poll(&r, BOTH, 400, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    rumbleStop(&r);
    poll(&r, BOTH, 401, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);
    rumbleDestroy(&r);
}

static void testFirst(void) {
    Rumble r;
    RumbleWord w;
    rumbleInit(&r);
    CHECK(rumbleTakeFirst(&r, &w) == 0);
    // Someone else's word, and a host that starts by saying zero, are not the first
    rumbleOnHost(&r, 4, 100, 100);
    rumbleOnHost(&r, 0, 0, 0);
    CHECK(rumbleTakeFirst(&r, &w) == 0);
    // The first one that asks for something, once
    rumbleOnHost(&r, 0, 1000, 2000);
    CHECK(rumbleTakeFirst(&r, &w) == 1);
    CHECK(w.low == 1000 && w.high == 2000 && w.leftTrigger == 0 && w.rightTrigger == 0);
    CHECK(rumbleTakeFirst(&r, &w) == 0);
    rumbleOnHost(&r, 0, 5, 5);
    CHECK(rumbleTakeFirst(&r, &w) == 0);
    // The next stream starts it again, and a trigger word counts
    rumbleStop(&r);
    CHECK(rumbleTakeFirst(&r, &w) == 0);
    rumbleTriggersOnHost(&r, 0, 0, 777);
    CHECK(rumbleTakeFirst(&r, &w) == 1);
    CHECK(w.low == 0 && w.high == 0 && w.leftTrigger == 0 && w.rightTrigger == 777);
    rumbleDestroy(&r);
}

// The callback from other threads while the frame loop polls. Each word the
// writers send has the same level in both motors, and the level is also its
// own check: a torn read would show as different levels in one poll.
typedef struct {
    Rumble* r;
    int id;
    int rounds;
} Writer;

static void* writer(void* arg) {
    Writer* w = arg;
    for (int i = 1; i <= w->rounds; i++) {
        unsigned short v = (unsigned short)(1 + (i * 37 + w->id * 1000) % 65535);
        rumbleOnHost(w->r, 0, v, v);
        if (i % 17 == 0) {
            rumbleOnHost(w->r, 0, 0, 0);
        }
        if (i % 1000 == 0) {
            rumbleStop(w->r);
        }
    }
    return NULL;
}

static void testThreads(void) {
    enum { WRITERS = 3 };
    Rumble r;
    rumbleInit(&r);
    pthread_t th[WRITERS];
    Writer w[WRITERS];
    for (int i = 0; i < WRITERS; i++) {
        w[i].r = &r;
        w[i].id = i;
        w[i].rounds = 20000;
        pthread_create(&th[i], NULL, writer, &w[i]);
    }
    // The frame loop polls meanwhile, 1 ms apart in its own time
    int torn = 0;
    int64_t ms = 0;
    for (int i = 0; i < 20000; i++) {
        RumbleRequest q[2];
        poll(&r, BOTH, ms, q);
        ms += 1;
        if (q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY
                && q[0].amplitude != q[1].amplitude) {
            torn++;
        }
        if (i % 500 == 0) {
            rumbleReset(&r);
        }
    }
    for (int i = 0; i < WRITERS; i++) {
        pthread_join(th[i], NULL);
    }
    CHECK(torn == 0);
    // The last word, whichever it was, then a zero: everything running stops
    RumbleRequest q[2];
    rumbleOnHost(&r, 0, 12345, 12345);
    poll(&r, BOTH, ms + 1, q);
    CHECK(q[0].action == PAD_RUMBLE_APPLY && q[1].action == PAD_RUMBLE_APPLY);
    rumbleOnHost(&r, 0, 0, 0);
    poll(&r, BOTH, ms + 2, q);
    CHECK(q[0].action == PAD_RUMBLE_STOP && q[1].action == PAD_RUMBLE_STOP);
    poll(&r, BOTH, ms + 100, q);
    CHECK(q[0].action == PAD_RUMBLE_KEEP && q[1].action == PAD_RUMBLE_KEEP);
    rumbleDestroy(&r);

    // Through the global callbacks too
    Rumble* g = rumbleGlobal();
    Writer gw[2] = { { g, 0, 20000 }, { g, 1, 20000 } };
    for (int i = 0; i < 2; i++) {
        pthread_create(&th[i], NULL, writer, &gw[i]);
    }
    for (int i = 0; i < 5000; i++) {
        RumbleRequest p[2];
        poll(g, BOTH, ms + 200 + i, p);
    }
    for (int i = 0; i < 2; i++) {
        pthread_join(th[i], NULL);
    }
    rumbleHostCallback(0, 0, 0);
    RumbleRequest p[2];
    poll(g, BOTH, ms + 10000, p);
    poll(g, BOTH, ms + 10100, p);
    CHECK(p[0].action == PAD_RUMBLE_KEEP && p[1].action == PAD_RUMBLE_KEEP);
}

int main(void) {
    testSplit();
    testPulses();
    testStop();
    testController();
    testVibration();
    testMix();
    testFirst();
    testThreads();
    // The global one last, since the threads test also leaves it zeroed
    testGlobal();
    return checksDone("input_rumble");
}
