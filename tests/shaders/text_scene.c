// Moonlight XR Native: the synthetic desktop of text_scene.h. GPLv3.
#include "text_scene.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct {
    stbtt_fontinfo info;
    unsigned char* data;
} Font;

static uint32_t rng = 0x2545f491u;
static uint32_t nextRand(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void put(float* rgb, int x, int y, const float c[3]) {
    if (x < 0 || y < 0 || x >= TXS_W || y >= TXS_H) return;
    float* p = rgb + ((size_t)y * TXS_W + x) * 3;
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
}

static void fillTile(float* rgb, int t, const float c[3]) {
    int x0, y0;
    txs_tile(t, &x0, &y0);
    for (int y = y0; y < y0 + TXS_TILE_H; y++)
        for (int x = x0; x < x0 + TXS_TILE_W; x++) put(rgb, x, y, c);
}

// One line of text at three times the horizontal resolution: coverage per
// sub pixel, glyphs at their fractional pen positions
static void lineCoverage(Font* f, float em, const char* s, float* cov3, int w3, int h, float baseline) {
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, em);
    float pen = 2.0f;
    for (const char* c = s; *c; c++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f->info, *c, &adv, &lsb);
        float px3 = pen * 3.0f;
        float shift = px3 - floorf(px3);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&f->info, *c, scale * 3.0f, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            unsigned char* g = malloc((size_t)gw * gh);
            stbtt_MakeCodepointBitmapSubpixel(&f->info, g, gw, gh, gw, scale * 3.0f, scale, shift, 0.0f, *c);
            int ox = (int)floorf(px3) + x0, oy = (int)lroundf(baseline) + y0;
            for (int y = 0; y < gh; y++)
                for (int x = 0; x < gw; x++) {
                    int X = ox + x, Y = oy + y;
                    if (X < 0 || Y < 0 || X >= w3 || Y >= h) continue;
                    float v = cov3[(size_t)Y * w3 + X] + g[y * gw + x] / 255.0f;
                    cov3[(size_t)Y * w3 + X] = v > 1.0f ? 1.0f : v;
                }
            free(g);
        }
        pen += adv * scale;
        if (pen * 3.0f >= w3 - 3 * em) break;
    }
}

// Random words, 2 to 9 letters, a capital now and then
static void words(char* out, int n) {
    int i = 0;
    while (i < n - 12) {
        int len = 2 + (int)(nextRand() % 8);
        for (int k = 0; k < len; k++) {
            int cap = k == 0 && nextRand() % 5 == 0;
            out[i++] = (char)((cap ? 'A' : 'a') + nextRand() % 26);
        }
        if (nextRand() % 9 == 0) out[i++] = nextRand() % 2 ? ',' : '.';
        out[i++] = ' ';
    }
    out[i] = 0;
}

// A tile of text lines, em sizes 12, 13, 14 and 16 px in turn (Noto's cap
// height is 0.714 em: 8.6 to 11.4 px; Windows' 9 pt UI text is 12 px).
// ink and paper are gamma encoded colours; ink may change per line (code).
// clearType: each channel takes its own third of a pixel (R left), through
// ClearType's 1 2 3 2 1 filter; else the three thirds averaged (grey AA).
static void textTile(float* rgb, Font* f, int t, const float paper[3], const float (*inks)[3], int nInks,
                     int clearType) {
    fillTile(rgb, t, paper);
    int x0, y0;
    txs_tile(t, &x0, &y0);
    const float ems[4] = {12.0f, 13.0f, 14.0f, 16.0f};
    int w3 = TXS_TILE_W * 3;
    float y = 4.0f;
    int line = 0;
    char buf[400];
    while (1) {
        float em = ems[line % 4];
        int lh = (int)ceilf(em * 1.35f);
        if (y + lh > TXS_TILE_H - 2) break;
        float* cov3 = calloc((size_t)w3 * lh, sizeof(float));
        words(buf, sizeof(buf));
        lineCoverage(f, em, buf, cov3, w3, lh, em * 1.02f);
        const float* ink = inks[line % nInks];
        for (int yy = 0; yy < lh; yy++)
            for (int x = 0; x < TXS_TILE_W; x++) {
                float c[3];
                float a[3];
                const float* r = cov3 + (size_t)yy * w3;
                if (clearType) {
                    for (int ch = 0; ch < 3; ch++) {
                        int s = 3 * x + ch;
                        float sum = 0.0f;
                        const float k[5] = {1, 2, 3, 2, 1};
                        for (int j = -2; j <= 2; j++) {
                            int q = s + j;
                            if (q >= 0 && q < w3) sum += k[j + 2] * r[q];
                        }
                        a[ch] = sum / 9.0f;
                    }
                } else {
                    a[0] = a[1] = a[2] = (r[3 * x] + r[3 * x + 1] + r[3 * x + 2]) / 3.0f;
                }
                if (a[0] + a[1] + a[2] <= 0.0f) continue;
                for (int ch = 0; ch < 3; ch++) c[ch] = paper[ch] + (ink[ch] - paper[ch]) * a[ch];
                put(rgb, x0 + x, y0 + (int)y + yy, c);
            }
        free(cov3);
        y += lh;
        line++;
    }
}

