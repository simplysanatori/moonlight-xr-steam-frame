// The adaptive jitter cushion (jitter.h): the policy on its own, the queue's
// crossfaded skip, and a simulation of pulse_out's data path (our queue in
// front of a server that plays 256-frame quanta from a 16 ms buffer) fed by
// a Wi-Fi-like packet trace, old fixed buffer against the cushion.
#include "check.h"
#include "jitter.h"
#include "pcmq.h"
#include "thread_prio.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000

static void testPolicy(void) {
    MlxrJitter j;
    mlxr_jitter_init(&j, 0, 40);
    CHECK(j.targetMs == 0 && j.refilling == 1);

    // Start: the refill wants the server buffer (768 frames) and no cushion
    CHECK(!mlxr_jitter_may_play(&j, 700, 768, RATE));
    CHECK(mlxr_jitter_may_play(&j, 768, 768, RATE));
    CHECK(j.refilling == 0);

    // Regular packets: nothing changes
    int64_t t = 1000;
    for (int i = 0; i < 100; i++, t += 5) {
        CHECK(mlxr_jitter_packet(&j, t, 16.0f) == 0);
    }
    CHECK(j.targetMs == 0);

    // The sink runs dry, the data comes back 30 ms after the last packet:
    // one step (10 ms) at least, and the refill waits for server + cushion
    mlxr_jitter_underrun(&j);
    CHECK(j.refilling == 1);
    t = j.lastPacketMs + 30;
    CHECK(mlxr_jitter_packet(&j, t, 16.0f) == 1);
    CHECK(j.targetMs == 15);  // 30 - 16 = 14, rounded up to 15 (> 10)
    CHECK(j.lateUnderruns == 1 && j.grows == 1);
    CHECK(!mlxr_jitter_may_play(&j, 768 + 700, 768, RATE));
    CHECK(mlxr_jitter_may_play(&j, 768 + 720, 768, RATE));

    // A long stall asks for more than a step; the range caps it
    mlxr_jitter_underrun(&j);
    t += 300;
    mlxr_jitter_packet(&j, t, 16.0f);
    CHECK(j.targetMs == 15);  // an outage no cushion covers: no growth
    CHECK(j.outages == 1);
    mlxr_jitter_underrun(&j);
    t += 70;
    mlxr_jitter_packet(&j, t, 16.0f);
    CHECK(j.targetMs == 40);
    mlxr_jitter_underrun(&j);
    t += 20;
    mlxr_jitter_packet(&j, t, 16.0f);
    CHECK(j.targetMs == 40);
    CHECK(j.lateUnderruns == 4);

    // The host going quiet (a gap past silenceMs) is not jitter
    mlxr_jitter_underrun(&j);
    t += 2000;
    CHECK(mlxr_jitter_packet(&j, t, 16.0f) == 0);
    CHECK(j.targetMs == 40 && j.silenceUnderruns == 1);

    // Calm: 5 ms off per calmMs (30 s) without a late underrun, down to min
    int shrinks = 0;
    for (int i = 0; i < 300 * 200; i++, t += 5) {
        shrinks += mlxr_jitter_packet(&j, t, 16.0f);
    }
    CHECK(shrinks == 8);
    CHECK(j.targetMs == 0);
    CHECK(j.shrinks == 8);

    // The longest gap under silenceMs is kept for the log
    CHECK(j.maxGapMs == 300);

    // A floor: never below min, even calm
    mlxr_jitter_init(&j, 10, 30);
    CHECK(j.targetMs == 10);
    for (int i = 0; i < 20000; i++) {
        mlxr_jitter_packet(&j, i * 5, 16.0f);
    }
    CHECK(j.targetMs == 10);
}

