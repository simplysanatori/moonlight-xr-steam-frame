// Text: stable metrics, UTF-8, ellipsize, wrap, alignment, atlas cache. Uses the shipped Noto Sans.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "text.h"

static UiFont* font;

static void test_metrics(void) {
    // Golden advances from assets/fonts/NotoSans-{Regular,Medium}.ttf (2.013) with GPOS kerning
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Distance", -1), 89.122, 0.01);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_MEDIUM, 22, "Distance", -1), 91.036, 0.01);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Moonlight XR", -1), 138.886, 0.01);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "The quick brown fox", -1), 208.516, 0.01);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 40, "Gamepad mode", -1), 302.32, 0.01);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "", -1), 0.0, 0.0);
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "\xC3\x89t\xC3\xA9 \xE2\x80\xA6", -1), 55.704, 0.01);
    // explicit length, and a NUL terminated string agree
    CHECK_NEAR(ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Distance and more", 8), 89.122, 0.01);
    // widths scale with the size (no hinting)
    double a = ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Convergence", -1);
    double b = ui_text_width(font, UI_WEIGHT_REGULAR, 44, "Convergence", -1);
    CHECK_NEAR(b / a, 2.0, 0.001);
    UiFontMetrics m;
    ui_font_metrics(font, UI_WEIGHT_REGULAR, 100, &m);
    CHECK_NEAR(m.ascent, 106.9, 0.05);
    CHECK_NEAR(m.descent, 29.3, 0.05);
    CHECK_NEAR(m.cap_height, 71.4, 0.05);
    CHECK_NEAR(m.x_height, 53.6, 0.05);
    CHECK(m.line_height >= m.ascent + m.descent);
    // medium is not narrower than regular for the same string
    CHECK(ui_text_width(font, UI_WEIGHT_MEDIUM, 22, "Distance", -1) > ui_text_width(font, UI_WEIGHT_REGULAR, 22, "Distance", -1));
    // glyph coverage the panels rely on
    CHECK(ui_font_has(font, UI_WEIGHT_REGULAR, 0xB0));     // degree sign
    CHECK(ui_font_has(font, UI_WEIGHT_REGULAR, 0x2026));   // ellipsis
    CHECK(ui_font_has(font, UI_WEIGHT_REGULAR, 0xB7));     // middle dot
    CHECK(ui_font_has(font, UI_WEIGHT_REGULAR, 'A'));
    CHECK(!ui_font_has(font, UI_WEIGHT_REGULAR, 0x2191));  // arrows are drawn as shapes
}

static void test_utf8(void) {
    uint32_t cp;
    const char* s = "\xC3\xA9";
    CHECK(ui_utf8_decode(s, s + 2, &cp) == 2 && cp == 0xE9);
    s = "A";
    CHECK(ui_utf8_decode(s, s + 1, &cp) == 1 && cp == 'A');
    s = "\xF0\x9F\x98\x80";
    CHECK(ui_utf8_decode(s, s + 4, &cp) == 4 && cp == 0x1F600);
    s = "\xE2\x82\xAC";
    CHECK(ui_utf8_decode(s, s + 3, &cp) == 3 && cp == 0x20AC);
    // malformed: one replacement character per bad byte, never past the end
    s = "\xFF";
    CHECK(ui_utf8_decode(s, s + 1, &cp) == 1 && cp == 0xFFFD);
    s = "\xE2\x80";
    CHECK(ui_utf8_decode(s, s + 2, &cp) == 1 && cp == 0xFFFD);    // truncated
    s = "\xC0\x80";
    CHECK(ui_utf8_decode(s, s + 2, &cp) == 1 && cp == 0xFFFD);    // overlong
    s = "\xED\xA0\x80";
    CHECK(ui_utf8_decode(s, s + 3, &cp) == 1 && cp == 0xFFFD);    // surrogate
    s = "\x80";
    CHECK(ui_utf8_decode(s, s + 1, &cp) == 1 && cp == 0xFFFD);    // stray continuation
    CHECK(ui_utf8_decode(s, s, &cp) == 0);
    static const uint32_t SAMPLES[] = { 0x24, 0x7F, 0x80, 0xE9, 0x7FF, 0x800, 0x2026, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF };
    for (size_t i = 0; i < sizeof SAMPLES / sizeof SAMPLES[0]; i++) {
        char out[4];
        int n = ui_utf8_encode(SAMPLES[i], out);
        uint32_t back;
        CHECK(ui_utf8_decode(out, out + n, &back) == n && back == SAMPLES[i]);
    }
    CHECK(ui_utf8_count("h\xC3\xA9llo", -1) == 5);
    CHECK(ui_utf8_count("\xFF\xFF", -1) == 2);
    // measuring and drawing malformed input is safe
    ui_text_width(font, UI_WEIGHT_REGULAR, 22, "\xFF\xE2\x80 bad \xC0\x80", -1);
}

