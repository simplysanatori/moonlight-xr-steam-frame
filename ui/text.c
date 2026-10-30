// See text.h. GPLv3.
#include "text.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define ATLAS_W 1024
#define ATLAS_H 1024
#define HASH_SIZE 8192   // power of two, > 2x the glyphs the atlas can hold
#define SUBPIXEL 4
// Coverage is lifted a little (gamma 0.8) so light text on a dark sheet is not
// thinner than the Android renderer's
#define TEXT_GAMMA 0.80f

typedef struct {
    stbtt_fontinfo info;
    unsigned char* data;
    size_t mapped;                   // native (lane wave6-i18n): data is an mmap of this many bytes
    int ascent, descent, line_gap;   // font units
    int loaded;
} Face;

// Faces: 0 regular, 1 medium, then (lane wave6-i18n) the fallback for code points the family
// lacks (a system CJK font), 2 regular and 3 medium
#define FACE_FALLBACK 2

typedef struct {
    uint32_t key;      // 0 = empty; else packed (glyph, size_q, weight, phase) + 1
    uint64_t full;     // full key for compare
    int16_t x, y;      // atlas position
    uint8_t w, h;
    int8_t bx, by;     // bitmap offset from the pen origin at the baseline
} Cached;

struct UiFont {
    Face face[4];
    uint8_t* atlas;
    Cached* table;
    int count;
    int shelf_x, shelf_y, shelf_h;
    uint32_t generation;
    uint8_t lut[256];
};

// Native (lane wave6-i18n): the test hook (text.h ui_text_set_trace)
static UiTextTraceFn g_trace;
static void* g_trace_user;

void ui_text_set_trace(UiTextTraceFn fn, void* user) {
    g_trace = fn;
    g_trace_user = user;
}

static unsigned char* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char* d = malloc((size_t)n);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    if (size) *size = (size_t)n;
    return d;
}

static int face_load(Face* f, const char* path) {
    size_t n;
    f->data = read_file(path, &n);
    if (!f->data) return 0;
    if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
        free(f->data);
        f->data = NULL;
        return 0;
    }
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);
    f->loaded = 1;
    return 1;
}

static void face_free(Face* f) {
    if (f->mapped) munmap(f->data, f->mapped);
    else free(f->data);
    memset(f, 0, sizeof *f);
}

// A face of a font file, mapped rather than read (a CJK collection is about 20 MB, of which a
// few glyphs are used); index picks a face in a .ttc collection
static int face_map(Face* f, const char* path, int index) {
    memset(f, 0, sizeof *f);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 12) { close(fd); return 0; }
    void* p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return 0;
    f->data = p;
    f->mapped = (size_t)st.st_size;
    int off = stbtt_GetFontOffsetForIndex(f->data, index < 0 ? 0 : index);
    if (off < 0 || !stbtt_InitFont(&f->info, f->data, off)) {
        face_free(f);
        return 0;
    }
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);
    f->loaded = 1;
    return 1;
}

UiFont* ui_font_open(const char* regular_path, const char* medium_path) {
    UiFont* font = calloc(1, sizeof(*font));
    if (!font) return NULL;
    if (!regular_path || !face_load(&font->face[0], regular_path)) {
        ui_font_close(font);
        return NULL;
    }
    if (!medium_path || !face_load(&font->face[1], medium_path)) {
        // regular stands in
        font->face[1] = font->face[0];
        font->face[1].data = NULL;   // not owned twice
    }
    font->atlas = calloc((size_t)ATLAS_W * ATLAS_H, 1);
    font->table = calloc(HASH_SIZE, sizeof(Cached));
    if (!font->atlas || !font->table) {
        ui_font_close(font);
        return NULL;
    }
    for (int i = 0; i < 256; i++) {
        font->lut[i] = (uint8_t)(255.0f * powf((float)i / 255.0f, TEXT_GAMMA) + 0.5f);
    }
    font->shelf_x = 1;
    font->shelf_y = 1;
    font->generation = 1;
    return font;
}

void ui_font_close(UiFont* font) {
    if (!font) return;
    free(font->face[0].data);
    free(font->face[1].data);
    face_free(&font->face[FACE_FALLBACK]);
    face_free(&font->face[FACE_FALLBACK + 1]);
    free(font->atlas);
    free(font->table);
    free(font);
}

static int file_exists(const char* p) { return access(p, R_OK) == 0; }