static void testTrim(void) {
    MlxrJitter j;
    mlxr_jitter_init(&j, 0, 40);
    j.targetMs = 20;
    mlxr_jitter_may_play(&j, 100000, 0, RATE);
    // The queue never goes under 35 ms in a second: 15 ms of it is latency
    // nothing needed; trim cuts down to the cushion (20 ms)
    int cut = 0;
    for (int ms = 0; ms <= 1000; ms += 5) {
        int level = (35 + (ms % 10 == 0 ? 5 : 0)) * RATE / 1000;
        cut += mlxr_jitter_trim(&j, ms, level, RATE);
    }
    CHECK(cut == 15 * RATE / 1000);
    // Within cushion + slack (4 ms): left alone
    cut = 0;
    for (int ms = 1005; ms <= 2100; ms += 5) {
        cut += mlxr_jitter_trim(&j, ms, 23 * RATE / 1000, RATE);
    }
    CHECK(cut == 0);
    // Nothing during a refill
    mlxr_jitter_underrun(&j);
    CHECK(mlxr_jitter_trim(&j, 5000, 100000, RATE) == 0);

    // The cap leaves room for a refill (server + cushion) and a little over
    j.targetMs = 40;
    int cap = mlxr_jitter_cap_frames(&j, 768, 480, RATE);
    CHECK(cap >= 768 + 40 * RATE / 1000);
    CHECK(cap <= 768 + 40 * RATE / 1000 + 480);
}

static void testSkip(void) {
    // A 1 kHz sine; skipping 7 ms of it must not leave a step at the splice
    MlxrPcmQueue* q = mlxr_pcmq_create(1, 4800);
    float in[4800], out[4800];
    for (int i = 0; i < 4800; i++) {
        in[i] = sinf(2.0f * (float) M_PI * 1000.0f * (float) i / RATE);
    }
    mlxr_pcmq_push(q, in, 2400);
    float first[10];
    mlxr_pcmq_pop(q, first, 10);  // the first pop after create fades in; skip it
    CHECK(mlxr_pcmq_skip(q, 336) == 336);
    int n = mlxr_pcmq_pop(q, out, 4800);
    CHECK(n == 2400 - 10 - 336);
    float worst = 0.0f;
    // A 1 kHz full-scale sine moves at most 2*pi*1000/48000 = 0.13 a sample
    float prev = first[9];
    for (int i = 0; i < 200; i++) {
        float d = fabsf(out[i] - prev);
        if (d > worst) {
            worst = d;
        }
        prev = out[i];
    }
    CHECK(worst < 0.2f);
    // After the fade the samples are the originals, 336 further on
    CHECK(fabsf(out[300] - in[10 + 336 + 300]) < 1e-6f);
    MlxrPcmQueueStats st;
    mlxr_pcmq_stats(q, &st);
    CHECK(st.skippedFrames == 336);
    // Never more than leaves the fade's worth
    mlxr_pcmq_push(q, in, 100);
    CHECK(mlxr_pcmq_skip(q, 1000) == 0);  // 100 frames hold no 5 ms crossfade
    mlxr_pcmq_push(q, in, 400);
    CHECK(mlxr_pcmq_skip(q, 1000) == 500 - MLXR_PCMQ_XFADE_FRAMES);
    mlxr_pcmq_destroy(q);
}

// ---- simulation of pulse_out's path ---------------------------------------------

#define PACKET 240        // 5 ms
#define QUANTUM 256       // PipeWire's at 48 kHz on the Frame
#define TLENGTH 768       // 16 ms, what the server grants for 20
#define PREBUF 384
#define QUANTUM_US 5333

typedef struct {
    int adaptive;
    MlxrPcmQueue* q;
    MlxrJitter j;
    int server;           // frames in the server buffer
    int playing;
    uint32_t underruns;
    double latSum, latMax;  // queue + server, ms, sampled each quantum
    long latN;
    float scratch[TLENGTH];
} Sim;

static void simPump(Sim* s) {
    if (s->adaptive && !mlxr_jitter_may_play(&s->j, mlxr_pcmq_frames(s->q), TLENGTH, RATE)) {
        return;
    }
    int want = TLENGTH - s->server;
    if (want > 0) {
        s->server += mlxr_pcmq_pop(s->q, s->scratch, want);
    }
    if (!s->playing && s->server >= PREBUF) {
        s->playing = 1;
    }
}