static void test_ellipsize(void) {
    char out[256];
    int cut;
    const char* text = "Sends your message, the headset and build details, your settings";
    const float max_w = 300.0f;
    float full = ui_text_width(font, UI_WEIGHT_REGULAR, 22, text, -1);
    CHECK(full > max_w);
    for (int mode = 0; mode < 3; mode++) {
        int n = ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, text, max_w, (UiEllipsize)mode, out, sizeof out, &cut);
        CHECK(cut == 1);
        CHECK(n == (int)strlen(out));
        CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 22, out, -1) <= max_w);
        CHECK(strstr(out, "\xE2\x80\xA6") != NULL);
        // as long as it can be: one more character would not fit
        CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 22, out, -1) > max_w * 0.85f);
        if (mode == UI_ELLIPSIZE_END) CHECK(strncmp(out, "Sends", 5) == 0 && strcmp(out + n - 3, "\xE2\x80\xA6") == 0);
        if (mode == UI_ELLIPSIZE_START) CHECK(strncmp(out, "\xE2\x80\xA6", 3) == 0 && strcmp(out + n - 8, "settings") == 0);
        if (mode == UI_ELLIPSIZE_MIDDLE) CHECK(strncmp(out, "Se", 2) == 0 && strcmp(out + n - 4, "ings") == 0);
    }
    // fits: unchanged, not truncated
    int n = ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, "Short", 300, UI_ELLIPSIZE_END, out, sizeof out, &cut);
    CHECK(cut == 0 && n == 5 && strcmp(out, "Short") == 0);
    // nothing fits: just the ellipsis
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, text, 5, UI_ELLIPSIZE_END, out, sizeof out, &cut);
    CHECK(strcmp(out, "\xE2\x80\xA6") == 0);
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, text, 5, UI_ELLIPSIZE_MIDDLE, out, sizeof out, &cut);
    CHECK(strcmp(out, "\xE2\x80\xA6") == 0);
    // a trailing space is trimmed before the ellipsis, as upstream does
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, "aaaa bbbb cccc dddd", ui_text_width(font, 0, 22, "aaaa bbbb \xE2\x80\xA6", -1) + 1, UI_ELLIPSIZE_END, out, sizeof out, &cut);
    CHECK(strstr(out, " \xE2\x80\xA6") == NULL);
    // a tiny output buffer is cut, never overrun
    char small[6];
    memset(small, 'x', sizeof small);
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, text, max_w, UI_ELLIPSIZE_END, small, sizeof small, &cut);
    CHECK(small[5] == 0 || memchr(small, 0, sizeof small) != NULL);
    CHECK(strlen(small) < sizeof small);
    char one[1];
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, text, max_w, UI_ELLIPSIZE_MIDDLE, one, 1, &cut);
    CHECK(one[0] == 0);
    // multi-byte characters are never cut in half
    ui_text_ellipsize(font, UI_WEIGHT_REGULAR, 22, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 60, UI_ELLIPSIZE_END, out, sizeof out, &cut);
    CHECK(ui_utf8_count(out, -1) == (int)ui_utf8_count(out, -1));
    for (const char* p = out; *p;) {
        uint32_t cp;
        int l = ui_utf8_decode(p, p + strlen(p), &cp);
        CHECK(cp != 0xFFFD);
        p += l;
    }
}

