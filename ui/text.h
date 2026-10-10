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

#endif