static void simWrite(Sim* s, int64_t nowMs, const float* pcm) {
    mlxr_pcmq_push(s->q, pcm, PACKET);
    if (!s->adaptive) {
        simPump(s);
        return;
    }
    if (mlxr_jitter_packet(&s->j, nowMs, 16.0f)) {
        mlxr_pcmq_set_capacity(s->q, mlxr_jitter_cap_frames(&s->j, TLENGTH, 480, RATE));
    }
    simPump(s);
    int cut = mlxr_jitter_trim(&s->j, nowMs, mlxr_pcmq_frames(s->q), RATE);
    if (cut > 0) {
        mlxr_pcmq_skip(s->q, cut);
    }
}

static void simQuantum(Sim* s, int measure) {
    if (s->playing) {
        if (s->server >= QUANTUM) {
            s->server -= QUANTUM;
        }
        else {
            s->server = 0;
            s->playing = 0;
            s->underruns++;
            if (s->adaptive) {
                mlxr_jitter_underrun(&s->j);
            }
        }
    }
    simPump(s);  // the server's write callback
    if (measure) {
        double ms = 1000.0 * (mlxr_pcmq_frames(s->q) + s->server) / RATE;
        s->latSum += ms;
        if (ms > s->latMax) {
            s->latMax = ms;
        }
        s->latN++;
    }
}

// Packets every 5 ms of host time. A stall holds every packet due in it and
// hands them over together at its end (a Wi-Fi burst). stall(t) returns the end
// of the stall covering t (in us), or 0.
typedef int64_t (*StallFn)(int64_t us);

// quiet: packets due in a stall are never sent (the host had nothing to play)
static void simulate(Sim* s, int adaptive, int64_t fromUs, int64_t toUs, StallFn stall, int quiet,
                     int64_t measureFromUs) {
    static float pcm[PACKET];
    s->adaptive = adaptive;
    int64_t nextPacket = fromUs, nextQuantum = fromUs + 20000;
    int64_t heldUntil = 0;
    while (nextPacket < toUs || nextQuantum < toUs) {
        int64_t deliver = nextPacket;
        int64_t end = stall != NULL ? stall(nextPacket) : 0;
        if (quiet && end > 0) {
            if (nextPacket <= nextQuantum) {
                nextPacket += 5000;
            }
            else {
                simQuantum(s, nextQuantum >= measureFromUs);
                nextQuantum += QUANTUM_US;
            }
            continue;
        }
        if (end > deliver) {
            heldUntil = end;
        }
        if (heldUntil > deliver) {
            deliver = heldUntil;
        }
        if (deliver <= nextQuantum) {
            simWrite(s, deliver / 1000, pcm);
            nextPacket += 5000;
        }
        else {
            simQuantum(s, nextQuantum >= measureFromUs);
            nextQuantum += QUANTUM_US;
        }
    }
}

static void simInit(Sim* s, int adaptive) {
    memset(s, 0, sizeof *s);
    MlxrJitter big;
    mlxr_jitter_init(&s->j, 0, 40);
    big = s->j;
    big.targetMs = 40;
    s->q = mlxr_pcmq_create(1, mlxr_jitter_cap_frames(&big, 2 * TLENGTH, 480, RATE));
    // The old path: our queue capped at 10 ms, no hold, no trim
    mlxr_pcmq_set_capacity(s->q, adaptive ? mlxr_jitter_cap_frames(&s->j, TLENGTH, 480, RATE) : 480);
}

// Like the owner's run at 22:34-22:35: every 700 ms the link holds packets for
// 25-60 ms (moonlight-common-c's FEC wait alone is up to 30 ms)
static int64_t wifiStalls(int64_t us) {
    int64_t period = 700000;
    int64_t k = us / period;
    int64_t start = k * period + 100000;
    int64_t len = 25000 + (k * 7919 % 36) * 1000;
    return us >= start && us < start + len ? start + len : 0;
}

