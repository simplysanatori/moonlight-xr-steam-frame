#include "opus_out.h"

#include "pulse_out.h"
#include "surround.h"
#include "thread_prio.h"

#include <opus_multistream.h>

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_DECODE_FRAMES 5760  /* 120 ms at 48 kHz, the longest Opus packet */

static struct {
    MlxrOpusOutConfig cfg;          // the config in force (set by configure/init)
    int haveCfg;

    OpusMSDecoder* dec;
    int channels, rate, samplesPerFrame;
    float* pcm;                     // decoded, channels * MAX_DECODE_FRAMES
    float* stereo;                  // 2 * MAX_DECODE_FRAMES
    MlxrSurround* surround;
    MlxrPulseOut* sink;
    int initialised;
    int noSink;                     // PulseAudio could not be opened
    int started;
    int mutedApplied;               // audio-thread view of the mute state

    _Atomic float yaw, pitch;
    _Atomic int surroundEnabled;
    _Atomic int muted;
    _Atomic int running;

    // stats
    _Atomic uint64_t packets, framesOut, droppedFrames;
    _Atomic uint32_t lost, decodeErrors, mutedPackets, overruns;
    _Atomic float processUs, processMaxUs;
    _Atomic int threadId;
    _Atomic int64_t firstPacketNs, lastPacketNs;           // common-c's audio receive thread, once seen
    int priorityDone;               // audio thread: the raise was tried
    char threadPriority[96];        // written once by the audio thread before threadId
    MlxrOpusOutStats final;         // totals of the stream that ended
    int haveFinal;
} O;

void mlxr_opus_out_config_default(MlxrOpusOutConfig* cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->surround = true;
}

void mlxr_opus_out_configure(const MlxrOpusOutConfig* cfg) {
    O.cfg = *cfg;
    O.haveCfg = 1;
    atomic_store(&O.surroundEnabled, cfg->surround ? 1 : 0);
}

void mlxr_opus_out_set_pose(float yawRadians, float pitchRadians) {
    if (isfinite(yawRadians)) {
        atomic_store_explicit(&O.yaw, yawRadians, memory_order_relaxed);
    }
    if (isfinite(pitchRadians)) {
        atomic_store_explicit(&O.pitch, pitchRadians, memory_order_relaxed);
    }
}

void mlxr_opus_out_set_surround_enabled(bool enabled) {
    atomic_store(&O.surroundEnabled, enabled ? 1 : 0);
}

void mlxr_opus_out_set_muted(bool muted) {
    atomic_store(&O.muted, muted ? 1 : 0);
}

static void resetCounters(void) {
    atomic_store(&O.packets, 0);
    atomic_store(&O.framesOut, 0);
    atomic_store(&O.droppedFrames, 0);
    atomic_store(&O.lost, 0);
    atomic_store(&O.decodeErrors, 0);
    atomic_store(&O.mutedPackets, 0);
    atomic_store(&O.overruns, 0);
    atomic_store(&O.processUs, 0.0f);
    atomic_store(&O.processMaxUs, 0.0f);
    atomic_store(&O.threadId, 0);
    atomic_store(&O.firstPacketNs, 0);
    atomic_store(&O.lastPacketNs, 0);
    O.priorityDone = 0;
    O.threadPriority[0] = '\0';
}