static void test_wrap(void) {
    const char* text = "Pinch your thumb and index finger three times quickly to lock your hands out";
    UiTextLine lines[16];
    int n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, text, 300, lines, 16);
    CHECK(n >= 3 && n <= 6);
    int covered = 0;
    for (int i = 0; i < n; i++) {
        CHECK(lines[i].width <= 300.0f + 0.01f);
        CHECK(lines[i].len > 0);
        CHECK_NEAR(lines[i].width, ui_text_width(font, UI_WEIGHT_REGULAR, 22, text + lines[i].start, lines[i].len), 0.01);
        // breaks at spaces here
        if (i + 1 < n) CHECK(text[lines[i].start + lines[i].len] == ' ');
        covered += lines[i].len + 1;
    }
    CHECK(covered - 1 == (int)strlen(text));
    // the count is the need even when the caller's array is short
    UiTextLine two[2];
    CHECK(ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, text, 300, two, 2) == n);
    // line breaks, empty paragraphs, trailing newline
    n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, "one\n\ntwo\n", 500, lines, 16);
    CHECK(n == 4);
    CHECK(lines[0].len == 3 && lines[1].len == 0 && lines[2].len == 3 && lines[3].len == 0);
    n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, "", 500, lines, 16);
    CHECK(n == 1 && lines[0].len == 0);
    // no spaces: broken between characters, nothing lost, each at least one code point
    const char* longword = "Supercalifragilisticexpialidocious";
    n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, longword, 120, lines, 16);
    CHECK(n >= 3);
    int total = 0;
    for (int i = 0; i < n; i++) { CHECK(lines[i].width <= 120.01f); total += lines[i].len; }
    CHECK(total == (int)strlen(longword));
    // a width narrower than one glyph still makes progress
    n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, "WWW", 1, lines, 16);
    CHECK(n == 3);
    // multi-byte text wraps on whole code points
    n = ui_text_wrap(font, UI_WEIGHT_REGULAR, 22, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 40, lines, 16);
    CHECK(n >= 2);
    for (int i = 0; i < n; i++) CHECK(lines[i].len % 2 == 0);
    // fit_size
    float sz = ui_text_fit_size(font, UI_WEIGHT_REGULAR, 21, 15, "Grand Cinema Extra Long Name", 100);
    CHECK(sz == 15.0f);
    sz = ui_text_fit_size(font, UI_WEIGHT_REGULAR, 21, 15, "Synthwave", 400);
    CHECK(sz == 21.0f);
}

typedef struct { int x0, y0, x1, y1; } Box;

static Box ink_box(const uint8_t* px, int w, int h) {
    Box b = { w, h, -1, -1 };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[((size_t)y * w + x) * 4 + 3] > 24) {
                if (x < b.x0) b.x0 = x;
                if (x > b.x1) b.x1 = x;
                if (y < b.y0) b.y0 = y;
                if (y > b.y1) b.y1 = y;
            }
    return b;
}

static void test_draw(void) {
    enum { W = 400, H = 80 };
    uint8_t* px = calloc(W * H, 4);
    UiCanvas cv;
    ui_cv_init(&cv, px, W, H);
    const char* s = "Alignment Test";
    float wd = ui_text_width(font, UI_WEIGHT_REGULAR, 24, s, -1);
    float adv = ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 24, 100, 50, UI_ALIGN_LEFT, UI_WHITE, s, -1);
    CHECK_NEAR(adv, wd, 0.001);
    Box l = ink_box(px, W, H);
    CHECK(l.x0 >= 98 && l.x0 <= 104);
    CHECK(l.x1 <= 100 + (int)wd + 2);
    CHECK(l.x1 >= 100 + (int)wd - 8);
    // ink above the baseline by about the cap height, below by descenders
    UiFontMetrics m;
    ui_font_metrics(font, UI_WEIGHT_REGULAR, 24, &m);
    CHECK(l.y0 >= 50 - (int)ceilf(m.ascent) - 1 && l.y0 <= 50 - (int)m.cap_height + 1);
    CHECK(l.y1 >= 50 && l.y1 <= 50 + (int)ceilf(m.descent));

    memset(px, 0, W * H * 4);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 24, 200, 50, UI_ALIGN_CENTER, UI_WHITE, s, -1);
    Box c = ink_box(px, W, H);
    CHECK(abs((c.x0 + c.x1) / 2 - 200) <= 4);
    memset(px, 0, W * H * 4);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 24, 300, 50, UI_ALIGN_RIGHT, UI_WHITE, s, -1);
    Box r = ink_box(px, W, H);
    CHECK(r.x1 <= 302 && r.x1 >= 292);
    CHECK(r.x0 >= 300 - (int)wd - 4);

    // premultiplied output: colour channels never exceed alpha
    int bad = 0;
    for (int i = 0; i < W * H; i++) {
        const uint8_t* p = px + i * 4;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) bad++;
    }
    CHECK(bad == 0);

    // drawing off every edge and into a 1x1 canvas does not touch memory it does not own
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 60, -30, 20, UI_ALIGN_LEFT, UI_WHITE, "Clipped", -1);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 60, W - 10, H + 20, UI_ALIGN_LEFT, UI_WHITE, "Clipped", -1);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 500, 0, 40, UI_ALIGN_LEFT, UI_WHITE, "Huge", -1);   // beyond the cacheable size
    uint8_t one[4] = { 0 };
    UiCanvas tiny;
    ui_cv_init(&tiny, one, 1, 1);
    ui_text_draw(&tiny, font, UI_WEIGHT_REGULAR, 20, 0, 1, UI_ALIGN_LEFT, UI_WHITE, "tiny", -1);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 22, 10, 30, UI_ALIGN_LEFT, UI_WHITE, "\xFF\xE2\x80 bad \xC0\x80 text", -1);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 22, 10, 30, UI_ALIGN_LEFT, UI_WHITE, NULL, -1);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 22, 10, 30, UI_ALIGN_LEFT, UI_WHITE, "", 0);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 0.1f, 10, 30, UI_ALIGN_LEFT, UI_WHITE, "zero", -1);

    // clip rectangle is honoured
    memset(px, 0, W * H * 4);
    ui_cv_set_clip(&cv, 150, 0, 220, H);
    ui_text_draw(&cv, font, UI_WEIGHT_REGULAR, 24, 100, 50, UI_ALIGN_LEFT, UI_WHITE, s, -1);
    Box k = ink_box(px, W, H);
    CHECK(k.x0 >= 150 && k.x1 < 220);
    free(px);
}

