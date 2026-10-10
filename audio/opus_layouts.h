// Header-only helpers for the ENCODE side of a stream (tests and the manual
// tool): the Opus multistream parameters a Sunshine/GFE host uses for each
// Moonlight audio configuration, and channel names. The decode side reads
// them from the stream's OPUS_MULTISTREAM_CONFIGURATION.
//
// Moonlight channel order: 0 FL, 1 FR, 2 FC, 3 LFE, 4 BL, 5 BR, 6 SL, 7 SR.
// mapping[i] = the Opus stream channel carrying Moonlight channel i (used
// unchanged by both the encoder and the decoder).
//   stereo: 1 stream, 1 coupled          mapping 0 1
//   5.1:    4 streams, 2 coupled (FL/FR, BL/BR, FC, LFE)  mapping 0 1 4 5 2 3
//   7.1:    5 streams, 3 coupled (FL/FR, BL/BR, SL/SR, FC, LFE)
//                                        mapping 0 1 6 7 2 3 4 5
#ifndef MLXR_OPUS_LAYOUTS_H
#define MLXR_OPUS_LAYOUTS_H

#include "Limelight.h"

#include <string.h>

static inline int mlxr_layout_fill(int channels, POPUS_MULTISTREAM_CONFIGURATION oc) {
    static const unsigned char m2[8] = {0, 1};
    static const unsigned char m6[8] = {0, 1, 4, 5, 2, 3};
    static const unsigned char m8[8] = {0, 1, 6, 7, 2, 3, 4, 5};
    memset(oc, 0, sizeof *oc);
    oc->sampleRate = 48000;
    oc->channelCount = channels;
    oc->samplesPerFrame = 240;
    switch (channels) {
    case 2: oc->streams = 1; oc->coupledStreams = 1; memcpy(oc->mapping, m2, 8); return 0;
    case 6: oc->streams = 4; oc->coupledStreams = 2; memcpy(oc->mapping, m6, 8); return 0;
    case 8: oc->streams = 5; oc->coupledStreams = 3; memcpy(oc->mapping, m8, 8); return 0;
    default: return -1;
    }
}

static inline const char* mlxr_channel_name(int channels, int c) {
    static const char* const n8[8] = {"FL (front left)", "FR (front right)", "FC (center)",
                                      "LFE (subwoofer)", "BL (back left)", "BR (back right)",
                                      "SL (side left)", "SR (side right)"};
    static const char* const n6[6] = {"FL (front left)", "FR (front right)", "FC (center)",
                                      "LFE (subwoofer)", "SL (surround left)",
                                      "SR (surround right)"};
    static const char* const n2[2] = {"L (left)", "R (right)"};
    if (c < 0 || c >= channels) {
        return "?";
    }
    return channels == 2 ? n2[c] : channels == 6 ? n6[c] : n8[c];
}

#endif