static UiFont* try_dir(const char* dir) {
    char reg[1024], med[1024];
    snprintf(reg, sizeof reg, "%s/NotoSans-Regular.ttf", dir);
    snprintf(med, sizeof med, "%s/NotoSans-Medium.ttf", dir);
    if (!file_exists(reg)) return NULL;
    return ui_font_open(reg, file_exists(med) ? med : NULL);
}

UiFont* ui_font_default(void) {
    static UiFont* shared;
    if (shared) return shared;
    char dir[1024];
    const char* env = getenv("MLXR_FONT_DIR");
    if (env && *env && (shared = try_dir(env))) return shared;
    env = getenv("MLXR_ASSETS_DIR");
    if (env && *env) {
        snprintf(dir, sizeof dir, "%s/fonts", env);
        if ((shared = try_dir(dir))) return shared;
    }
    char exe[900];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char* slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            const char* rel[] = { "assets/fonts", "../assets/fonts", "../../assets/fonts",
                                  "../share/moonlight-xr/assets/fonts" };
            for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++) {
                snprintf(dir, sizeof dir, "%s/%s", exe, rel[i]);
                if ((shared = try_dir(dir))) return shared;
            }
        }
    }
#ifdef MLXR_ASSETS_DIR
    snprintf(dir, sizeof dir, "%s/fonts", MLXR_ASSETS_DIR);
    if ((shared = try_dir(dir))) return shared;
#endif
    return shared = try_dir("/usr/share/fonts/noto");
}

// ---- UTF-8 ---------------------------------------------------------------