static void test_atlas(void) {
    enum { W = 300, H = 60 };
    uint8_t* a = calloc(W * H, 4);
    uint8_t* b = calloc(W * H, 4);
    UiCanvas ca, cb;
    ui_cv_init(&ca, a, W, H);
    ui_cv_init(&cb, b, W, H);
    ui_text_draw(&ca, font, UI_WEIGHT_REGULAR, 26.5f, 12.3f, 40, UI_ALIGN_LEFT, 0xFFCCDDEE, "Cache hit == miss", -1);
    int cached = ui_font_cached_glyphs(font);
    CHECK(cached > 0);
    uint32_t gen0;
    int aw, ah;
    const uint8_t* atlas = ui_font_atlas(font, &aw, &ah, &gen0);
    CHECK(atlas != NULL && aw >= 512 && ah >= 512);
    ui_text_draw(&cb, font, UI_WEIGHT_REGULAR, 26.5f, 12.3f, 40, UI_ALIGN_LEFT, 0xFFCCDDEE, "Cache hit == miss", -1);
    CHECK(ui_font_cached_glyphs(font) == cached);   // the second draw added nothing
    CHECK(memcmp(a, b, W * H * 4) == 0);
    // Fill the atlas past capacity with distinct sizes: it flushes instead of overflowing
    uint8_t* big = calloc(200 * 200, 4);
    UiCanvas cbig;
    ui_cv_init(&cbig, big, 200, 200);
    for (int size = 20; size < 90; size++) {
        for (int c = 0; c < 2; c++) {
            ui_text_draw(&cbig, font, c ? UI_WEIGHT_MEDIUM : UI_WEIGHT_REGULAR, (float)size + 0.25f * (float)(size & 3), 10.0f + 0.3f * (float)size, 100,
                         UI_ALIGN_LEFT, UI_WHITE, "AbcdefghijklmnopqrstuvwxyzQWERTYUIOP", -1);
        }
    }
    uint32_t gen1;
    ui_font_atlas(font, NULL, NULL, &gen1);
    CHECK(gen1 > gen0);   // flushed at least once
    memset(a, 0, W * H * 4);
    ui_text_draw(&ca, font, UI_WEIGHT_REGULAR, 26.5f, 12.3f, 40, UI_ALIGN_LEFT, 0xFFCCDDEE, "Cache hit == miss", -1);
    CHECK(memcmp(a, b, W * H * 4) == 0);   // re-rasterised after the flush, identical
    free(big);
    free(a);
    free(b);
}

static void test_default_font(void) {
    UiFont* d = ui_font_default();
    CHECK(d != NULL);
    CHECK(ui_font_default() == d);
    CHECK_NEAR(ui_text_width(d, UI_WEIGHT_REGULAR, 22, "Distance", -1), 89.122, 0.01);
    CHECK(ui_font_open("/nonexistent/font.ttf", NULL) == NULL);
}

int main(void) {
    font = ui_font_open(MLXR_ASSETS_DIR "/fonts/NotoSans-Regular.ttf", MLXR_ASSETS_DIR "/fonts/NotoSans-Medium.ttf");
    CHECK(font != NULL);
    if (!font) return checksDone("ui_text");
    test_metrics();
    test_utf8();
    test_ellipsize();
    test_wrap();
    test_draw();
    test_atlas();
    test_default_font();
    ui_font_close(font);
    return checksDone("ui_text");
}