// The host stops sending for 2 s every 10 s (nothing playing on the PC)
static int64_t silences(int64_t us) {
    int64_t k = us / 10000000;
    int64_t start = k * 10000000 + 5000000;
    return us >= start && us < start + 2000000 ? start + 2000000 : 0;
}

static void testSimulation(void) {
    Sim old, cushion;
    simInit(&old, 0);
    simInit(&cushion, 1);
    // A minute of a bad link
    simulate(&old, 0, 0, 60000000, wifiStalls, 0, 0);
    simulate(&cushion, 1, 0, 60000000, wifiStalls, 0, 0);
    printf("bad link, 60 s: fixed buffer %u underruns (latency avg %.1f max %.1f ms), "
           "cushion %u underruns (target %d ms, latency avg %.1f max %.1f ms)\n",
           old.underruns, old.latSum / old.latN, old.latMax, cushion.underruns, cushion.j.targetMs,
           cushion.latSum / cushion.latN, cushion.latMax);
    CHECK(old.underruns >= 50);           // nearly every stall crackles today
    CHECK(cushion.underruns <= 5);        // a few while the cushion grows
    CHECK(cushion.j.targetMs <= 40);
    CHECK(cushion.latMax <= 16.0 + 40.0 + 16.0 + 6.0);  // inside stock moonlight-qt's ~65-95 ms

    // Then five clean minutes: the cushion and the latency come back down
    Sim* c = &cushion;
    c->latSum = c->latMax = 0.0;
    c->latN = 0;
    uint32_t before = c->underruns;
    simulate(c, 1, 60000000, 360000000, NULL, 0, 300000000);
    printf("then clean, 5 min: %u underruns, target %d ms, latency avg %.1f max %.1f ms\n",
           c->underruns - before, c->j.targetMs, c->latSum / c->latN, c->latMax);
    CHECK(c->underruns == before);
    CHECK(c->j.targetMs == 0);
    CHECK(c->latMax <= 16.0 + 10.0 + 1.0);

    // The host going quiet now and then is not a reason to grow
    Sim q;
    simInit(&q, 1);
    simulate(&q, 1, 0, 60000000, silences, 1, 0);
    CHECK(q.j.targetMs == 0);
    CHECK(q.j.lateUnderruns == 0 && q.j.silenceUnderruns >= 5);

    mlxr_pcmq_destroy(old.q);
    mlxr_pcmq_destroy(cushion.q);
    mlxr_pcmq_destroy(q.q);
}

static void testThreadInfo(void) {
    CHECK(mlxr_prio_mode_parse(NULL, MLXR_PRIO_NICE) == MLXR_PRIO_NICE);
    CHECK(mlxr_prio_mode_parse("rt", MLXR_PRIO_NICE) == MLXR_PRIO_REALTIME);
    CHECK(mlxr_prio_mode_parse("off", MLXR_PRIO_NICE) == MLXR_PRIO_OFF);
    CHECK(mlxr_prio_mode_parse("bogus", MLXR_PRIO_OFF) == MLXR_PRIO_OFF);
    char said[200] = "";
    CHECK(mlxr_thread_raise_priority(MLXR_PRIO_OFF, said, sizeof said) == MLXR_PRIO_OFF);
    CHECK(said[0] != '\0');
    // Burn a little CPU so the counter moves, then read it back
    volatile double x = 0.0;
    for (int i = 0; i < 2000000; i++) {
        x += sqrt((double) i);
    }
    uint64_t cpu = 0, wait = 0;
    CHECK(mlxr_thread_schedstat(mlxr_thread_id(), &cpu, &wait) == 0);
    CHECK(cpu > 0);
    CHECK(mlxr_thread_schedstat(-5, &cpu, &wait) != 0);
}

int main(void) {
    testThreadInfo();
    testPolicy();
    testTrim();
    testSkip();
    testSimulation();
    return checksDone("audio_jitter");
}