int ui_utf8_decode(const char* s, const char* end, uint32_t* cp) {
    const unsigned char* p = (const unsigned char*)s;
    size_t avail = (size_t)(end - s);
    if (avail == 0) { *cp = 0; return 0; }
    unsigned c = p[0];
    if (c < 0x80) { *cp = c; return 1; }
    int need = 0;
    uint32_t v = 0, min = 0;
    if ((c & 0xE0) == 0xC0) { need = 1; v = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { need = 2; v = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { need = 3; v = c & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    if ((size_t)need >= avail) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i <= need; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
    *cp = v;
    return need + 1;
}

int ui_utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int ui_utf8_count(const char* s, int len) {
    if (len < 0) len = (int)strlen(s);
    int n = 0;
    for (const char* p = s; p < s + len;) {
        uint32_t cp;
        p += ui_utf8_decode(p, s + len, &cp);
        n++;
    }
    return n;
}

// ---- metrics and advances ---------------------------------------------------

static Face* face_of(UiFont* font, UiWeight w) {
    return &font->face[w == UI_WEIGHT_MEDIUM ? 1 : 0];
}

static int glyph_of(Face* f, uint32_t cp) {
    if (cp == 0xA0) cp = ' ';
    return stbtt_FindGlyphIndex(&f->info, (int)cp);
}

int ui_font_has(UiFont* font, UiWeight w, uint32_t cp) {
    return glyph_of(face_of(font, w), cp) != 0;
}

// The face that draws cp: the family's, else the fallback's when it has the glyph. Returns the
// face index (0..3) and the glyph in *glyph (0, the family's .notdef, when neither has it).
static int pick(UiFont* font, UiWeight w, uint32_t cp, int* glyph) {
    int fi = w == UI_WEIGHT_MEDIUM ? 1 : 0;
    int g = glyph_of(&font->face[fi], cp);
    if (g == 0 && font->face[FACE_FALLBACK].loaded) {
        int ff = FACE_FALLBACK + (w == UI_WEIGHT_MEDIUM && font->face[FACE_FALLBACK + 1].loaded ? 1 : 0);
        int gf = glyph_of(&font->face[ff], cp);
        if (gf != 0) {
            *glyph = gf;
            return ff;
        }
    }
    *glyph = g;
    return fi;
}

void ui_font_metrics(UiFont* font, UiWeight w, float size_px, UiFontMetrics* out) {
    Face* f = face_of(font, w);
    float s = stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
    out->ascent = (float)f->ascent * s;
    out->descent = (float)-f->descent * s;
    out->line_gap = (float)f->line_gap * s;
    out->line_height = out->ascent + out->descent + out->line_gap;
    int x0, y0, x1, y1;
    int g = glyph_of(f, 'H');
    stbtt_GetGlyphBox(&f->info, g, &x0, &y0, &x1, &y1);
    out->cap_height = (float)y1 * s;
    g = glyph_of(f, 'x');
    stbtt_GetGlyphBox(&f->info, g, &x0, &y0, &x1, &y1);
    out->x_height = (float)y1 * s;
}

typedef struct { int glyph; float adv; } Step;

// Advance of cp followed by next (0 for none), in pixels at size_px; kerning only between two
// glyphs of one face
static float advance_of(UiFont* font, UiWeight w, float size_px, uint32_t cp, uint32_t next, int* glyph_out,
                        int* face_out) {
    int g;
    int fi = pick(font, w, cp, &g);
    Face* f = &font->face[fi];
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
    float a = (float)adv;
    if (next) {
        int gn;
        if (pick(font, w, next, &gn) == fi) a += (float)stbtt_GetGlyphKernAdvance(&f->info, g, gn);
    }
    if (glyph_out) *glyph_out = g;
    if (face_out) *face_out = fi;
    return a * stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
}

float ui_text_width(UiFont* font, UiWeight w, float size_px, const char* s, int len) {
    if (!s) return 0.0f;
    if (len < 0) len = (int)strlen(s);
    const char* end = s + len;
    float x = 0.0f;
    uint32_t cp, next;
    const char* p = s;
    if (p >= end) return 0.0f;
    p += ui_utf8_decode(p, end, &cp);
    while (1) {
        int n = 0;
        next = 0;
        if (p < end) n = ui_utf8_decode(p, end, &next);
        if (cp >= 0x20) x += advance_of(font, w, size_px, cp, next, NULL, NULL);
        if (!n) break;
        cp = next;
        p += n;
    }
    return x;
}

float ui_text_baseline_centered(UiFont* font, UiWeight w, float size_px, float cy) {
    UiFontMetrics m;
    ui_font_metrics(font, w, size_px, &m);
    return cy + (m.ascent - m.descent) * 0.5f;
}

// ---- glyph cache -------------------------------------------------------------

static void flush_cache(UiFont* font) {
    memset(font->table, 0, HASH_SIZE * sizeof(Cached));
    memset(font->atlas, 0, (size_t)ATLAS_W * ATLAS_H);
    font->count = 0;
    font->shelf_x = 1;
    font->shelf_y = 1;
    font->shelf_h = 0;
    font->generation++;
}

// face: 0..3 (bit 3 the weight, bit 61 the fallback)
static uint64_t make_key(int glyph, int size_q, int face, int phase) {
    return ((uint64_t)(uint32_t)glyph << 24) | ((uint64_t)(uint32_t)size_q << 4) |
           ((uint64_t)(face & 1) << 3) | ((uint64_t)(face >> 1) << 61) | (uint64_t)phase | (1ull << 62);
}

static const Cached* glyph_bitmap(UiFont* font, int face, int glyph, int size_q, int phase) {
    uint64_t key = make_key(glyph, size_q, face, phase);
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (HASH_SIZE - 1);
    while (font->table[h].full) {
        if (font->table[h].full == key) return &font->table[h];
        h = (h + 1) & (HASH_SIZE - 1);
    }
    Face* f = &font->face[face];
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)size_q / 8.0f);
    int x0, y0, x1, y1;
    float shift = (float)phase / SUBPIXEL;
    stbtt_GetGlyphBitmapBoxSubpixel(&f->info, glyph, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    if (bw < 0) bw = 0;
    if (bh < 0) bh = 0;
    if (bw > 250 || bh > 250) { bw = bh = 0; }   // absurd sizes are not cached
    if (font->count > HASH_SIZE / 2) {
        flush_cache(font);
        return glyph_bitmap(font, face, glyph, size_q, phase);
    }
    // shelf packing with a 1 px gutter
    if (font->shelf_x + bw + 1 > ATLAS_W) {
        font->shelf_x = 1;
        font->shelf_y += font->shelf_h + 1;
        font->shelf_h = 0;
    }
    if (font->shelf_y + bh + 1 > ATLAS_H) {
        flush_cache(font);
        return glyph_bitmap(font, face, glyph, size_q, phase);
    }
    h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (HASH_SIZE - 1);
    while (font->table[h].full) h = (h + 1) & (HASH_SIZE - 1);
    Cached* c = &font->table[h];
    c->full = key;
    c->x = (int16_t)font->shelf_x;
    c->y = (int16_t)font->shelf_y;
    c->w = (uint8_t)bw;
    c->h = (uint8_t)bh;
    c->bx = (int8_t)(x0 < -127 ? -127 : (x0 > 127 ? 127 : x0));
    c->by = (int8_t)(y0 < -127 ? -127 : (y0 > 127 ? 127 : y0));
    if (bw && bh) {
        stbtt_MakeGlyphBitmapSubpixel(&f->info, font->atlas + (size_t)c->y * ATLAS_W + c->x, bw, bh,
                                      ATLAS_W, scale, scale, shift, 0.0f, glyph);
        for (int yy = 0; yy < bh; yy++) {
            uint8_t* row = font->atlas + (size_t)(c->y + yy) * ATLAS_W + c->x;
            for (int xx = 0; xx < bw; xx++) row[xx] = font->lut[row[xx]];
        }
    }
    font->shelf_x += bw + 1;
    if (bh > font->shelf_h) font->shelf_h = bh;
    font->count++;
    return c;
}

float ui_text_draw(UiCanvas* cv, UiFont* font, UiWeight wt, float size_px, float x,
                   float baseline_y, UiAlign align, uint32_t argb, const char* s, int len) {
    if (!s) return 0.0f;
    if (len < 0) len = (int)strlen(s);
    float total = ui_text_width(font, wt, size_px, s, len);
    float pen = align == UI_ALIGN_CENTER ? x - total * 0.5f : align == UI_ALIGN_RIGHT ? x - total : x;
    float pen0 = pen;
    int size_q = (int)(size_px * 8.0f + 0.5f);
    if (size_q < 8) size_q = 8;
    int by = (int)floorf(baseline_y + 0.5f);
    int ink_x0 = 1 << 30, ink_y0 = 1 << 30, ink_x1 = -(1 << 30), ink_y1 = -(1 << 30), missing = 0;
    const char* end = s + len;
    const char* p = s;
    uint32_t cp, next;
    if (p >= end) return total;
    p += ui_utf8_decode(p, end, &cp);
    while (1) {
        int n = 0;
        next = 0;
        if (p < end) n = ui_utf8_decode(p, end, &next);
        if (cp >= 0x20) {
            int g, fi;
            float adv = advance_of(font, wt, size_px, cp, next, &g, &fi);
            if (g == 0) missing++;
            if (cp != ' ' && cp != 0xA0) {
                int ix = (int)floorf(pen);
                int phase = (int)((pen - (float)ix) * SUBPIXEL + 0.5f);
                if (phase >= SUBPIXEL) { phase = 0; ix++; }
                const Cached* c = glyph_bitmap(font, fi, g, size_q, phase);
                if (c->w && c->h) {
                    if (ix + c->bx < ink_x0) ink_x0 = ix + c->bx;
                    if (by + c->by < ink_y0) ink_y0 = by + c->by;
                    if (ix + c->bx + c->w > ink_x1) ink_x1 = ix + c->bx + c->w;
                    if (by + c->by + c->h > ink_y1) ink_y1 = by + c->by + c->h;
                }
                for (int yy = 0; yy < c->h; yy++) {
                    const uint8_t* row = font->atlas + (size_t)(c->y + yy) * ATLAS_W + c->x;
                    for (int xx = 0; xx < c->w; xx++) {
                        if (row[xx]) {
                            ui_cv_over(cv, ix + c->bx + xx, by + c->by + yy, argb,
                                       (float)row[xx] * (1.0f / 255.0f));
                        }
                    }
                }
            }
            pen += adv;
        }
        if (!n) break;
        cp = next;
        p += n;
    }
    if (g_trace) {
        UiTextTraceEvent e = { UI_TEXT_TRACE_DRAW, s, len, cv, size_px, pen0, pen0 + total,
                               (float)ink_x0, (float)ink_y0, (float)ink_x1, (float)ink_y1, 0, missing };
        g_trace(&e, g_trace_user);
    }
    return total;
}

const uint8_t* ui_font_atlas(UiFont* font, int* w, int* h, uint32_t* generation) {
    if (w) *w = ATLAS_W;
    if (h) *h = ATLAS_H;
    if (generation) *generation = font->generation;
    return font->atlas;
}

int ui_font_cached_glyphs(UiFont* font) { return font->count; }

// ---- ellipsize, wrap, fit ----------------------------------------------------------

static int copy_out(char* out, size_t cap, const char* a, int alen, const char* b, int blen,
                    const char* c, int clen) {
    if (cap == 0) return 0;
    size_t pos = 0;
    const char* parts[3] = { a, b, c };
    int lens[3] = { alen, blen, clen };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < lens[i] && pos + 1 < cap; j++) out[pos++] = parts[i][j];
    }
    out[pos] = 0;
    return (int)pos;
}

