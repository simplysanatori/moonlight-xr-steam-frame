// See text.h. GPLv3.
#include "text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    int ascent, descent, line_gap;   // font units
    int loaded;
} Face;

typedef struct {
    uint32_t key;      // 0 = empty; else packed (glyph, size_q, weight, phase) + 1
    uint64_t full;     // full key for compare
    int16_t x, y;      // atlas position
    uint8_t w, h;
    int8_t bx, by;     // bitmap offset from the pen origin at the baseline
} Cached;

struct UiFont {
    Face face[2];
    uint8_t* atlas;
    Cached* table;
    int count;
    int shelf_x, shelf_y, shelf_h;
    uint32_t generation;
    uint8_t lut[256];
};

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

// Advance of cp followed by next (0 for none), in pixels
static float advance_of(Face* f, float scale, uint32_t cp, uint32_t next, int* glyph_out) {
    int g = glyph_of(f, cp);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
    float a = (float)adv;
    if (next) {
        int gn = glyph_of(f, next);
        a += (float)stbtt_GetGlyphKernAdvance(&f->info, g, gn);
    }
    if (glyph_out) *glyph_out = g;
    return a * scale;
}

float ui_text_width(UiFont* font, UiWeight w, float size_px, const char* s, int len) {
    if (!s) return 0.0f;
    if (len < 0) len = (int)strlen(s);
    Face* f = face_of(font, w);
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
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
        if (cp >= 0x20) x += advance_of(f, scale, cp, next, NULL);
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

static uint64_t make_key(int glyph, int size_q, int weight, int phase) {
    return ((uint64_t)(uint32_t)glyph << 24) | ((uint64_t)(uint32_t)size_q << 4) |
           ((uint64_t)weight << 3) | (uint64_t)phase | (1ull << 62);
}

static const Cached* glyph_bitmap(UiFont* font, UiWeight wt, int glyph, int size_q, int phase) {
    int weight = wt == UI_WEIGHT_MEDIUM ? 1 : 0;
    uint64_t key = make_key(glyph, size_q, weight, phase);
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40) & (HASH_SIZE - 1);
    while (font->table[h].full) {
        if (font->table[h].full == key) return &font->table[h];
        h = (h + 1) & (HASH_SIZE - 1);
    }
    Face* f = face_of(font, wt);
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
        return glyph_bitmap(font, wt, glyph, size_q, phase);
    }
    // shelf packing with a 1 px gutter
    if (font->shelf_x + bw + 1 > ATLAS_W) {
        font->shelf_x = 1;
        font->shelf_y += font->shelf_h + 1;
        font->shelf_h = 0;
    }
    if (font->shelf_y + bh + 1 > ATLAS_H) {
        flush_cache(font);
        return glyph_bitmap(font, wt, glyph, size_q, phase);
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
    Face* f = face_of(font, wt);
    int size_q = (int)(size_px * 8.0f + 0.5f);
    if (size_q < 8) size_q = 8;
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
    int by = (int)floorf(baseline_y + 0.5f);
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
            int g;
            float adv = advance_of(f, scale, cp, next, &g);
            if (cp != ' ' && cp != 0xA0) {
                int ix = (int)floorf(pen);
                int phase = (int)((pen - (float)ix) * SUBPIXEL + 0.5f);
                if (phase >= SUBPIXEL) { phase = 0; ix++; }
                const Cached* c = glyph_bitmap(font, wt, g, size_q, phase);
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
    Face* f = face_of(font, w);
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, size_px);
    const char* end = s + len;
    const char* p = s;
    float x = 0.0f;
    int best = 0;
    while (p < end) {
        uint32_t cp, next = 0;
        int n = ui_utf8_decode(p, end, &cp);
        if (p + n < end) ui_utf8_decode(p + n, end, &next);
        float a = cp >= 0x20 ? advance_of(f, scale, cp, next, NULL) : 0.0f;
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
