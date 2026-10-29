// The Opus renderer without a sound server: encode a known multichannel signal
// with libopus, push the packets through the renderer's callbacks, and look at
// what the decoder produced (via the tap, in Moonlight channel order) and at the
// stereo that reaches the sink (via the capture hook).
#include "check.h"
#include "opus_layouts.h"
#include "opus_out.h"
#include "tutil.h"

#include <opus_multistream.h>

#include <stdlib.h>
#include <string.h>

#ifndef HRTF_DIR
#error HRTF_DIR
#endif

#define RATE 48000
#define MAXCH 8

typedef struct {
    float* tap;     // decoded, channels interleaved
    int tapFrames;
    float* stereo;
    int stereoFrames;
    int captureCalls;
    int cap;
} Rec;

static Rec rec;

static void onTap(const float* pcm, int frames, int channels, void* user) {
    if (rec.tapFrames + frames <= rec.cap) {
        memcpy(rec.tap + (size_t) rec.tapFrames * channels, pcm,
               sizeof(float) * (size_t) frames * channels);
        rec.tapFrames += frames;
    }
}

static void onCapture(const float* stereo, int frames, void* user) {
    if (rec.stereoFrames + frames <= rec.cap) {
        memcpy(rec.stereo + (size_t) rec.stereoFrames * 2, stereo,
               sizeof(float) * (size_t) frames * 2);
        rec.stereoFrames += frames;
    }
    rec.captureCalls++;
}

static void recReset(void) {
    rec.tapFrames = rec.stereoFrames = rec.captureCalls = 0;
}

typedef struct {
    AUDIO_RENDERER_CALLBACKS ar;
    OPUS_MULTISTREAM_CONFIGURATION oc;
    OpusMSEncoder* enc;
    int channels;
} Stream;

static int openStream(Stream* s, int channels, int surround, int withHrtf, int samplesPerFrame) {
    memset(s, 0, sizeof *s);
    s->channels = channels;
    mlxr_layout_fill(channels, &s->oc);
    s->oc.samplesPerFrame = samplesPerFrame;
    int err = 0;
    s->enc = opus_multistream_encoder_create(RATE, channels, s->oc.streams, s->oc.coupledStreams,
                                             s->oc.mapping, OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                             &err);
    CHECK(s->enc != NULL);
    if (s->enc == NULL) {
        return -1;
    }
    opus_multistream_encoder_ctl(s->enc, OPUS_SET_BITRATE(96000 * s->oc.streams));
    MlxrOpusOutConfig cfg;
    mlxr_opus_out_config_default(&cfg);
    cfg.hrtfDir = withHrtf ? HRTF_DIR : NULL;
    cfg.surround = surround != 0;
    cfg.decodedTap = onTap;
    cfg.capture = onCapture;
    mlxr_opus_out_callbacks(&s->ar);
    int audioConfiguration = channels == 2 ? AUDIO_CONFIGURATION_STEREO
                             : channels == 6 ? AUDIO_CONFIGURATION_51_SURROUND
                                             : AUDIO_CONFIGURATION_71_SURROUND;
    int rc = s->ar.init(audioConfiguration, &s->oc, &cfg, 0);
    CHECK(rc == 0);
    s->ar.start();
    return rc;
}

static void closeStream(Stream* s) {
    s->ar.stop();
    s->ar.cleanup();
    opus_multistream_encoder_destroy(s->enc);
}

// Feeds `packets` packets of `fr` frames. gen(channel, sampleIndex) gives the signal.
typedef float (*Gen)(int channel, long n, void* arg);

static long fed;

static void feed(Stream* s, int packets, int fr, Gen gen, void* arg) {
    float* in = (float*) calloc((size_t) fr * s->channels, sizeof(float));
    unsigned char pkt[4000];
    for (int p = 0; p < packets; p++) {
        for (int f = 0; f < fr; f++) {
            for (int c = 0; c < s->channels; c++) {
                in[f * s->channels + c] = gen(c, fed + f, arg);
            }
        }
        fed += fr;
        int len = opus_multistream_encode_float(s->enc, in, fr, pkt, sizeof pkt);
        CHECK(len > 0);
        if (len > 0) {
            s->ar.decodeAndPlaySample((char*) pkt, len);
        }
    }
    free(in);
}

static float soloGen(int channel, long n, void* arg) {
    int only = *(int*) arg;
    return channel == only ? 0.5f * sinf(2.0f * (float) M_PI * 1000.0f * (float) n / RATE) : 0.0f;
}

