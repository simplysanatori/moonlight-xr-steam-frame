// The bounded PCM queue: FIFO order, wrap-around, drop-oldest at the cap
// (latency stays bounded), the fade-in after a splice, and a producer and
// a consumer thread together.
#include "check.h"
#include "pcmq.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static void testFifo(void) {
    MlxrPcmQueue* q = mlxr_pcmq_create(2, 100);
    float in[2 * 40], out[2 * 100];
    for (int i = 0; i < 40; i++) {
        in[2 * i] = (float) i;
        in[2 * i + 1] = (float) -i;
    }
    CHECK(mlxr_pcmq_push(q, in, 40) == 0);
    CHECK(mlxr_pcmq_frames(q) == 40);
    CHECK(mlxr_pcmq_pop(q, out, 15) == 15);
    for (int i = 0; i < 15; i++) {
        CHECK(out[2 * i + 1] == (float) -i);
    }
    // Wrap the ring: 60 more (45 queued -> 85), pop 70, push 60 again
    for (int round = 0; round < 3; round++) {
        float more[2 * 60];
        for (int i = 0; i < 60; i++) {
            more[2 * i] = more[2 * i + 1] = 1000.0f * (float) (round + 1) + (float) i;
        }
        CHECK(mlxr_pcmq_push(q, more, 60) == 0 || round > 0);
        int n = mlxr_pcmq_pop(q, out, 70);
        CHECK(n > 0);
    }
    CHECK(mlxr_pcmq_frames(q) <= 100);
    // Empty pop
    mlxr_pcmq_clear(q);
    CHECK(mlxr_pcmq_pop(q, out, 10) == 0);
    mlxr_pcmq_destroy(q);
}

static void testOrderAcrossWrap(void) {
    MlxrPcmQueue* q = mlxr_pcmq_create(1, 50);
    float in[30], out[30];
    int next = 0, expect = 0, bad = 0;
    for (int round = 0; round < 40; round++) {
        for (int i = 0; i < 30; i++) {
            in[i] = (float) next++;
        }
        CHECK(mlxr_pcmq_push(q, in, 30) == 0);
        int n = mlxr_pcmq_pop(q, out, 30);
        CHECK(n == 30);
        for (int i = 0; i < n; i++) {
            // the first pop after create fades; compare from the second round
            if (round > 0 && out[i] != (float) expect) {
                bad++;
            }
            expect++;
        }
    }
    CHECK(bad == 0);
    mlxr_pcmq_destroy(q);
}