static void snapshot(MlxrOpusOutStats* out) {
    memset(out, 0, sizeof *out);
    out->packets = atomic_load(&O.packets);
    out->lostPackets = atomic_load(&O.lost);
    out->decodeErrors = atomic_load(&O.decodeErrors);
    out->mutedPackets = atomic_load(&O.mutedPackets);
    out->framesOut = atomic_load(&O.framesOut);
    out->overruns = atomic_load(&O.overruns);
    out->droppedFrames = atomic_load(&O.droppedFrames);
    out->processUs = atomic_load(&O.processUs);
    out->processMaxUs = atomic_load(&O.processMaxUs);
    out->channels = O.channels;
    out->samplesPerFrame = O.samplesPerFrame;
    out->firstPacketNs = atomic_load(&O.firstPacketNs);
    out->lastPacketNs = atomic_load(&O.lastPacketNs);
    out->muted = atomic_load(&O.muted);
    out->running = atomic_load(&O.running);
    if (O.surround != NULL) {
        out->surroundActive = mlxr_surround_is_enabled(O.surround);
        out->taps = mlxr_surround_taps(O.surround);
        snprintf(out->kernel, sizeof out->kernel, "%s", mlxr_surround_kernel(O.surround));
    }
    if (O.sink != NULL) {
        MlxrPulseOutStats ps;
        mlxr_pulse_out_stats(O.sink, &ps);
        out->underruns = ps.underruns;
        out->lateUnderruns = ps.lateUnderruns;
        out->silenceUnderruns = ps.silenceUnderruns;
        out->trimmedFrames = ps.trimmedFrames;
        out->jitterGrows = ps.jitterGrows;
        out->jitterShrinks = ps.jitterShrinks;
        out->outages = ps.outages;
        out->sinkFrames = ps.writtenFrames;
        out->jitterTargetMs = (float) ps.jitterTargetMs;
        out->gapMaxMs = ps.gapMaxMs;
        out->overruns += ps.overruns;
        out->droppedFrames += ps.droppedFrames;
        out->queueMs = ps.queueMs;
        out->latencyMs = ps.latencyMs;
        out->tlengthMs = ps.tlengthMs;
        out->bufferedMs = ps.queueMs + ps.latencyMs;
        out->sinkDead = ps.dead;
        out->running = out->running && !ps.dead;
    }
    int tid = atomic_load(&O.threadId);
    if (tid > 0) {
        snprintf(out->threadPriority, sizeof out->threadPriority, "%s", O.threadPriority);
        mlxr_thread_schedstat(tid, &out->threadCpuNs, &out->threadWaitNs);
    }
    if (O.noSink) {
        out->sinkDead = 1;
        out->running = 0;
    }
}

int mlxr_opus_out_take_gap_max(void) {
    return O.initialised && O.sink != NULL ? mlxr_pulse_out_take_gap_max(O.sink) : 0;
}

void mlxr_opus_out_stats(MlxrOpusOutStats* out) {
    if (!O.initialised && O.haveFinal) {
        *out = O.final;
        return;
    }
    snapshot(out);
}

static void teardown(void) {
    atomic_store(&O.running, 0);
    if (O.initialised) {
        snapshot(&O.final);
        O.final.running = 0;
        O.haveFinal = 1;
    }
    if (O.sink != NULL) {
        mlxr_pulse_out_close(O.sink);
        O.sink = NULL;
    }
    if (O.surround != NULL) {
        mlxr_surround_destroy(O.surround);
        O.surround = NULL;
    }
    if (O.dec != NULL) {
        opus_multistream_decoder_destroy(O.dec);
        O.dec = NULL;
    }
    free(O.pcm);
    free(O.stereo);
    O.pcm = O.stereo = NULL;
    O.initialised = 0;
}