static const double SOLO_RMS = 0.5 / 1.41421356;

// The span of the recording to analyse: skip the codec's start-up
static int skip(int frames) {
    return frames / 2;
}

static void testSolo(int channels) {
    printf("%d channels, one channel at a time\n", channels);
    for (int only = 0; only < channels; only++) {
        Stream s;
        recReset();
        fed = 0;
        if (openStream(&s, channels, 0, 0, 240) != 0) {
            return;
        }
        feed(&s, 40, 240, soloGen, &only);
        CHECK(rec.tapFrames == 40 * 240);
        int a = skip(rec.tapFrames), n = rec.tapFrames - a;
        char line[200];
        int off = snprintf(line, sizeof line, "  %-20s decoded:", mlxr_channel_name(channels, only));
        for (int c = 0; c < channels; c++) {
            double level = toneRms(rec.tap + (size_t) a * channels + c, n, channels, 1000.0, RATE);
            double total = rmsOf(rec.tap + (size_t) a * channels + c, n, channels);
            off += snprintf(line + off, sizeof line - (size_t) off, " %6.1f", db(total / SOLO_RMS));
            if (c == only) {
                // the right channel carries the tone, within 1 dB of the encoded level
                CHECK_NEAR(db(level / SOLO_RMS), 0.0, 1.0);
            } else {
                // every other channel is silent (> 50 dB down)
                CHECK(total < SOLO_RMS * 0.003);
            }
        }
        printf("%s dB\n", line);

        // The sink gets stereo, 5 ms per packet
        CHECK(rec.stereoFrames == 40 * 240);
        const float* L = rec.stereo + (size_t) a * 2;
        const float* R = L + 1;
        double l = rmsOf(L, n, 2), r = rmsOf(R, n, 2);
        if (channels == 2) {
            // passthrough: exactly the decoded channels
            CHECK(memcmp(rec.stereo, rec.tap, sizeof(float) * (size_t) rec.tapFrames * 2) == 0);
            if (only == 0) CHECK(l > 100 * r);
            if (only == 1) CHECK(r > 100 * l);
        } else {
            // plain downmix: left-side channels left, right-side right, centre/LFE both at -3 dB
            int leftSide = only == 0 || only == 4 || only == 6;
            int rightSide = only == 1 || only == 5 || only == 7;
            if (leftSide) {
                CHECK(l > 100 * r);
                CHECK_NEAR(db(l / SOLO_RMS), only == 0 ? 0.0 : -3.01, 0.7);
            } else if (rightSide) {
                CHECK(r > 100 * l);
                CHECK_NEAR(db(r / SOLO_RMS), only == 1 ? 0.0 : -3.01, 0.7);
            } else {
                CHECK_NEAR(db(l / r), 0.0, 0.1);
                CHECK_NEAR(db(l / SOLO_RMS), -3.01, 0.7);
            }
        }
        closeStream(&s);
    }
}

static void testSurroundSolo(int channels) {
    printf("%d channels through the virtual surround\n", channels);
    for (int only = 0; only < channels; only++) {
        Stream s;
        recReset();
        fed = 0;
        if (openStream(&s, channels, 1, 1, 240) != 0) {
            return;
        }
        feed(&s, 40, 240, soloGen, &only);
        int a = skip(rec.stereoFrames), n = rec.stereoFrames - a;
        const float* L = rec.stereo + (size_t) a * 2;
        double l = toneRms(L, n, 2, 1000.0, RATE), r = toneRms(L + 1, n, 2, 1000.0, RATE);
        double bal = db(l / r);
        printf("  %-20s left-right %+6.1f dB\n", mlxr_channel_name(channels, only), bal);
        int leftSide = only == 0 || only == 4 || only == 6;
        int rightSide = only == 1 || only == 5 || only == 7;
        if (leftSide) {
            CHECK(bal > 3.0);
        } else if (rightSide) {
            CHECK(bal < -3.0);
        } else {
            CHECK_NEAR(bal, 0.0, 0.5);
        }
        // Nothing is lost on the way: some energy reaches both ears
        CHECK(l > 0.01 && r > 0.01);
        closeStream(&s);
    }
}

static const double FREQS[MAXCH] = {440, 660, 880, 1100, 1320, 1540, 1760, 1980};

static float chordGen(int channel, long n, void* arg) {
    return 0.15f * sinf(2.0f * (float) M_PI * (float) FREQS[channel] * (float) n / RATE);
}

