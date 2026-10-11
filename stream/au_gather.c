// Moonlight XR Native (lane au-scatter): see au_gather.h. GPLv3.
#include "au_gather.h"

#include <stdlib.h>

int mlxr_au_gather(MlxrAuGather* g, const LENTRY* list, int fullLength) {
    uint32_t count = 0;
    for (const LENTRY* e = list; e; e = e->next) count++;
    if (count > g->cap) {
        uint32_t cap = g->cap ? g->cap : 256;
        while (cap < count) cap *= 2;
        const void** parts = realloc(g->parts, (size_t)cap * sizeof(*parts));
        if (!parts) return MLXR_AU_GATHER_NOMEM;
        g->parts = parts;
        uint32_t* sizes = realloc(g->sizes, (size_t)cap * sizeof(*sizes));
        if (!sizes) return MLXR_AU_GATHER_NOMEM;
        g->sizes = sizes;
        g->cap = cap;
    }
    uint64_t total = 0;
    int bad = 0;
    uint32_t n = 0;
    for (const LENTRY* e = list; e; e = e->next) {
        if (e->length < 0) bad = 1;
        g->parts[n] = e->data;
        g->sizes[n++] = e->length < 0 ? 0u : (uint32_t)e->length;
        total += e->length < 0 ? 0u : (uint64_t)e->length;
    }
    g->n = n;
    g->total = total;
    if (bad || fullLength < 0 || total != (uint64_t)fullLength) return MLXR_AU_GATHER_MISMATCH;
    return (int)n;
}

void mlxr_au_gather_free(MlxrAuGather* g) {
    free(g->parts);
    free(g->sizes);
    *g = (MlxrAuGather){ 0 };
}