static void testCap(void) {
    MlxrPcmQueue* q = mlxr_pcmq_create(2, 480);  // 10 ms
    float in[2 * 240], out[2 * 480];
    int next = 0;
    int dropped = 0;
    // A consumer that stalls: ten 5 ms packets in 50 ms of nothing popped
    for (int p = 0; p < 10; p++) {
        for (int i = 0; i < 240; i++) {
            in[2 * i] = (float) (next + i + 1);
            in[2 * i + 1] = (float) -(next + i + 1);
        }
        next += 240;
        dropped += mlxr_pcmq_push(q, in, 240);
        CHECK(mlxr_pcmq_frames(q) <= 480);
    }
    CHECK(mlxr_pcmq_frames(q) == 480);
    CHECK(dropped == 2400 - 480);
    MlxrPcmQueueStats st;
    mlxr_pcmq_stats(q, &st);
    CHECK(st.pushedFrames == 2400);
    CHECK(st.droppedFrames == 1920);
    CHECK(st.overruns == 8);  // the first two packets fit
    // What is left is the newest 10 ms, in order, and the splice fades in
    CHECK(mlxr_pcmq_pop(q, out, 480) == 480);
    CHECK(out[2 * 479] == 2400.0f);
    CHECK(out[2 * 0 + 1] != 0.0f);
    float first = out[0];
    float unfaded = (float) (2400 - 480 + 1);
    CHECK_NEAR(first, unfaded * (1.0f / MLXR_PCMQ_FADE_FRAMES), 1e-3);
    CHECK_NEAR(out[2 * (MLXR_PCMQ_FADE_FRAMES + 3)], unfaded + MLXR_PCMQ_FADE_FRAMES + 3, 1e-3);
    // An oversized push keeps only the newest cap frames
    float big[2 * 1000];
    for (int i = 0; i < 1000; i++) {
        big[2 * i] = big[2 * i + 1] = (float) i;
    }
    int d = mlxr_pcmq_push(q, big, 1000);
    CHECK(d == 520);
    CHECK(mlxr_pcmq_frames(q) == 480);
    mlxr_pcmq_pop(q, out, 480);
    CHECK(out[2 * 479] == 999.0f);
    // set_capacity shrinks the bound for later pushes
    mlxr_pcmq_set_capacity(q, 100);
    CHECK(mlxr_pcmq_capacity(q) == 100);
    mlxr_pcmq_push(q, in, 240);
    CHECK(mlxr_pcmq_frames(q) == 100);
    // An underrun mark fades the next pop
    mlxr_pcmq_clear(q);
    for (int i = 0; i < 200; i++) {
        in[2 * i] = in[2 * i + 1] = 1.0f;
    }
    mlxr_pcmq_push(q, in, 100);
    mlxr_pcmq_pop(q, out, 100);
    CHECK_NEAR(out[0], 1.0f / MLXR_PCMQ_FADE_FRAMES, 1e-6);  // clear() marks it
    mlxr_pcmq_push(q, in, 100);
    mlxr_pcmq_pop(q, out, 100);
    CHECK_NEAR(out[0], 1.0f, 1e-6);
    mlxr_pcmq_mark_discontinuity(q);
    mlxr_pcmq_push(q, in, 100);
    mlxr_pcmq_pop(q, out, 100);
    CHECK_NEAR(out[0], 1.0f / MLXR_PCMQ_FADE_FRAMES, 1e-6);
    mlxr_pcmq_destroy(q);
}

#define TOTAL 200000
static void* producer(void* arg) {
    MlxrPcmQueue* q = (MlxrPcmQueue*) arg;
    float buf[240];
    int sent = 0;
    while (sent < TOTAL) {
        int n = 1 + rand() % 240;
        if (sent + n > TOTAL) {
            n = TOTAL - sent;
        }
        for (int i = 0; i < n; i++) {
            buf[i] = (float) (sent + i);
        }
        mlxr_pcmq_push(q, buf, n);
        sent += n;
    }
    return NULL;
}

static void testThreads(void) {
    // Sized so nothing drops: the order must be exact, every frame once
    MlxrPcmQueue* q = mlxr_pcmq_create(1, TOTAL);
    pthread_t t;
    pthread_create(&t, NULL, producer, q);
    float out[64];
    long popped = 0;
    int bad = 0;
    for (;;) {
        int n = mlxr_pcmq_pop(q, out, 64);
        for (int i = 0; i < n; i++) {
            if (out[i] != (float) (popped + i)) {
                bad++;
            }
        }
        popped += n;
        MlxrPcmQueueStats st;
        mlxr_pcmq_stats(q, &st);
        if (st.pushedFrames == TOTAL && mlxr_pcmq_frames(q) == 0) {
            break;
        }
    }
    pthread_join(t, NULL);
    CHECK(bad == 0);
    CHECK(popped == TOTAL);
    MlxrPcmQueueStats st;
    mlxr_pcmq_stats(q, &st);
    CHECK(st.droppedFrames == 0 && st.overruns == 0);
    mlxr_pcmq_destroy(q);

    // And with a small cap and a slow consumer: popped + dropped == pushed
    q = mlxr_pcmq_create(1, 700);
    pthread_create(&t, NULL, producer, q);
    for (;;) {
        mlxr_pcmq_pop(q, out, 3);
        mlxr_pcmq_stats(q, &st);
        if (st.pushedFrames == TOTAL && mlxr_pcmq_frames(q) == 0) {
            break;
        }
    }
    pthread_join(t, NULL);
    mlxr_pcmq_stats(q, &st);
    CHECK(st.poppedFrames + st.droppedFrames == st.pushedFrames);
    CHECK(st.droppedFrames > 0);
    mlxr_pcmq_destroy(q);
}

int main(void) {
    testFifo();
    testOrderAcrossWrap();
    testCap();
    testThreads();
    return checksDone("audio_queue");
}
