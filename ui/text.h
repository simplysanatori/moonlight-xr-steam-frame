// Text: measuring, wrapping, ellipsizing and drawing UTF-8 with one font
// family (Noto Sans Regular and Medium) through stb_truetype. Glyphs are
// rasterised at the exact pixel size and a quarter-pixel horizontal phase into
// one 8-bit alpha atlas that is cached, so drawing is a blend of cached
// coverage and measuring is plain advance arithmetic (kerning from the `kern`
// table). Sizes are the em size in pixels, which is what Android's
// Paint.setTextSize takes. GPLv3.
#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "canvas.h"

typedef struct UiFont UiFont;

typedef enum { UI_WEIGHT_REGULAR = 0, UI_WEIGHT_MEDIUM = 1 } UiWeight;
typedef enum { UI_ALIGN_LEFT = 0, UI_ALIGN_CENTER = 1, UI_ALIGN_RIGHT = 2 } UiAlign;
typedef enum { UI_ELLIPSIZE_END = 0, UI_ELLIPSIZE_MIDDLE = 1, UI_ELLIPSIZE_START = 2 } UiEllipsize;

// Opens the two files; medium may be NULL (regular stands in). NULL on failure.
UiFont* ui_font_open(const char* regular_path, const char* medium_path);
// Looks for assets/fonts/NotoSans-{Regular,Medium}.ttf in $MLXR_FONT_DIR,
// $MLXR_ASSETS_DIR/fonts, next to the executable (../assets/fonts,
// ./assets/fonts), the compile-time MLXR_ASSETS_DIR, then
// /usr/share/fonts/noto. A shared, lazily created instance.
UiFont* ui_font_default(void);
void ui_font_close(UiFont* font);

typedef struct {
    float ascent;       // above the baseline, positive
    float descent;      // below the baseline, positive
    float line_gap;
    float line_height;  // ascent + descent + line_gap
    float cap_height;   // height of a capital H
    float x_height;
} UiFontMetrics;
void ui_font_metrics(UiFont* font, UiWeight w, float size_px, UiFontMetrics* out);

// Decodes one code point; returns bytes consumed (>= 1 when s < end). A bad
// sequence yields U+FFFD and consumes one byte.
int ui_utf8_decode(const char* s, const char* end, uint32_t* cp);
// Encodes a code point (up to 4 bytes); returns the length.
int ui_utf8_encode(uint32_t cp, char out[4]);
int ui_utf8_count(const char* s, int len);

// Advance width of a UTF-8 string in pixels. len < 0 means NUL terminated.
float ui_text_width(UiFont* font, UiWeight w, float size_px, const char* s, int len);
// True if the font has a glyph for the code point
int ui_font_has(UiFont* font, UiWeight w, uint32_t cp);

// Draws with the baseline at baseline_y and the pen at x (left), the centre
// of the text at x (centre) or its end at x (right). Returns the advance.
float ui_text_draw(UiCanvas* cv, UiFont* font, UiWeight w, float size_px, float x,
                   float baseline_y, UiAlign align, uint32_t argb, const char* s, int len);
// The baseline that centres a line vertically on cy the way the upstream
// panels do (cy - (ascent + descent) / 2 with Android's signed ascent)
float ui_text_baseline_centered(UiFont* font, UiWeight w, float size_px, float cy);

// Cuts the string to fit max_w with an ellipsis (U+2026). Writes a
// NUL-terminated string into out (cap bytes) and returns its length; sets
// *truncated when it was cut. Whole code points only.
int ui_text_ellipsize(UiFont* font, UiWeight w, float size_px, const char* s, float max_w,
                      UiEllipsize mode, char* out, size_t cap, int* truncated);

// Line breaking: at '\n', then at the last space that fits, else between
// characters (which is how a language without spaces wraps).
typedef struct {
    int start;     // byte offset of the line in the source string
    int len;       // bytes in the line (without the break or the space it broke at)
    float width;   // advance width
} UiTextLine;
// Fills up to max_lines; returns the number of lines the whole text needs,
// which may exceed max_lines.
int ui_text_wrap(UiFont* font, UiWeight w, float size_px, const char* s, float max_w,
                 UiTextLine* lines, int max_lines);

// Shrinks the size by 1 px steps (not below min_px) until the text fits max_w;
// returns the size chosen. Upstream does this for long tile names.
float ui_text_fit_size(UiFont* font, UiWeight w, float size_px, float min_px, const char* s,
                       float max_w);

// The glyph atlas, for tests and for a GPU path later: 8-bit coverage, rows
// top down. *generation changes every time the cache is flushed.
const uint8_t* ui_font_atlas(UiFont* font, int* w, int* h, uint32_t* generation);
int ui_font_cached_glyphs(UiFont* font);

// ---- other scripts and the test hook (lane wave6-i18n, PLAN 6.1) ----------------------------------
// A fallback face for code points the family lacks: Chinese, Japanese and Korean from the system's
// Noto Sans CJK (not bundled). Mapped, not read. index picks a face in a .ttc collection; medium may
// be NULL (regular stands in). Replaces an earlier fallback and empties the glyph cache. 0 on failure.
int ui_font_set_fallback(UiFont* font, const char* regular_path, int regular_index, const char* medium_path,
                         int medium_index);
void ui_font_clear_fallback(UiFont* font);
int ui_font_has_fallback(UiFont* font);
// 1 when every code point of s has a glyph in the family or the fallback; else 0 and the first
// one missing in *missing
int ui_font_covers(UiFont* font, UiWeight w, const char* s, uint32_t* missing);
// Looks under root (/usr/share/fonts; four levels down) for Noto Sans CJK for a region ("JP", "KR",
// "SC", "TC"): a NotoSansCJK-Regular.ttc collection (the face of that region), else
// NotoSansCJK<region>-Regular.otf, else NotoSans<region>-Regular.otf, else another region's
// NotoSansCJK file. The -Medium file beside it when there is one (medium[0] = 0 when not).
// 1 when found.
int ui_font_find_cjk(const char* root, const char* region, char* regular, size_t rcap, int* rindex, char* medium,
                     size_t mcap, int* mindex);

// How far the glyphs of s reach above and below the baseline at size_px (from the glyphs' boxes,
// positive both), for a line laid out close to another one
void ui_text_ink_extent(UiFont* font, UiWeight w, float size_px, const char* s, float* above, float* below);

// Every ui_text_draw (DRAW: the text, the pen's extent and the box of what it inked, in canvas
// pixels, unclipped; ink_x0 > ink_x1 when nothing inked; missing counts code points drawn as the
// .notdef box) and every ui_text_ellipsize that had to cut (CUT: the whole text, pen_x1 the width it
// had) goes to fn while it is set. For tests (tests/ui/test_i18n_panels.c); NULL turns it off.
typedef enum { UI_TEXT_TRACE_DRAW = 0, UI_TEXT_TRACE_CUT = 1 } UiTextTraceKind;
typedef struct {
    int kind;
    const char* text;
    int len;
    const UiCanvas* cv;
    float size_px;
    float pen_x0, pen_x1;
    float ink_x0, ink_y0, ink_x1, ink_y1;
    int truncated;
    int missing;
} UiTextTraceEvent;
typedef void (*UiTextTraceFn)(const UiTextTraceEvent* e, void* user);
void ui_text_set_trace(UiTextTraceFn fn, void* user);

#endif
