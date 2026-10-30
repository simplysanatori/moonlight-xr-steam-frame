// Moonlight XR Native: a synthetic 2560x1440 desktop for the text clarity
// measurement (tests/shaders, group "text"): small text in the shapes a
// Windows desktop shows, edges and line grids to measure, in 4 x 4 tiles of
// 640 x 360. Values are gamma encoded RGB, 0..1, as the host's frame holds
// them before its 4:2:0 encode. GPLv3.
#pragma once

#define TXS_W 2560
#define TXS_H 1440
#define TXS_TILE_W 640
#define TXS_TILE_H 360

enum {
    TXS_TEXT_BW,      // black text on white, em 12 to 16 px (cap height 9 to 12)
    TXS_TEXT_WD,      // white text on a dark grey (a dark theme)
    TXS_TEXT_CT,      // black on white with ClearType's coloured fringes
    TXS_TEXT_CODE,    // syntax coloured text on a dark editor background
    TXS_EDGE_V,       // a near vertical edge, 5 degrees off, 0.1 | 0.9: horizontal MTF
    TXS_EDGE_H,       // the same, near horizontal: vertical MTF
    TXS_LINES_V,      // 1 px black lines on white, periods 2, 3, 4, 6 in four columns of 160
    TXS_LINES_H,      // the same, horizontal, in four rows of 90
    TXS_ZONE,         // a zone plate up to the frame's Nyquist: aliasing
    TXS_TEXT_RED,     // red text on white (top) and blue links on white (bottom)
    TXS_TEXT_CT_WD,   // ClearType white on dark
    TXS_GRID,         // a 1 px grid, every 4 px, black on white
    TXS_TEXT_BW2,     // more black on white text (rows 3, all four tiles)
    TXS_TILES = 16
};

#define TXS_EDGE_TAN 0.0874887f // tan(5 degrees)
#define TXS_EDGE_LO 0.1f
#define TXS_EDGE_HI 0.9f

// Tile t's rectangle in frame pixels
static inline void txs_tile(int t, int* x, int* y) {
    *x = (t % 4) * TXS_TILE_W;
    *y = (t / 4) * TXS_TILE_H;
}

// The line period of a lines tile's column (vertical) or row (horizontal), i 0..3
static inline int txs_period(int i) {
    static const int p[4] = {2, 3, 4, 6};
    return p[i & 3];
}

// Fills rgb (TXS_W * TXS_H * 3 floats). Glyphs are Noto Sans from fontPath
// through stb_truetype; 0 on success, -1 when the font cannot be read.
int txs_build(float* rgb, const char* fontPath);

// The tone desktop (lane polish-3: does Sharpen move brightness or colour?):
// row 0 eight flat colour patches of 320 x 360, rows 1 and 2 a photo-like
// smooth gradient with soft blobs of colour, row 3 the same with fine
// photo-like texture over it (2 to 12 px detail)
enum { TXS_TONE_FLAT, TXS_TONE_GRADIENT, TXS_TONE_TEXTURE, TXS_TONE_CLASSES };
void txs_build_tone(float* rgb);
// Which of the classes above a desktop position is in, -1 within margin of a
// patch or band edge
int txs_tone_class(float sx, float sy, float margin);