// All channels at once, each a different frequency: each must come out of its own
// channel and no other
static void testMapping(int channels) {
    Stream s;
    recReset();
    fed = 0;
    if (openStream(&s, channels, 0, 0, 240) != 0) {
        return;
    }
    feed(&s, 60, 240, chordGen, NULL);
    int a = skip(rec.tapFrames), n = rec.tapFrames - a;
    double want = 0.15 / 1.41421356;
    int bad = 0;
    for (int c = 0; c < channels; c++) {
        for (int k = 0; k < channels; k++) {
            double level = toneRms(rec.tap + (size_t) a * channels + c, n, channels, FREQS[k], RATE);
            if (k == c) {
                if (fabs(db(level / want)) > 1.0) bad++;
            } else if (level > want * 0.01) {
                bad++;
            }
        }
    }
    CHECK(bad == 0);
    closeStream(&s);
}

static void testStats(void) {
    Stream s;
    recReset();
    fed = 0;
    int only = 0;
    if (openStream(&s, 6, 0, 0, 240) != 0) {
        return;
    }
    feed(&s, 20, 240, soloGen, &only);
    // Two lost packets: concealment fills in 5 ms each, no error
    s.ar.decodeAndPlaySample(NULL, 0);
    s.ar.decodeAndPlaySample(NULL, 0);
    feed(&s, 5, 240, soloGen, &only);
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    CHECK(st.packets == 27);
    CHECK(st.lostPackets == 2);
    CHECK(st.decodeErrors == 0);
    CHECK(st.framesOut == 27 * 240);
    CHECK(rec.stereoFrames == 27 * 240);
    CHECK(st.channels == 6);
    CHECK(st.running == 1);
    CHECK(st.processUs > 0 && st.processUs < 4000);
    // Garbage that is not a packet is counted, not fatal
    unsigned char junk[3] = {0xff, 0xfe, 0xfd};
    s.ar.decodeAndPlaySample((char*) junk, 3);
    mlxr_opus_out_stats(&st);
    CHECK(st.decodeErrors == 1);

    // Mute: packets are dropped (nothing reaches the sink), unmute resumes
    int before = rec.captureCalls;
    mlxr_opus_out_set_muted(true);
    feed(&s, 10, 240, soloGen, &only);
    mlxr_opus_out_stats(&st);
    CHECK(rec.captureCalls == before);
    CHECK(st.mutedPackets == 10);
    CHECK(st.muted == 1);
    mlxr_opus_out_set_muted(false);
    feed(&s, 10, 240, soloGen, &only);
    CHECK(rec.captureCalls == before + 10);
    mlxr_opus_out_stats(&st);
    CHECK(st.muted == 0);
    uint64_t packets = st.packets;
    // After stop, samples are discarded
    s.ar.stop();
    int calls = rec.captureCalls;
    feed(&s, 3, 240, soloGen, &only);
    CHECK(rec.captureCalls == calls);
    s.ar.cleanup();
    // The totals of the stream that ended stay readable
    mlxr_opus_out_stats(&st);
    CHECK(st.packets == packets);
    CHECK(st.running == 0);
    opus_multistream_encoder_destroy(s.enc);
}

// 10 ms packets (the host may send them): samplesPerFrame = 480
static void testTenMs(void) {
    Stream s;
    recReset();
    fed = 0;
    int only = 2;
    if (openStream(&s, 8, 1, 1, 480) != 0) {
        return;
    }
    feed(&s, 30, 480, soloGen, &only);
    CHECK(rec.tapFrames == 30 * 480);
    CHECK(rec.stereoFrames == 30 * 480);
    int a = skip(rec.stereoFrames), n = rec.stereoFrames - a;
    double l = rmsOf(rec.stereo + (size_t) a * 2, n, 2), r = rmsOf(rec.stereo + (size_t) a * 2 + 1, n, 2);
    CHECK_NEAR(db(l / r), 0.0, 0.5);
    closeStream(&s);
}

