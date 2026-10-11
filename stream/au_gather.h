// Moonlight XR Native (lane au-scatter): one decode unit of moonlight-common-c
// as a list of parts for the hardware decoder. GPLv3.
//
// common-c makes one buffer entry per received video packet (1376 bytes of
// payload at packetSize 1392, 1368 for a frame's first packet), plus one
// entry each for the VPS, SPS and PPS of an IDR frame. A frame of 175 KB
// (126 Mbps at 90 fps) is about 128 entries, an IDR frame several hundred.
// The gather keeps every entry: its arrays grow on the heap and are kept
// from one call to the next.
#pragma once
#include <stdint.h>

#include "Limelight.h"

typedef struct MlxrAuGather {
    const void** parts;
    uint32_t* sizes;
    uint32_t cap;      // entries the arrays hold
    uint32_t n;        // parts of the last gathered unit
    uint64_t total;    // its bytes, summed
} MlxrAuGather;

enum {
    MLXR_AU_GATHER_MISMATCH = -1, // the entries do not add up to fullLength: do not submit
    MLXR_AU_GATHER_NOMEM = -2,
};

// Fills g->parts and g->sizes with every entry of list, in order. Returns
// the number of parts (0 for an empty list), MLXR_AU_GATHER_MISMATCH when the
// summed lengths differ from fullLength (or an entry has a negative length),
// or MLXR_AU_GATHER_NOMEM. g->n and g->total are set in every case but NOMEM.
int mlxr_au_gather(MlxrAuGather* g, const LENTRY* list, int fullLength);
void mlxr_au_gather_free(MlxrAuGather* g);