// Byte offsets of the start of each code point, plus the end
static int boundaries(const char* s, int len, int* off, int max) {
    int n = 0;
    const char* p = s;
    const char* end = s + len;
    while (p < end && n < max - 1) {
        off[n++] = (int)(p - s);
        uint32_t cp;
        p += ui_utf8_decode(p, end, &cp);
    }
    off[n] = len;
    return n;
}

int ui_text_ellipsize(UiFont* font, UiWeight w, float size_px, const char* s, float max_w,
                      UiEllipsize mode, char* out, size_t cap, int* truncated) {
    static const char ELL[] = "\xE2\x80\xA6";
    int len = (int)strlen(s);
    if (truncated) *truncated = 0;
    if (ui_text_width(font, w, size_px, s, len) <= max_w) {
        return copy_out(out, cap, s, len, NULL, 0, NULL, 0);
    }
    if (truncated) *truncated = 1;
    if (g_trace) {
        UiTextTraceEvent e = { UI_TEXT_TRACE_CUT, s, len, NULL, size_px, 0, max_w, 0, 0, 0, 0, 1, 0 };
        g_trace(&e, g_trace_user);
    }
    enum { MAXC = 2048 };
    int* off = malloc(sizeof(int) * (MAXC + 1));
    if (!off) return copy_out(out, cap, ELL, 3, NULL, 0, NULL, 0);
    int n = boundaries(s, len, off, MAXC);
    int result = 0;
    if (mode == UI_ELLIPSIZE_MIDDLE) {
        // a third of what is kept from the start, the rest from the end
        int done = 0;
        for (int keep = n - 1; keep > 1; keep--) {
            int head = keep / 3;
            int tail = keep - head;
            int hb = off[head];
            int tb = off[n - tail];
            char tmp[1];
            (void)tmp;
            float wd = ui_text_width(font, w, size_px, s, hb) + ui_text_width(font, w, size_px, ELL, 3) +
                       ui_text_width(font, w, size_px, s + tb, len - tb);
            if (wd <= max_w) {
                result = copy_out(out, cap, s, hb, ELL, 3, s + tb, len - tb);
                done = 1;
                break;
            }
        }
        if (!done) result = copy_out(out, cap, ELL, 3, NULL, 0, NULL, 0);
    } else if (mode == UI_ELLIPSIZE_START) {
        int done = 0;
        for (int keep = n - 1; keep >= 1; keep--) {
            int tb = off[n - keep];
            float wd = ui_text_width(font, w, size_px, ELL, 3) + ui_text_width(font, w, size_px, s + tb, len - tb);
            if (wd <= max_w) {
                result = copy_out(out, cap, ELL, 3, s + tb, len - tb, NULL, 0);
                done = 1;
                break;
            }
        }
        if (!done) result = copy_out(out, cap, ELL, 3, NULL, 0, NULL, 0);
    } else {
        float ew = ui_text_width(font, w, size_px, ELL, 3);
        int end = n;
        while (end > 0 && ui_text_width(font, w, size_px, s, off[end]) + ew > max_w) end--;
        int eb = off[end];
        while (eb > 0 && s[eb - 1] == ' ') eb--;   // trim, as upstream does
        result = copy_out(out, cap, s, eb, ELL, 3, NULL, 0);
    }
    free(off);
    return result;
}

