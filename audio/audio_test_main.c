// mlxr-audio-test: plays a short tone through each channel in turn, through the
// real decode -> (surround) -> PulseAudio path, for checking by ear.
//
//   mlxr-audio-test --tones stereo|51|71 [--surround] [--hrtf-dir DIR]
//                   [--sink NAME] [--stream-name NAME] [--system-spatializer]
//                   [--level DB] [--seconds S] [--yaw DEG] [--dry]
//
// Each channel plays up to 1 s (default 0.8 s) at -20 dBFS (--level) with a
// 0.25 s gap, and its name is printed as it starts. --surround renders stereo,
// 5.1 and 7.1 through the HRTF virtual surround (needs --hrtf-dir, default
// assets/hrtf/kemar next to the source tree); without it 5.1/7.1 are
// downmixed and stereo passes through. By default the stream bypasses SteamOS's VR spatializer (see
// opus_out.h); --system-spatializer lets the system process it, for A/B listening.
// --dry decodes everything but discards the output (no sound server).
#include "opus_layouts.h"
#include "opus_out.h"

#include <opus_multistream.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef MLXR_ASSETS_DIR
#define MLXR_ASSETS_DIR "assets"
#endif

static void sleepUntil(struct timespec* t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec++;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL);
}

static void dryCapture(const float* stereo, int frames, void* user) {}

static void usage(void) {
    fprintf(stderr,
            "usage: mlxr-audio-test --tones stereo|51|71 [--surround] [--hrtf-dir DIR]\n"
            "       [--sink NAME] [--stream-name NAME] [--system-spatializer]\n"
            "       [--level DB] [--seconds S] [--yaw DEG] [--dry]\n");
}

int main(int argc, char** argv) {
    int channels = 0;
    int surround = 0, dry = 0, systemSpatializer = 0;
    double level = -20.0, seconds = 0.8, yawDeg = 0.0;
    const char* sink = NULL;
    const char* streamName = NULL;
    const char* hrtf = MLXR_ASSETS_DIR "/hrtf/kemar";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tones") && i + 1 < argc) {
            const char* v = argv[++i];
            channels = !strcmp(v, "stereo") ? 2 : !strcmp(v, "51") ? 6 : !strcmp(v, "71") ? 8 : 0;
        } else if (!strcmp(argv[i], "--surround")) {
            surround = 1;
        } else if (!strcmp(argv[i], "--dry")) {
            dry = 1;
        } else if (!strcmp(argv[i], "--hrtf-dir") && i + 1 < argc) {
            hrtf = argv[++i];
        } else if (!strcmp(argv[i], "--sink") && i + 1 < argc) {
            sink = argv[++i];
        } else if (!strcmp(argv[i], "--system-spatializer")) {
            systemSpatializer = 1;
        } else if (!strcmp(argv[i], "--stream-name") && i + 1 < argc) {
            streamName = argv[++i];
        } else if (!strcmp(argv[i], "--level") && i + 1 < argc) {
            level = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) {
            yawDeg = atof(argv[++i]);
        } else {
            usage();
            return 2;
        }
    }
    if (channels == 0) {
        usage();
        return 2;
    }
    if (seconds > 1.0) {
        seconds = 1.0;
    }
    if (level > -6.0) {
        level = -6.0;  // modest, whatever is asked
    }

    OPUS_MULTISTREAM_CONFIGURATION oc;
    mlxr_layout_fill(channels, &oc);
    int err = 0;
    OpusMSEncoder* enc = opus_multistream_encoder_create(oc.sampleRate, channels, oc.streams,
                                                         oc.coupledStreams, oc.mapping,
                                                         OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                                         &err);
    if (enc == NULL) {
        fprintf(stderr, "encoder: %s\n", opus_strerror(err));
        return 1;
    }
    opus_multistream_encoder_ctl(enc, OPUS_SET_BITRATE(64000 * oc.streams));

    MlxrOpusOutConfig cfg;
    mlxr_opus_out_config_default(&cfg);
    cfg.hrtfDir = surround ? hrtf : NULL;
    cfg.surround = surround != 0;
    cfg.sink = sink;
    cfg.streamName = streamName;
    cfg.systemSpatializer = systemSpatializer != 0;
    if (dry) {
        cfg.capture = dryCapture;
    }
    AUDIO_RENDERER_CALLBACKS ar;
    mlxr_opus_out_callbacks(&ar);
    int audioConfiguration = channels == 2 ? AUDIO_CONFIGURATION_STEREO
                             : channels == 6 ? AUDIO_CONFIGURATION_51_SURROUND
                                             : AUDIO_CONFIGURATION_71_SURROUND;
    if (ar.init(audioConfiguration, &oc, &cfg, 0) != 0) {
        fprintf(stderr, "audio init failed\n");
        return 1;
    }
    ar.start();
    mlxr_opus_out_set_pose((float) (yawDeg * M_PI / 180.0), 0.0f);
    printf("%s, %s%s, %.0f dBFS, %.1f s per channel\n",
           channels == 2 ? "stereo" : channels == 6 ? "5.1" : "7.1",
           surround ? "HRTF virtual surround" : channels == 2 ? "passthrough" : "plain downmix",
           dry ? " (dry run)" : "", level, seconds);

    const int fr = oc.samplesPerFrame;
    float* in = (float*) calloc((size_t) fr * channels, sizeof(float));
    unsigned char packet[4000];
    const double amp = pow(10.0, level / 20.0);
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    long long n = 0;  // running frame count, for phase continuity
    const int toneFrames = (int) (seconds * oc.sampleRate) / fr * fr;
    const int gapFrames = oc.sampleRate / 4 / fr * fr;
    for (int c = 0; c < channels; c++) {
        printf("  playing %s\n", mlxr_channel_name(channels, c));
        fflush(stdout);
        const int lfe = channels > 2 && c == 3;
        const double f0 = lfe ? 60.0 : 500.0;
        for (int pos = 0; pos < toneFrames + gapFrames; pos += fr) {
            memset(in, 0, sizeof(float) * (size_t) fr * channels);
            if (pos < toneFrames) {
                for (int f = 0; f < fr; f++) {
                    int p = pos + f;
                    double env = 1.0;
                    if (p < 960) env = p / 960.0;
                    if (toneFrames - p < 960) env = (toneFrames - p) / 960.0;
                    double ph = 2.0 * M_PI * f0 * (double) (n + f) / oc.sampleRate;
                    double s = sin(ph) + 0.5 * sin(2 * ph) + 0.25 * sin(3 * ph);
                    in[f * channels + c] = (float) (amp * env * s / 1.75);
                }
            }
            n += fr;
            int len = opus_multistream_encode_float(enc, in, fr, packet, sizeof packet);
            if (len > 0) {
                ar.decodeAndPlaySample((char*) packet, len);
            }
            sleepUntil(&t, (long) (1e9 * fr / oc.sampleRate));
        }
    }
    sleepUntil(&t, 150000000L);  // let the tail drain
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    printf("packets %llu, lost %u, decode errors %u, underruns %u, overruns %u, dropped %llu fr\n",
           (unsigned long long) st.packets, st.lostPackets, st.decodeErrors, st.underruns,
           st.overruns, (unsigned long long) st.droppedFrames);
    printf("latency: pulse %.1f ms (server tlength %.1f ms) + queue %.1f ms; process %.0f us avg, %.0f us max\n",
           st.latencyMs, st.tlengthMs, st.queueMs, st.processUs, st.processMaxUs);
    ar.stop();
    ar.cleanup();
    free(in);
    opus_multistream_encoder_destroy(enc);
    return 0;
}