// Surround toggled and head pose set through the module API while streaming
static void testRuntimeControls(void) {
    Stream s;
    recReset();
    fed = 0;
    int only = 2;  // centre
    if (openStream(&s, 6, 1, 1, 240) != 0) {
        return;
    }
    mlxr_opus_out_set_pose(0.0f, 0.0f);
    feed(&s, 30, 240, soloGen, &only);
    int mark = rec.stereoFrames;
    double l0 = toneRms(rec.stereo + (size_t) (mark / 2) * 2, mark / 2, 2, 1000.0, RATE);
    double r0 = toneRms(rec.stereo + (size_t) (mark / 2) * 2 + 1, mark / 2, 2, 1000.0, RATE);
    CHECK_NEAR(db(l0 / r0), 0.0, 0.5);
    // Head turned left by 90 degrees: the screen is at the right ear
    mlxr_opus_out_set_pose(1.5707963f, 0.0f);
    feed(&s, 30, 240, soloGen, &only);
    int a = mark + (rec.stereoFrames - mark) / 2, n = rec.stereoFrames - a;
    double l1 = toneRms(rec.stereo + (size_t) a * 2, n, 2, 1000.0, RATE);
    double r1 = toneRms(rec.stereo + (size_t) a * 2 + 1, n, 2, 1000.0, RATE);
    printf("  FC, head turned 90 left: left-right %+.1f dB\n", db(l1 / r1));
    CHECK(db(r1 / l1) > 6.0);
    // Surround off: the plain downmix puts the centre equally in both, whatever the pose
    mlxr_opus_out_set_surround_enabled(false);
    feed(&s, 30, 240, soloGen, &only);
    int b = rec.stereoFrames - 15 * 240, m = 15 * 240;
    double l2 = toneRms(rec.stereo + (size_t) b * 2, m, 2, 1000.0, RATE);
    double r2 = toneRms(rec.stereo + (size_t) b * 2 + 1, m, 2, 1000.0, RATE);
    CHECK_NEAR(db(l2 / r2), 0.0, 0.1);
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    CHECK(st.surroundActive == 0);
    CHECK(st.taps == 175);
    CHECK(strcmp(st.kernel, "FFT") == 0);
    mlxr_opus_out_set_surround_enabled(true);
    closeStream(&s);
}

// What the frame loop feeds: the head's yaw against the screen, positive = turned
// left (xr/core yawBetween). A source on the screen has to stay with the screen,
// so turning the head one way and the other swings it to the opposite ears.
static double centreBalanceDb(Stream* s, float yaw) {
    int only = 2;  // centre
    mlxr_opus_out_set_pose(yaw, 0.0f);
    int mark = rec.stereoFrames;
    feed(s, 30, 240, soloGen, &only);
    int a = mark + (rec.stereoFrames - mark) / 2, n = rec.stereoFrames - a;
    double l = toneRms(rec.stereo + (size_t) a * 2, n, 2, 1000.0, RATE);
    double r = toneRms(rec.stereo + (size_t) a * 2 + 1, n, 2, 1000.0, RATE);
    return db(l / r);
}

static void testHeadYawDirection(void) {
    Stream s;
    recReset();
    fed = 0;
    if (openStream(&s, 6, 1, 1, 240) != 0) {
        return;
    }
    const float quarter = 1.5707963f;
    double ahead = centreBalanceDb(&s, 0.0f);
    double left = centreBalanceDb(&s, quarter);     // head turned left: screen at the right ear
    double right = centreBalanceDb(&s, -quarter);   // head turned right: screen at the left ear
    printf("  FC left-right: ahead %+.1f dB, head left 90 %+.1f dB, head right 90 %+.1f dB
", ahead,
           left, right);
    CHECK_NEAR(ahead, 0.0, 0.5);
    CHECK(left < -6.0);
    CHECK(right > 6.0);
    // The same angle either way is the same distance from the midline
    CHECK_NEAR(left, -right, 3.0);
    // Back to the front and the sound is back in the middle
    CHECK_NEAR(centreBalanceDb(&s, 0.0f), 0.0, 0.5);
    closeStream(&s);
}