// Largest prefix (bytes) of text[0..len) that fits, at least one code point
static int fit_bytes(UiFont* font, UiWeight w, float size_px, const char* s, int len, float max_w) {
    const char* end = s + len;
    const char* p = s;
    float x = 0.0f;
    int best = 0;
    while (p < end) {
        uint32_t cp, next = 0;
        int n = ui_utf8_decode(p, end, &cp);
        if (p + n < end) ui_utf8_decode(p + n, end, &next);
        float a = cp >= 0x20 ? advance_of(font, w, size_px, cp, next, NULL, NULL) : 0.0f;
        if (x + a > max_w + 0.001f && best > 0) break;
        x += a;
        p += n;
        best = (int)(p - s);
    }
    return best;
}

int ui_text_wrap(UiFont* font, UiWeight w, float size_px, const char* s, float max_w,
                 UiTextLine* lines, int max_lines) {
    int total = 0;
    int len = (int)strlen(s);
    int para = 0;
    while (1) {
        int pend = para;
        while (pend < len && s[pend] != '\n') pend++;
        int rest = para;
        if (pend == para) {
            if (total < max_lines) { lines[total].start = para; lines[total].len = 0; lines[total].width = 0; }
            total++;
        }
        while (rest < pend) {
            int remain = pend - rest;
            int fits = fit_bytes(font, w, size_px, s + rest, remain, max_w);
            int cut, next;
            if (fits >= remain) {
                cut = remain;
                next = pend;
            } else {
                // last space at or before the fitted prefix's end
                int space = -1;
                for (int i = fits; i >= 1; i--) {
                    if (s[rest + i] == ' ' && i <= remain) { space = i; break; }
                }
                if (space > 0) { cut = space; next = rest + space + 1; }
                else { cut = fits; next = rest + fits; }
            }
            if (total < max_lines) {
                lines[total].start = rest;
                lines[total].len = cut;
                lines[total].width = ui_text_width(font, w, size_px, s + rest, cut);
            }
            total++;
            rest = next;
        }
        if (pend >= len) break;
        para = pend + 1;
    }
    return total;
}

