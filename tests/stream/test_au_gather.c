// Moonlight XR Native (lane au-scatter): the hardware path's gather of a
// decode unit (stream/au_gather.h). Decode units shaped as moonlight-common-c
// makes them at packetSize 1392: 1368 bytes in the first packet's entry,
// 1376 in each later one, VPS, SPS and PPS as their own entries. GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/au_gather.h"

static unsigned char pattern(size_t i) { return (unsigned char)((i * 31u + (i >> 8)) % 251u); }

// n entries: the given header entries, then packets of 1368, 1376, 1376...
// The bytes run through pattern() across all entries.
static LENTRY* makeUnit(const int* headers, int nHeaders, int packets, int* fullLength, unsigned char** bytes) {
    int n = nHeaders + packets;
    LENTRY* e = calloc((size_t)n, sizeof(*e));
    int total = 0;
    for (int i = 0; i < n; i++) total += i < nHeaders ? headers[i] : (i == nHeaders ? 1368 : 1376);
    unsigned char* b = malloc((size_t)total);
    for (int i = 0; i < total; i++) b[i] = pattern((size_t)i);
    int off = 0;
    for (int i = 0; i < n; i++) {
        e[i].length = i < nHeaders ? headers[i] : (i == nHeaders ? 1368 : 1376);
        e[i].data = (char*)b + off;
        e[i].next = i + 1 < n ? &e[i + 1] : NULL;
        off += e[i].length;
    }
    *fullLength = total;
    *bytes = b;
    return e;
}

// Every byte in order, as mlxr_vdec_submit_parts copies the parts
static int joinedMatches(const MlxrAuGather* g, int fullLength) {
    unsigned char* out = malloc((size_t)fullLength);
    size_t off = 0;
    for (uint32_t i = 0; i < g->n; i++) {
        memcpy(out + off, g->parts[i], g->sizes[i]);
        off += g->sizes[i];
    }
    int ok = off == (size_t)fullLength;
    for (size_t i = 0; ok && i < off; i++) ok = out[i] == pattern(i);
    free(out);
    return ok;
}

int main(void) {
    MlxrAuGather g = { 0 };
    int full;
    unsigned char* bytes;

    // The two sums of the owner's log: 64 entries of a P frame, and 64 of
    // an IDR frame whose VPS, SPS and PPS took three of them
    CHECK(1368 + 63 * 1376 == 88056);
    CHECK(1368 + 60 * 1376 == 83928);

    // An IDR frame of 400 packets (about 550 KB) after VPS, SPS and PPS
    static const int vpsSpsPps[3] = { 28, 52, 9 };
    LENTRY* idr = makeUnit(vpsSpsPps, 3, 400, &full, &bytes);
    CHECK(mlxr_au_gather(&g, idr, full) == 403);
    CHECK(g.n == 403 && g.total == (uint64_t)full && g.cap >= 403);
    CHECK(g.sizes[0] == 28 && g.sizes[3] == 1368 && g.sizes[402] == 1376);
    CHECK(joinedMatches(&g, full));
    free(idr);
    free(bytes);

    // A P frame of 200 packets (126 Mbps at 90 fps is about 128): the arrays
    // are kept, nothing is cut at 64
    LENTRY* p = makeUnit(NULL, 0, 200, &full, &bytes);
    CHECK(full == 1368 + 199 * 1376);
    CHECK(mlxr_au_gather(&g, p, full) == 200);
    CHECK(g.total == (uint64_t)full && joinedMatches(&g, full));
    uint32_t keptCap = g.cap;

    // The entries do not add up to the decode unit's length: refused
    CHECK(mlxr_au_gather(&g, p, full + 1) == MLXR_AU_GATHER_MISMATCH);
    CHECK(g.total == (uint64_t)full);
    CHECK(mlxr_au_gather(&g, p, full - 1376) == MLXR_AU_GATHER_MISMATCH);
    p[17].length = -5; // never <= 0 in common-c, refused all the same
    CHECK(mlxr_au_gather(&g, p, full) == MLXR_AU_GATHER_MISMATCH);
    p[17].length = 1376;
    CHECK(mlxr_au_gather(&g, p, -1) == MLXR_AU_GATHER_MISMATCH);
    free(p);
    free(bytes);

    // A short unit: three entries, the arrays stay as they were
    LENTRY* s = makeUnit(NULL, 0, 3, &full, &bytes);
    CHECK(mlxr_au_gather(&g, s, full) == 3);
    CHECK(g.cap == keptCap && joinedMatches(&g, full));
    // A single entry
    s[0].next = NULL;
    CHECK(mlxr_au_gather(&g, s, 1368) == 1 && g.sizes[0] == 1368 && g.parts[0] == s[0].data);
    free(s);
    free(bytes);

    // An empty list: nothing to submit
    CHECK(mlxr_au_gather(&g, NULL, 0) == 0 && g.n == 0);
    CHECK(mlxr_au_gather(&g, NULL, 10) == MLXR_AU_GATHER_MISMATCH);

    mlxr_au_gather_free(&g);
    CHECK(g.parts == NULL && g.cap == 0);
    return checksDone("stream au gather");
}