// Headset off: nothing reaches the sink, and putting it on again restores the
// sound at the level it had, with no spike. (There is no gain ramp: the stream is
// corked and the decoder restarted, so the first packet starts from silence.)
static void testMuteRestores(void) {
    Stream s;
    recReset();
    fed = 0;
    int only = 2;
    if (openStream(&s, 2, 0, 0, 240) != 0) {
        return;
    }
    mlxr_opus_out_set_muted(false);
    feed(&s, 40, 240, soloGen, &only);
    int a = rec.stereoFrames / 2, n = rec.stereoFrames - a;
    double base = toneRms(rec.stereo + (size_t) a * 2, n, 2, 1000.0, RATE);
    CHECK(base > 0.1);

    int framesBefore = rec.stereoFrames;
    mlxr_opus_out_set_muted(true);
    feed(&s, 20, 240, soloGen, &only);
    CHECK(rec.stereoFrames == framesBefore);

    mlxr_opus_out_set_muted(false);
    int mark = rec.stereoFrames;
    feed(&s, 40, 240, soloGen, &only);
    CHECK(rec.stereoFrames == mark + 40 * 240);
    float peak = 0.0f;
    for (int i = mark * 2; i < rec.stereoFrames * 2; i++) {
        float v = fabsf(rec.stereo[i]);
        if (v > peak) {
            peak = v;
        }
    }
    int b = mark + (rec.stereoFrames - mark) / 2, m = rec.stereoFrames - b;
    double after = toneRms(rec.stereo + (size_t) b * 2, m, 2, 1000.0, RATE);
    printf("  level before mute %.3f, after unmute %.3f, peak after %.3f
", base, after, peak);
    CHECK_NEAR(db(after / base), 0.0, 1.0);
    CHECK(peak <= 1.0f);
    closeStream(&s);
}

static void testInitFailures(void) {
    AUDIO_RENDERER_CALLBACKS ar;
    mlxr_opus_out_callbacks(&ar);
    CHECK((ar.capabilities & CAPABILITY_DIRECT_SUBMIT) != 0);
    OPUS_MULTISTREAM_CONFIGURATION oc;
    mlxr_layout_fill(6, &oc);
    MlxrOpusOutConfig cfg;
    mlxr_opus_out_config_default(&cfg);
    cfg.capture = onCapture;
    // Four channels is not a Moonlight configuration
    OPUS_MULTISTREAM_CONFIGURATION bad = oc;
    bad.channelCount = 4;
    CHECK(ar.init(0, &bad, &cfg, 0) != 0);
    // A mapping that points outside the streams is rejected by libopus
    bad = oc;
    bad.mapping[0] = 17;
    CHECK(ar.init(AUDIO_CONFIGURATION_51_SURROUND, &bad, &cfg, 0) != 0);
    // Missing HRTF files: the stream still starts, downmixed
    cfg.hrtfDir = "/nonexistent";
    recReset();
    CHECK(ar.init(AUDIO_CONFIGURATION_51_SURROUND, &oc, &cfg, 0) == 0);
    ar.start();
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    CHECK(st.taps == 0);
    ar.stop();
    ar.cleanup();
}

// No sound server: the stream still runs (silently) instead of failing the connection
static void testNoServer(void) {
    setenv("PULSE_SERVER", "unix:/nonexistent/mlxr-pulse-socket", 1);
    AUDIO_RENDERER_CALLBACKS ar;
    mlxr_opus_out_callbacks(&ar);
    OPUS_MULTISTREAM_CONFIGURATION oc;
    mlxr_layout_fill(2, &oc);
    MlxrOpusOutConfig cfg;
    mlxr_opus_out_config_default(&cfg);  // no capture hook: tries PulseAudio
    CHECK(ar.init(AUDIO_CONFIGURATION_STEREO, &oc, &cfg, 0) == 0);
    ar.start();
    int err;
    OpusMSEncoder* enc = opus_multistream_encoder_create(RATE, 2, 1, 1, oc.mapping,
                                                         OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    float in[240 * 2] = {0};
    unsigned char pkt[400];
    int len = opus_multistream_encode_float(enc, in, 240, pkt, sizeof pkt);
    for (int i = 0; i < 10; i++) {
        ar.decodeAndPlaySample((char*) pkt, len);
    }
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    CHECK(st.sinkDead == 1);
    CHECK(st.running == 0);
    CHECK(st.packets == 10);
    ar.stop();
    ar.cleanup();
    opus_multistream_encoder_destroy(enc);
    unsetenv("PULSE_SERVER");
}

int main(void) {
    rec.cap = 100 * 480;
    rec.tap = (float*) calloc((size_t) rec.cap * MAXCH, sizeof(float));
    rec.stereo = (float*) calloc((size_t) rec.cap * 2, sizeof(float));
    testSolo(2);
    testSolo(6);
    testSolo(8);
    testMapping(2);
    testMapping(6);
    testMapping(8);
    testSurroundSolo(6);
    testSurroundSolo(8);
    testStats();
    testTenMs();
    testRuntimeControls();
    testInitFailures();
    testNoServer();
    return checksDone("audio_opus");
}