float ui_text_fit_size(UiFont* font, UiWeight w, float size_px, float min_px, const char* s,
                       float max_w) {
    float size = size_px;
    while (size > min_px && ui_text_width(font, w, size, s, -1) > max_w) size -= 1.0f;
    return size;
}

// ---- fallback faces (lane wave6-i18n) ---------------------------------------------------------

void ui_font_clear_fallback(UiFont* font) {
    if (!font) return;
    face_free(&font->face[FACE_FALLBACK]);
    face_free(&font->face[FACE_FALLBACK + 1]);
    flush_cache(font);
}

int ui_font_set_fallback(UiFont* font, const char* regular_path, int regular_index, const char* medium_path,
                         int medium_index) {
    if (!font || !regular_path) return 0;
    ui_font_clear_fallback(font);
    if (!face_map(&font->face[FACE_FALLBACK], regular_path, regular_index)) return 0;
    if (medium_path && *medium_path) face_map(&font->face[FACE_FALLBACK + 1], medium_path, medium_index);
    return 1;
}

int ui_font_has_fallback(UiFont* font) { return font && font->face[FACE_FALLBACK].loaded; }

int ui_font_covers(UiFont* font, UiWeight w, const char* s, uint32_t* missing) {
    if (!font || !s) return 0;
    const char* end = s + strlen(s);
    for (const char* p = s; p < end;) {
        uint32_t cp;
        p += ui_utf8_decode(p, end, &cp);
        if (cp < 0x20 || cp == 0xA0) continue;
        int g;
        pick(font, w, cp, &g);
        if (g == 0) {
            if (missing) *missing = cp;
            return 0;
        }
    }
    return 1;
}

// The face of a .ttc whose names say region ("JP", "KR", "SC", "TC"), not a Mono one; -1 for none.
// Name 4 (full name) of the Windows Unicode table, UTF-16BE, read as ASCII.
static int ttc_index_for(const unsigned char* data, const char* region) {
    int n = stbtt_GetNumberOfFonts(data);
    for (int i = 0; i < n && i < 64; i++) {
        stbtt_fontinfo info;
        int off = stbtt_GetFontOffsetForIndex(data, i);
        if (off < 0 || !stbtt_InitFont(&info, data, off)) continue;
        int len = 0;
        const char* name = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                   STBTT_MS_LANG_ENGLISH, 4);
        if (!name) continue;
        char ascii[128];
        int k = 0;
        for (int j = 1; j < len && k < (int)sizeof ascii - 1; j += 2) ascii[k++] = name[j];
        ascii[k] = 0;
        char want[32];
        snprintf(want, sizeof want, "CJK %s", region);
        if (strstr(ascii, want) && !strstr(ascii, "Mono")) return i;
    }
    return -1;
}

typedef struct {
    char path[2][512];   // regular, medium
    int index[2];
    int rank;            // lower is better; 99 none
} CjkPick;

static int str_ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