static int rendererInit(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION oc,
                        void* context, int arFlags) {
    if (O.initialised) {
        teardown();
    }
    if (context != NULL) {
        mlxr_opus_out_configure((const MlxrOpusOutConfig*) context);
    } else if (!O.haveCfg) {
        MlxrOpusOutConfig def;
        mlxr_opus_out_config_default(&def);
        mlxr_opus_out_configure(&def);
    }
    const MlxrOpusOutConfig* cfg = &O.cfg;
    O.noSink = 0;
    int channels = oc->channelCount;
    if (channels != 2 && channels != 6 && channels != 8) {
        fprintf(stderr, "audio: unsupported channel count %d\n", channels);
        return -1;
    }
    O.channels = channels;
    O.rate = oc->sampleRate;
    O.samplesPerFrame = oc->samplesPerFrame > 0 ? oc->samplesPerFrame : 240;
    if (O.samplesPerFrame > MAX_DECODE_FRAMES) {
        return -1;
    }
    int err = 0;
    O.dec = opus_multistream_decoder_create(oc->sampleRate, channels, oc->streams,
                                            oc->coupledStreams, oc->mapping, &err);
    if (O.dec == NULL || err != OPUS_OK) {
        fprintf(stderr, "audio: opus_multistream_decoder_create failed: %s\n",
                opus_strerror(err));
        teardown();
        return -2;
    }
    O.pcm = (float*) calloc((size_t) MAX_DECODE_FRAMES * (size_t) channels, sizeof(float));
    O.stereo = (float*) calloc((size_t) MAX_DECODE_FRAMES * 2, sizeof(float));
    if (O.pcm == NULL || O.stereo == NULL) {
        teardown();
        return -3;
    }
    if (channels > 2 && cfg->hrtfDir != NULL) {
        MlxrSurroundParams sp;
        mlxr_surround_params_default(&sp);
        sp.hrtfDir = cfg->hrtfDir;
        sp.channels = channels;
        sp.sampleRate = oc->sampleRate;
        sp.maxBlockFrames = O.samplesPerFrame;
        char msg[200] = "";
        O.surround = mlxr_surround_create(&sp, msg, sizeof msg);
        if (O.surround == NULL) {
            // The stream plays on, downmixed
            fprintf(stderr, "audio: virtual surround unavailable (%s), plain downmix\n", msg);
        } else {
            mlxr_surround_set_enabled(O.surround, atomic_load(&O.surroundEnabled) != 0);
        }
    }
    if (cfg->capture == NULL) {
        MlxrPulseOutParams pp;
        mlxr_pulse_out_params_default(&pp);
        pp.sink = cfg->sink;
        pp.streamName = cfg->streamName != NULL ? cfg->streamName
                        : cfg->systemSpatializer ? "Moonlight XR" : "Moonlight XR virtual sink";
        pp.channels = 2;
        pp.rate = oc->sampleRate;
        if (cfg->tlengthMs > 0) {
            pp.tlengthMs = cfg->tlengthMs;
        }
        if (cfg->minreqMs > 0) {
            pp.minreqMs = cfg->minreqMs;
        }
        if (cfg->prebufMs > 0) {
            pp.prebufMs = cfg->prebufMs;
        }
        if (cfg->queueCapMs > 0) {
            pp.queueCapMs = cfg->queueCapMs;
        }
        pp.adaptive = !cfg->fixedBuffer;
        if (cfg->jitterMinMs > 0 || cfg->jitterMaxMs > 0) {
            pp.jitterMinMs = cfg->jitterMinMs;
            pp.jitterMaxMs = cfg->jitterMaxMs;
        }
        const char* env = getenv("MLXR_AUDIO_JITTER");
        int lo, hi;
        if (env != NULL && sscanf(env, "%d,%d", &lo, &hi) == 2 && lo >= 0 && hi >= lo && hi <= 200) {
            pp.jitterMinMs = lo;
            pp.jitterMaxMs = hi;
            pp.adaptive = true;
        }
        char msg[200] = "";
        O.sink = mlxr_pulse_out_open(&pp, msg, sizeof msg);
        if (O.sink == NULL) {
            // The stream plays on without sound rather than failing the whole
            // connection; stats().sinkDead says so
            fprintf(stderr, "audio: PulseAudio unavailable (%s), streaming without sound\n", msg);
            O.noSink = 1;
        }
    }
    resetCounters();
    O.haveFinal = 0;
    O.mutedApplied = 0;
    O.initialised = 1;
    fprintf(stderr, "audio: %d channels at %d Hz, %d frames/packet, %s, %s\n", channels, O.rate,
            O.samplesPerFrame,
            channels == 2 ? "stereo passthrough"
            : O.surround != NULL ? "virtual surround available" : "plain downmix",
            cfg->fixedBuffer && getenv("MLXR_AUDIO_JITTER") == NULL ? "fixed buffer"
                                                                 : "adaptive jitter cushion");
    return 0;
}

static void rendererStart(void) {
    O.started = 1;
    atomic_store(&O.running, 1);
}

static void rendererStop(void) {
    // Samples may still arrive after this; they are discarded
    atomic_store(&O.running, 0);
    O.started = 0;
}

static void rendererCleanup(void) {
    teardown();
}

static double nowUs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1e6 + (double) ts.tv_nsec / 1e3;
}