// Coverage of a half plane at a pixel: 16 x 16 point samples
static float halfPlane(float px, float py, float (*side)(float, float)) {
    int n = 0;
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) n += side(px + (i + 0.5f) / 16.0f, py + (j + 0.5f) / 16.0f) > 0.0f;
    return n / 256.0f;
}

static float edgeCx, edgeCy;
static float sideV(float x, float y) {
    return x - (edgeCx + (y - edgeCy) * TXS_EDGE_TAN);
}
static float sideH(float x, float y) {
    return y - (edgeCy - (x - edgeCx) * TXS_EDGE_TAN);
}

static void edgeTile(float* rgb, int t, int vertical) {
    int x0, y0;
    txs_tile(t, &x0, &y0);
    edgeCx = x0 + TXS_TILE_W / 2.0f;
    edgeCy = y0 + TXS_TILE_H / 2.0f;
    for (int y = y0; y < y0 + TXS_TILE_H; y++)
        for (int x = x0; x < x0 + TXS_TILE_W; x++) {
            float a = halfPlane((float)x, (float)y, vertical ? sideV : sideH);
            float v = TXS_EDGE_LO + (TXS_EDGE_HI - TXS_EDGE_LO) * a;
            float c[3] = {v, v, v};
            put(rgb, x, y, c);
        }
}

static void linesTile(float* rgb, int t, int vertical) {
    int x0, y0;
    txs_tile(t, &x0, &y0);
    for (int y = 0; y < TXS_TILE_H; y++)
        for (int x = 0; x < TXS_TILE_W; x++) {
            int band = vertical ? x / (TXS_TILE_W / 4) : y / (TXS_TILE_H / 4);
            int pos = vertical ? x : y;
            float v = pos % txs_period(band) == 0 ? 0.0f : 1.0f;
            float c[3] = {v, v, v};
            put(rgb, x0 + x, y0 + y, c);
        }
}

static void zoneTile(float* rgb, int t) {
    int x0, y0;
    txs_tile(t, &x0, &y0);
    float cx = TXS_TILE_W / 2.0f, cy = TXS_TILE_H / 2.0f;
    for (int y = 0; y < TXS_TILE_H; y++)
        for (int x = 0; x < TXS_TILE_W; x++) {
            // Local frequency r / 360 cycles a pixel: Nyquist at r = 180
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float v = 0.5f + 0.4f * cosf((float)M_PI * (dx * dx + dy * dy) / 360.0f);
            float c[3] = {v, v, v};
            put(rgb, x0 + x, y0 + y, c);
        }
}

static void gridTile(float* rgb, int t) {
    int x0, y0;
    txs_tile(t, &x0, &y0);
    for (int y = 0; y < TXS_TILE_H; y++)
        for (int x = 0; x < TXS_TILE_W; x++) {
            float v = (x % 4 == 0 || y % 4 == 0) ? 0.0f : 1.0f;
            float c[3] = {v, v, v};
            put(rgb, x0 + x, y0 + y, c);
        }
}

int txs_build(float* rgb, const char* fontPath) {
    Font f;
    memset(&f, 0, sizeof(f));
    FILE* fp = fopen(fontPath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    f.data = malloc((size_t)n);
    if (fread(f.data, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        free(f.data);
        return -1;
    }
    fclose(fp);
    if (!stbtt_InitFont(&f.info, f.data, stbtt_GetFontOffsetForIndex(f.data, 0))) {
        free(f.data);
        return -1;
    }
    rng = 0x2545f491u;
    const float white[3] = {1, 1, 1}, black[3] = {0, 0, 0};
    const float dark[3] = {0.118f, 0.118f, 0.118f}, light[3] = {0.86f, 0.86f, 0.86f};
    const float code[4][3] = {{0.86f, 0.86f, 0.67f}, {0.34f, 0.61f, 0.84f}, {0.81f, 0.57f, 0.47f}, {0.42f, 0.6f, 0.33f}};
    const float redBlue[2][3] = {{0.8f, 0.0f, 0.0f}, {0.0f, 0.2f, 0.9f}};
    textTile(rgb, &f, TXS_TEXT_BW, white, &black, 1, 0);
    textTile(rgb, &f, TXS_TEXT_WD, dark, &light, 1, 0);
    textTile(rgb, &f, TXS_TEXT_CT, white, &black, 1, 1);
    textTile(rgb, &f, TXS_TEXT_CODE, dark, code, 4, 0);
    edgeTile(rgb, TXS_EDGE_V, 1);
    edgeTile(rgb, TXS_EDGE_H, 0);
    linesTile(rgb, TXS_LINES_V, 1);
    linesTile(rgb, TXS_LINES_H, 0);
    zoneTile(rgb, TXS_ZONE);
    textTile(rgb, &f, TXS_TEXT_RED, white, redBlue, 2, 0);
    textTile(rgb, &f, TXS_TEXT_CT_WD, dark, &light, 1, 1);
    gridTile(rgb, TXS_GRID);
    for (int t = TXS_TEXT_BW2; t < TXS_TILES; t++) textTile(rgb, &f, t, white, &black, 1, t % 2);
    free(f.data);
    return 0;
}