// One file's place among the candidates for region: 0 a Noto Sans CJK collection, 1 a Noto Sans CJK
// file of the region, 2 the region's own Noto Sans (NotoSansJP), 3 a Noto Sans CJK file of another
// region (every CJK region's font has kana, hangul and the common Han), else -1. *weight 0 regular,
// 1 medium.
static int cjk_rank(const char* name, const char* region, int* weight) {
    static const char* const W[2] = { "Regular", "Medium" };
    static const char* const REG[4] = { "JP", "KR", "SC", "TC" };
    static const char* const EXT[3] = { "ttc", "otf", "ttf" };
    char want[64];
    for (int w = 0; w < 2; w++) {
        *weight = w;
        snprintf(want, sizeof want, "NotoSansCJK-%s.ttc", W[w]);
        if (str_ieq(name, want)) return 0;
        for (int e = 1; e < 3; e++) {
            snprintf(want, sizeof want, "NotoSansCJK%s-%s.%s", region, W[w], EXT[e]);
            if (str_ieq(name, want)) return 1;
            snprintf(want, sizeof want, "NotoSans%s-%s.%s", region, W[w], EXT[e]);
            if (str_ieq(name, want)) return 2;
            for (int r = 0; r < 4; r++) {
                snprintf(want, sizeof want, "NotoSansCJK%s-%s.%s", REG[r], W[w], EXT[e]);
                if (str_ieq(name, want)) return 3;
            }
        }
    }
    return -1;
}

static void cjk_walk(const char* dir, int depth, const char* region, CjkPick* found, int* budget) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && --*budget > 0) {
        if (e->d_name[0] == '.') continue;
        char path[512];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) cjk_walk(path, depth - 1, region, found, budget);
            continue;
        }
        int weight;
        int rank = cjk_rank(e->d_name, region, &weight);
        if (rank < 0) continue;
        // a regular file of each rank, the medium one beside it when there is one
        CjkPick* f = &found[rank];
        snprintf(f->path[weight], sizeof f->path[weight], "%s", path);
        f->rank = rank;
    }
    closedir(d);
}

int ui_font_find_cjk(const char* root, const char* region, char* regular, size_t rcap, int* rindex, char* medium,
                     size_t mcap, int* mindex) {
    if (regular && rcap) regular[0] = 0;
    if (medium && mcap) medium[0] = 0;
    if (rindex) *rindex = 0;
    if (mindex) *mindex = 0;
    if (!root || !region) return 0;
    CjkPick found[4];
    memset(found, 0, sizeof found);
    for (int i = 0; i < 4; i++) found[i].rank = 99;
    int budget = 20000;
    cjk_walk(root, 4, region, found, &budget);
    for (int r = 0; r < 4; r++) {
        CjkPick* f = &found[r];
        if (f->rank != r || !f->path[0][0]) continue;
        int idx[2] = { 0, 0 };
        int ok = 1;
        for (int w = 0; w < 2 && ok; w++) {
            if (!f->path[w][0]) continue;
            Face face;
            if (!face_map(&face, f->path[w], 0)) {
                if (w == 0) ok = 0;
                f->path[w][0] = 0;
                continue;
            }
            if (stbtt_GetNumberOfFonts(face.data) > 1) {
                idx[w] = ttc_index_for(face.data, region);
                if (idx[w] < 0) idx[w] = 0;
            }
            face_free(&face);
        }
        if (!ok) continue;
        if (regular) snprintf(regular, rcap, "%s", f->path[0]);
        if (rindex) *rindex = idx[0];
        if (medium && f->path[1][0]) snprintf(medium, mcap, "%s", f->path[1]);
        if (mindex) *mindex = idx[1];
        return 1;
    }
    return 0;
}

void ui_text_ink_extent(UiFont* font, UiWeight w, float size_px, const char* s, float* above, float* below) {
    float up = 0.0f, down = 0.0f;
    const char* end = s ? s + strlen(s) : NULL;
    for (const char* p = s; p && p < end;) {
        uint32_t cp;
        p += ui_utf8_decode(p, end, &cp);
        if (cp <= 0x20 || cp == 0xA0) continue;
        int g;
        Face* f = &font->face[pick(font, w, cp, &g)];
        int x0, y0, x1, y1;
        if (!stbtt_GetGlyphBox(&f->info, g, &x0, &y0, &x1, &y1)) continue;
        float sc = stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
        if ((float)y1 * sc > up) up = (float)y1 * sc;
        if ((float)-y0 * sc > down) down = (float)-y0 * sc;
    }
    if (above) *above = up;
    if (below) *below = down;
}