static void rendererDecode(char* data, int length) {
    if (!atomic_load(&O.running) || O.dec == NULL) {
        return;
    }
    atomic_fetch_add(&O.packets, 1);
    {
        int64_t t = (int64_t) (nowUs() * 1000.0);
        if (atomic_load(&O.firstPacketNs) == 0) {
            atomic_store(&O.firstPacketNs, t);
        }
        atomic_store(&O.lastPacketNs, t);
    }

    // Once, on common-c's receive thread (which runs this): above the frame
    // loop, the depth engine and the panel worker, so a busy CPU does not
    // delay the packets that keep the sink fed. Stock moonlight-qt's audio
    // thread (SDL's) asks rtkit the same way.
    if (!O.priorityDone) {
        O.priorityDone = 1;
        const char* want = getenv("MLXR_AUDIO_PRIORITY");
        if (want == NULL) {
            want = O.cfg.threadPriority;
        }
        MlxrPrioMode m = mlxr_prio_mode_parse(want, MLXR_PRIO_NICE);
        mlxr_thread_raise_priority(m, O.threadPriority, sizeof O.threadPriority);
        fprintf(stderr, "audio: receive thread %s\n", O.threadPriority);
        atomic_store(&O.threadId, mlxr_thread_id());
    }

    // Mute: the audio thread alone touches the stream
    int muted = atomic_load(&O.muted);
    if (muted != O.mutedApplied) {
        O.mutedApplied = muted;
        if (O.sink != NULL) {
            mlxr_pulse_out_set_corked(O.sink, muted != 0);
        }
        if (!muted) {
            // Whatever the decoder and the convolution held is from before the gap
            opus_multistream_decoder_ctl(O.dec, OPUS_RESET_STATE);
            if (O.surround != NULL) {
                mlxr_surround_reset(O.surround);
            }
        }
    }
    if (muted) {
        atomic_fetch_add(&O.mutedPackets, 1);
        return;
    }

    double t0 = nowUs();
    int frames;
    if (data == NULL || length <= 0) {
        atomic_fetch_add(&O.lost, 1);
        frames = opus_multistream_decode_float(O.dec, NULL, 0, O.pcm, O.samplesPerFrame, 0);
    } else {
        frames = opus_multistream_decode_float(O.dec, (const unsigned char*) data, length, O.pcm,
                                               MAX_DECODE_FRAMES, 0);
    }
    if (frames <= 0) {
        atomic_fetch_add(&O.decodeErrors, 1);
        return;
    }
    const MlxrOpusOutConfig* cfg = &O.cfg;
    if (cfg->decodedTap != NULL) {
        cfg->decodedTap(O.pcm, frames, O.channels, cfg->hookUser);
    }

    const float* out = O.pcm;
    if (O.channels > 2) {
        if (O.surround != NULL) {
            mlxr_surround_set_enabled(O.surround, atomic_load(&O.surroundEnabled) != 0);
            mlxr_surround_set_pose(O.surround, atomic_load_explicit(&O.yaw, memory_order_relaxed),
                                   atomic_load_explicit(&O.pitch, memory_order_relaxed));
            mlxr_surround_render(O.surround, O.pcm, frames, O.stereo);
        } else {
            mlxr_downmix_to_stereo(O.channels, O.pcm, frames, O.stereo);
            for (int i = 0; i < 2 * frames; i++) {
                O.stereo[i] = O.stereo[i] > 1.0f ? 1.0f : O.stereo[i] < -1.0f ? -1.0f : O.stereo[i];
            }
        }
        out = O.stereo;
    }
    float us = (float) (nowUs() - t0);
    float avg = atomic_load_explicit(&O.processUs, memory_order_relaxed);
    atomic_store_explicit(&O.processUs, avg == 0.0f ? us : avg + 0.02f * (us - avg),
                          memory_order_relaxed);
    if (us > atomic_load_explicit(&O.processMaxUs, memory_order_relaxed)) {
        atomic_store_explicit(&O.processMaxUs, us, memory_order_relaxed);
    }

    if (cfg->capture != NULL) {
        cfg->capture(out, frames, cfg->hookUser);
    } else if (O.sink != NULL) {
        int dropped = mlxr_pulse_out_write(O.sink, out, frames);
        (void) dropped;
    }
    atomic_fetch_add(&O.framesOut, (uint64_t) frames);
}

void mlxr_opus_out_callbacks(AUDIO_RENDERER_CALLBACKS* cb) {
    memset(cb, 0, sizeof *cb);
    cb->init = rendererInit;
    cb->start = rendererStart;
    cb->stop = rendererStop;
    cb->cleanup = rendererCleanup;
    cb->decodeAndPlaySample = rendererDecode;
    cb->capabilities = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;
}
