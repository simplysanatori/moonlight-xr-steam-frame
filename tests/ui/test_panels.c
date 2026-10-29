// Panels: every panel and tab renders inside its buffer (guard bytes, and ASan in the _asan build), hit
// regions are inside the panel and agree with xr_layout, every settings row is present.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "panels.h"

#define GUARD 256

typedef struct {
    uint8_t* base;
    uint8_t* px;
    int w, h;
    size_t size;
} Guarded;

static Guarded guarded(int w, int h) {
    Guarded g;
    g.w = w;
    g.h = h;
    g.size = (size_t)w * h * 4;
    g.base = malloc(g.size + 2 * GUARD);
    memset(g.base, 0xA5, g.size + 2 * GUARD);
    g.px = g.base + GUARD;
    memset(g.px, 0, g.size);
    return g;
}

static int guard_ok(const Guarded* g) {
    for (int i = 0; i < GUARD; i++) {
        if (g->base[i] != 0xA5 || g->px[g->size + i] != 0xA5) return 0;
    }
    return 1;
}

static int ink_pixels(const Guarded* g) {
    int n = 0;
    for (size_t i = 3; i < g->size; i += 4) n += g->px[i] > 0;
    return n;
}

static int premultiplied_ok(const Guarded* g) {
    for (size_t i = 0; i < g->size; i += 4) {
        if (g->px[i] > g->px[i + 3] || g->px[i + 1] > g->px[i + 3] || g->px[i + 2] > g->px[i + 3]) return 0;
    }
    return 1;
}

static UiHits hits;

static void draw(UiPanelId id, const UiView* v, float scale, Guarded* g) {
    int w, h;
    ui_panel_native_size(id, &w, &h);
    *g = guarded((int)(w * scale + 0.5f), (int)(h * scale + 0.5f));
    UiCanvas cv;
    ui_cv_init(&cv, g->px, g->w, g->h);
    ui_draw_panel(id, &cv, v, &hits);
}

static void check_hits_inside(const char* what) {
    (void)what;
    CHECK(hits.count > 0 && hits.count <= UI_MAX_HITS);
    for (int i = 0; i < hits.count; i++) {
        const UiHit* h = &hits.hit[i];
        CHECK(h->u0 >= 0.0f && h->v0 >= 0.0f && h->u1 <= 1.0f && h->v1 <= 1.0f);
        CHECK(h->u1 > h->u0 && h->v1 > h->v0);
        CHECK(h->kind != UI_HIT_NONE);
    }
}

// Every pixel of a panel is within its buffer at every scale, and the output is sane
static void test_all_panels_scales(void) {
    Settings s;
    settings_defaults(&s);
    UiView v;
    ui_view_init(&v, &s);
    v.version = "1.2.3";
    v.commit = "abcdef0";
    v.log_path = "/tmp/moonlight.log";
    static const float SCALES[] = { 0.5f, 0.75f, 1.0f, 1.5f, 0.31f };
    for (int id = 0; id < UI_PANEL_COUNT; id++) {
        if (id == UI_PANEL_REPORT || id == UI_PANEL_TOAST || id == UI_PANEL_CLOCK || id == UI_PANEL_PC_LIST ||
            id == UI_PANEL_APP_LIST) continue;
        for (size_t k = 0; k < sizeof SCALES / sizeof SCALES[0]; k++) {
            Guarded g;
            draw((UiPanelId)id, &v, SCALES[k], &g);
            CHECK(guard_ok(&g));
            CHECK(ink_pixels(&g) > g.w * g.h / 40);
            CHECK(premultiplied_ok(&g));
            free(g.base);
        }
    }
    // the corners of a sheet are transparent (a rounded sheet), the middle is not
    Guarded g;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    CHECK(g.px[3] == 0);
    CHECK(g.px[(((size_t)g.h / 2) * g.w + 5) * 4 + 3] > 200);
    free(g.base);
    // tiny canvases do not crash
    for (int id = 0; id < UI_PANEL_COUNT; id++) {
        if (id == UI_PANEL_REPORT || id == UI_PANEL_TOAST || id == UI_PANEL_CLOCK || id == UI_PANEL_PC_LIST || id == UI_PANEL_APP_LIST) continue;
        Guarded t = guarded(7, 5);
        UiCanvas cv;
        ui_cv_init(&cv, t.px, 7, 5);
        ui_draw_panel((UiPanelId)id, &cv, &v, &hits);
        CHECK(guard_ok(&t));
        free(t.base);
    }
}

static void test_cog_rows_present(void) {
    Settings s;
    settings_defaults(&s);
    s.head_locked = 1;
    UiView v;
    ui_view_init(&v, &s);
    // Screen tab
    v.cog_tab = COG_TAB_SCREEN;
    Guarded g;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("screen");
    for (int row = 0; row < COG_SCREEN_ROW_COUNT; row++) {
        CHECK(ui_hits_find(&hits, UI_HIT_TRACK_DOWN, row, -1) >= 0);
        CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, row, -1) >= 0);
        CHECK(ui_hits_find(&hits, UI_HIT_TRACK_UP, row, -1) >= 0);
        // the row's band is where xr_layout says it is
        int i = ui_hits_find(&hits, UI_HIT_TRACK_RUN, row, -1);
        CHECK_NEAR((hits.hit[i].v0 + hits.hit[i].v1) * 0.5f, cogRowV(COG_TAB_SCREEN, row), 1e-5);
        CHECK_NEAR((hits.hit[i].v1 - hits.hit[i].v0) * 0.5f, cogRowHalf(COG_TAB_SCREEN), 1e-5);
    }
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_RESET, -1) >= 0);
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) CHECK(ui_hits_find(&hits, UI_HIT_TAB, tab, -1) >= 0);
    free(g.base);

    // Display tab: every option row has its cells, then the glow level track
    v.cog_tab = COG_TAB_DISPLAY;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("display");
    for (int row = 0; row < COG_OPTION_COUNT; row++) {
        int cells = ui_cog_row_cells(COG_TAB_DISPLAY, row);
        CHECK(cells >= 2);
        for (int c = 0; c < cells; c++) CHECK(ui_hits_find(&hits, UI_HIT_CELL, row, c) >= 0);
        CHECK(ui_hits_find(&hits, UI_HIT_CELL, row, cells) < 0);
    }
    CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_DISPLAY_SLIDER_ROW, -1) >= 0);
    free(g.base);

    // 3D tab
    v.cog_tab = COG_TAB_3D;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("3d");
    for (int c = 0; c < COG_PRESET_CELLS; c++) CHECK(ui_hits_find(&hits, UI_HIT_CELL, COG_ROW3D_PRESET, c) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROW3D_SEPARATION, -1) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROW3D_CONVERGENCE, -1) >= 0);
    for (int c = 0; c < COG_STEREO_CELLS; c++) CHECK(ui_hits_find(&hits, UI_HIT_CELL, COG_ROW3D_SWITCH, c) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_RESET, -1) >= 0);
    free(g.base);

    // Picture
    v.cog_tab = COG_TAB_PICTURE;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("picture");
    for (int row = 0; row < PICTURE_VALUES; row++) CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, row, -1) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_RESET, -1) >= 0);
    free(g.base);

    // About
    v.cog_tab = COG_TAB_ABOUT;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("about");
    int k = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_KOFI, -1);
    int r = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_REPORT, -1);
    CHECK(k >= 0 && r >= 0);
    CHECK(cogKofiButtonAt((hits.hit[k].u0 + hits.hit[k].u1) / 2, (hits.hit[k].v0 + hits.hit[k].v1) / 2));
    CHECK(cogReportButtonAt((hits.hit[r].u0 + hits.hit[r].u1) / 2, (hits.hit[r].v0 + hits.hit[r].v1) / 2));
    free(g.base);

    // Room tab (theater) and the fixed-size room
    v.cog_tab = COG_TAB_SCREEN;
    v.room_env = ENV_ID_HOME_THEATER;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    check_hits_inside("room");
    CHECK(ui_hits_find(&hits, UI_HIT_TAB, COG_TAB_SCREEN, -1) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROOM_ROW_BRIGHTNESS, -1) >= 0);
    for (int c = 0; c < 2; c++) {
        CHECK(ui_hits_find(&hits, UI_HIT_CELL, COG_ROOM_ROW_GLOW, c) >= 0);
        CHECK(ui_hits_find(&hits, UI_HIT_CELL, COG_ROOM_ROW_LIGHT, c) >= 0);
    }
    CHECK(ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROOM_ROW_LIGHT_LEVEL, -1) >= 0);
    int size = ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROOM_ROW_SIZE, -1);
    CHECK(size >= 0 && hits.hit[size].enabled);
    free(g.base);
    v.room_env = ENV_ID_GRAND_CINEMA;
    draw(UI_PANEL_COG, &v, 1.0f, &g);
    size = ui_hits_find(&hits, UI_HIT_TRACK_RUN, COG_ROOM_ROW_SIZE, -1);
    CHECK(size >= 0 && !hits.hit[size].enabled);   // greyed: kept, but not live
    free(g.base);
}

static void test_cog_hits_agree_with_xr_layout(void) {
    Settings s;
    settings_defaults(&s);
    s.head_locked = 1;
    UiView v;
    ui_view_init(&v, &s);
    static const int TABS[] = { COG_TAB_SCREEN, COG_TAB_DISPLAY, COG_TAB_3D, COG_TAB_PICTURE };
    for (size_t ti = 0; ti < sizeof TABS / sizeof TABS[0]; ti++) {
        v.cog_tab = TABS[ti];
        Guarded g;
        draw(UI_PANEL_COG, &v, 1.0f, &g);
        free(g.base);
        int face = ui_cog_face(&v);
        for (int i = 0; i < hits.count; i++) {
            const UiHit* h = &hits.hit[i];
            float mu = (h->u0 + h->u1) * 0.5f;
            if (h->kind == UI_HIT_TRACK_DOWN) CHECK(cogTrackPart(mu) == TRACK_PART_DOWN);
            if (h->kind == UI_HIT_TRACK_RUN) CHECK(cogTrackPart(mu) == TRACK_PART_RUN);
            if (h->kind == UI_HIT_TRACK_UP) CHECK(cogTrackPart(mu) == TRACK_PART_UP);
            if (h->kind == UI_HIT_TRACK_DOWN || h->kind == UI_HIT_TRACK_RUN || h->kind == UI_HIT_TRACK_UP ||
                h->kind == UI_HIT_CELL) {
                float mv = (h->v0 + h->v1) * 0.5f;
                CHECK_NEAR(mv, cogRowV(face, h->a), 1e-5);
                CHECK(fabsf(mv - cogRowV(face, h->a)) < cogRowHalf(face));
            }
            if (h->kind == UI_HIT_TAB) CHECK_NEAR(h->v1, COG_TAB_BAR_B, 1e-6);
        }
        // pressing the middle of every enabled hit lands on that hit; tracks are one of the three parts
        for (int i = 0; i < hits.count; i++) {
            const UiHit* h = &hits.hit[i];
            if (!h->enabled) continue;
            float mu = (h->u0 + h->u1) * 0.5f, mv = (h->v0 + h->v1) * 0.5f;
            int at = ui_hits_at(&hits, mu, mv);
            CHECK(at >= 0);
            if (at >= 0) {
                CHECK(hits.hit[at].kind == h->kind && hits.hit[at].a == h->a);
                if (h->kind == UI_HIT_CELL) CHECK(hits.hit[at].b == h->b);
            }
        }
        // cells of a row are disjoint and tile the track's width
        for (int row = 0; row < ui_cog_row_count(face); row++) {
            if (ui_cog_row_is_track(face, row)) continue;
            int cells = ui_cog_row_cells(face, row);
            float prev = COG_TRACK_L;
            for (int c = 0; c < cells; c++) {
                int i = ui_hits_find(&hits, UI_HIT_CELL, row, c);
                CHECK(i >= 0);
                if (i >= 0) {
                    CHECK_NEAR(hits.hit[i].u0, prev, 1e-5);
                    prev = hits.hit[i].u1;
                }
            }
            CHECK_NEAR(prev, COG_TRACK_R, 1e-5);
        }
    }
}

static void test_other_panels_hits(void) {
    Settings s;
    settings_defaults(&s);
    UiView v;
    ui_view_init(&v, &s);
    Guarded g;
    // picker: five tiles, ENV_CELL_ numbering, the cell's own rectangle
    draw(UI_PANEL_PICKER, &v, 1.0f, &g);
    check_hits_inside("picker");
    CHECK(hits.count == ENV_CELL_COUNT);
    for (int c = 0; c < ENV_CELL_COUNT; c++) {
        int i = ui_hits_find(&hits, UI_HIT_ENV_TILE, c, -1);
        CHECK(i >= 0);
        if (i >= 0) {
            CHECK_NEAR(hits.hit[i].u0, (float)c / PICKER_COLS, 1e-5);
            CHECK_NEAR(hits.hit[i].u1, (float)(c + 1) / PICKER_COLS, 1e-5);
            CHECK_NEAR(hits.hit[i].v0, (float)PICKER_HEADER_PX / PICKER_TEX_H, 1e-5);
        }
    }
    free(g.base);

    // keyboard: 44 keys, blanks have no hit, codes per sheet, held modifiers lit
    for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
        v.kb_sheet = sheet;
        draw(UI_PANEL_KEYBOARD, &v, 1.0f, &g);
        check_hits_inside("keyboard");
        int expect = 0;
        for (int i = 0; i < ui_kb_key_count(); i++) expect += ui_kb_key_label(sheet, i) != NULL;
        CHECK(hits.count == expect);
        CHECK(sheet == KB_STATE_FN ? expect == 35 : expect == 44);
        for (int i = 0; i < hits.count; i++) {
            float r[4];
            ui_kb_key_rect(hits.hit[i].a, r);
            CHECK(hits.hit[i].b == ui_kb_key_code(sheet, hits.hit[i].a));
            CHECK(hits.hit[i].u0 == r[0] && hits.hit[i].v1 == r[3]);
            CHECK(hits.hit[i].b != 0);
        }
        // keys never overlap
        for (int i = 0; i < hits.count; i++)
            for (int j = i + 1; j < hits.count; j++) {
                const UiHit *a = &hits.hit[i], *b = &hits.hit[j];
                CHECK(a->u1 <= b->u0 || b->u1 <= a->u0 || a->v1 <= b->v0 || b->v1 <= a->v0);
            }
        free(g.base);
    }
    // the same sheet with Ctrl and Win lit differs from the plain one (lit caps are drawn white)
    v.kb_sheet = KB_STATE_FN;
    v.kb_mods = 0;
    Guarded a, b;
    draw(UI_PANEL_KEYBOARD, &v, 1.0f, &a);
    v.kb_mods = KB_MOD_CTRL | KB_MOD_WIN;
    draw(UI_PANEL_KEYBOARD, &v, 1.0f, &b);
    CHECK(memcmp(a.px, b.px, a.size) != 0);
    int ci = -1;
    for (int i = 0; i < ui_kb_key_count(); i++) if (ui_kb_key_code(KB_STATE_FN, i) == KB_CODE_CTRL) ci = i;
    CHECK(ci >= 0);
    float r[4];
    ui_kb_key_rect(ci, r);
    int cx = (int)((r[0] + r[2]) * 0.5f * b.w), cy = (int)((r[1] + r[3]) * 0.5f * b.h);
    // a lit cap is nearly white, an unlit one dark grey, at the same corner inside the key
    int px = (int)(r[0] * b.w) + 6, py = (int)(r[1] * b.h) + 6;
    CHECK(b.px[((size_t)py * b.w + px) * 4] > 200 && a.px[((size_t)py * a.w + px) * 4] < 120);
    (void)cx; (void)cy;
    free(a.base);
    free(b.base);
    // the held tag on a letter sheet's space bar
    v.kb_sheet = KB_STATE_LOWER;
    v.kb_mods = 0;
    draw(UI_PANEL_KEYBOARD, &v, 1.0f, &a);
    v.kb_mods = KB_MOD_ALT;
    draw(UI_PANEL_KEYBOARD, &v, 1.0f, &b);
    CHECK(memcmp(a.px, b.px, a.size) != 0);
    free(a.base);
    free(b.base);
    v.kb_mods = 0;

    // bar: every slot has a hit that stays inside its neighbours' pitch, move pill in the middle
    v.bar_show_aim = 1;
    v.bar_show_stereo = 1;
    draw(UI_PANEL_BAR, &v, 1.0f, &g);
    check_hits_inside("bar");
    CHECK(hits.count == BAR_SLOTS + 1);
    int pill = ui_hits_find(&hits, UI_HIT_BAR_MOVE, 0, -1);
    CHECK(pill >= 0 && fabsf((hits.hit[pill].u0 + hits.hit[pill].u1) * 0.5f - 0.5f) < 1e-4);
    for (int slot = 0; slot < BAR_SLOTS; slot++) {
        int i = ui_hits_find(&hits, UI_HIT_BAR_BUTTON, slot, -1);
        CHECK(i >= 0);
        if (i < 0) continue;
        // the centre agrees with xr_layout's placement of that slot (strip is 0.72 of the frame wide)
        Vec3 local;
        float side;
        barSlotPlacement(slot, 1.0f, 1.0f, &local, &side);
        CHECK_NEAR((hits.hit[i].u0 + hits.hit[i].u1) * 0.5f, 0.5f + local.x / 0.72f, 1e-3);
        // and xr_layout's barSlotHit accepts the hit's middle (in the frame coordinates)
        float fu = 0.5f + local.x, fv = 0.5f - local.y / 1.0f;
        (void)fu; (void)fv;
        CHECK(barSlotHit(slot, 0.5f + local.x, 0.5f - local.y, 1.0f, 1.0f));
    }
    for (int i = 0; i < hits.count; i++)
        for (int j = i + 1; j < hits.count; j++) {
            if (hits.hit[i].kind == UI_HIT_BAR_MOVE || hits.hit[j].kind == UI_HIT_BAR_MOVE) continue;
            CHECK(hits.hit[i].u1 <= hits.hit[j].u0 + 1e-6 || hits.hit[j].u1 <= hits.hit[i].u0 + 1e-6);
        }
    free(g.base);
    // absent buttons are not drawn and have no hit
    v.bar_show_aim = 0;
    v.bar_show_stereo = 0;
    draw(UI_PANEL_BAR, &v, 1.0f, &g);
    CHECK(hits.count == BAR_SLOTS - 1);
    CHECK(ui_hits_find(&hits, UI_HIT_BAR_BUTTON, BAR_SLOT_AIM, -1) < 0 && ui_hits_find(&hits, UI_HIT_BAR_BUTTON, BAR_SLOT_STEREO, -1) < 0);
    free(g.base);

    // button atlas: the 12 cells of xr_layout, each with something in it
    draw(UI_PANEL_BUTTON_ATLAS, &v, 1.0f, &g);
    for (int cell = 0; cell < BTN_CELLS; cell++) {
        int x, y, n = 0;
        CHECK(buttonCellOrigin(cell, &x, &y));
        for (int yy = 0; yy < BUTTON_TEX; yy++)
            for (int xx = 0; xx < BUTTON_TEX; xx++) n += g.px[((size_t)(y + yy) * g.w + x + xx) * 4 + 3] > 0;
        CHECK(n > 300);
    }
    free(g.base);

    // exit prompt / hint / kofi: the zones xr_layout has, and the buttons' hits match them
    draw(UI_PANEL_EXIT, &v, 1.0f, &g);
    check_hits_inside("exit");
    CHECK(hits.count == 2);
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_CONFIRM, -1) >= 0 && ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_CANCEL, -1) >= 0);
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_DISCONNECT, -1) < 0);
    free(g.base);
    // a stream's prompt: Quit app, Disconnect, Cancel, side by side inside the sheet
    v.exit_disconnect = 1;
    draw(UI_PANEL_EXIT, &v, 1.0f, &g);
    check_hits_inside("exit with disconnect");
    CHECK(hits.count == 3);
    int hq = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_CONFIRM, -1);
    int hd = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_DISCONNECT, -1);
    int hc = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_EXIT_CANCEL, -1);
    CHECK(hq >= 0 && hd >= 0 && hc >= 0);
    if (hq >= 0 && hd >= 0 && hc >= 0) {
        CHECK(hits.hit[hq].u1 <= hits.hit[hd].u0 && hits.hit[hd].u1 <= hits.hit[hc].u0);
        CHECK(hits.hit[hq].v0 == hits.hit[hd].v0 && hits.hit[hd].v0 == hits.hit[hc].v0);
    }
    // the labels and the line about what Disconnect does are on the sheet
    CHECK(strstr(ui_str(UIS_EXIT_DISCONNECT_HINT), "running") != NULL);
    CHECK(strcmp(ui_str(UIS_EXIT_QUIT_APP), "Quit app") == 0 && strcmp(ui_str(UIS_EXIT_DISCONNECT), "Disconnect") == 0);
    free(g.base);
    v.exit_disconnect = 0;
    draw(UI_PANEL_HINT, &v, 1.0f, &g);
    check_hits_inside("hint");
    int ok = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_HINT_OK, -1);
    CHECK(ok >= 0 && handHintZone((hits.hit[ok].u0 + hits.hit[ok].u1) / 2, (hits.hit[ok].v0 + hits.hit[ok].v1) / 2) == HINT_ZONE_OK);
    int nv = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_HINT_NEVER, -1);
    CHECK(nv >= 0 && handHintZone((hits.hit[nv].u0 + hits.hit[nv].u1) / 2, (hits.hit[nv].v0 + hits.hit[nv].v1) / 2) == HINT_ZONE_NEVER);
    free(g.base);
    draw(UI_PANEL_KOFI, &v, 1.0f, &g);
    check_hits_inside("kofi");
    int cl = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_KOFI_CLOSE, -1);
    CHECK(cl >= 0 && kofiSheetZone((hits.hit[cl].u0 + hits.hit[cl].u1) / 2, (hits.hit[cl].v0 + hits.hit[cl].v1) / 2) == KOFI_ZONE_CLOSE);
    free(g.base);
}

static void test_report_toast_lists(void) {
    Guarded g;
    int w, h;
    ui_panel_native_size(UI_PANEL_REPORT, &w, &h);
    g = guarded(w, h);
    UiCanvas cv;
    ui_cv_init(&cv, g.px, w, h);
    UiReport r = { "Something went wrong with the stream when I pressed the cog button and then the keyboard", 
                   REPORT_ZONE_NOTE, REPORT_ZONE_NONE, 1 };
    ui_draw_report(&cv, &r, &hits);
    CHECK(guard_ok(&g) && ink_pixels(&g) > 1000);
    check_hits_inside("report");
    static const int ZONES[] = { REPORT_ZONE_NOTE, REPORT_ZONE_CANCEL, REPORT_ZONE_SEND };
    for (size_t i = 0; i < 3; i++) {
        int k = ui_hits_find(&hits, UI_HIT_FIELD, ZONES[i], -1);
        CHECK(k >= 0);
        if (k >= 0) CHECK(reportZone((hits.hit[k].u0 + hits.hit[k].u1) / 2, (hits.hit[k].v0 + hits.hit[k].v1) / 2) == ZONES[i]);
    }
    // a huge note, no note, no pointers: nothing breaks
    char* big = malloc(20000);
    memset(big, 'w', 19999);
    big[19999] = 0;
    for (int i = 100; i < 19999; i += 37) big[i] = ' ';
    UiReport rb = { big, REPORT_ZONE_NOTE, REPORT_ZONE_SEND, 0 };
    ui_cv_clear(&cv);
    ui_draw_report(&cv, &rb, NULL);
    UiReport rn = { NULL, 0, 0, 0 };
    ui_draw_report(&cv, &rn, NULL);
    CHECK(guard_ok(&g));
    free(big);
    free(g.base);

    ui_panel_native_size(UI_PANEL_TOAST, &w, &h);
    g = guarded(w, h);
    ui_cv_init(&cv, g.px, w, h);
    ui_draw_toast(&cv, "3D on", NULL, 0);
    CHECK(ink_pixels(&g) > 1000 && guard_ok(&g));
    ui_draw_toast(&cv, "A rather long first line that cannot possibly fit on a single toast sheet however small the type goes", "/home/steamos/Games/MoonlightXRNative/a/very/long/path/that/also/needs/cutting/from/the/middle/file.txt", 1);
    ui_draw_toast(&cv, "x", "", 0);
    ui_draw_toast(&cv, "", "second", 0);
    ui_draw_toast(&cv, NULL, NULL, 0);
    CHECK(guard_ok(&g));
    free(g.base);
    ui_panel_native_size(UI_PANEL_CLOCK, &w, &h);
    g = guarded(w, h);
    ui_cv_init(&cv, g.px, w, h);
    ui_draw_clock(&cv, "10:42   Battery 87%");
    CHECK(ink_pixels(&g) > 500 && guard_ok(&g));
    ui_draw_clock(&cv, NULL);
    free(g.base);

    // lists
    static UiPcItem pcs[12];
    for (int i = 0; i < 12; i++) {
        snprintf(pcs[i].name, sizeof pcs[i].name, "PC %d with a name", i);
        pcs[i].state = i % 3;
        pcs[i].paired = i & 1;
        pcs[i].game_running = i == 3;
    }
    ui_panel_native_size(UI_PANEL_PC_LIST, &w, &h);
    g = guarded(w, h);
    ui_cv_init(&cv, g.px, w, h);
    for (int page = 0; page < 3; page++) {
        ui_cv_clear(&cv);
        UiPcList l = { pcs, 12, page, 0, { UI_HIT_NONE, 0, 0 } };
        ui_draw_pc_list(&cv, &l, &hits);
        check_hits_inside("pc list");
        int rows = page < 2 ? UI_LIST_ROWS : 2;
        int items = 0;
        for (int i = 0; i < hits.count; i++) items += hits.hit[i].kind == UI_HIT_LIST_ITEM;
        CHECK(items == rows);
        CHECK(ui_hits_find(&hits, UI_HIT_LIST_ITEM, page * UI_LIST_ROWS, -1) >= 0);
        int up = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_LIST_PAGE_UP, -1);
        int down = ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_LIST_PAGE_DOWN, -1);
        CHECK(up >= 0 && down >= 0 && ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_LIST_ADD, -1) >= 0);
        CHECK(hits.hit[up].enabled == (page > 0) && hits.hit[down].enabled == (page < 2));
    }
    UiPcList empty = { NULL, 0, 0, -1, { UI_HIT_NONE, 0, 0 } };
    ui_cv_clear(&cv);
    ui_draw_pc_list(&cv, &empty, &hits);
    CHECK(ink_pixels(&g) > 1000 && guard_ok(&g));
    UiPcList beyond = { pcs, 12, 99, -1, { UI_HIT_NONE, 0, 0 } };   // a page past the end draws no rows
    ui_draw_pc_list(&cv, &beyond, &hits);
    CHECK(guard_ok(&g));
    static UiAppItem apps[7];
    for (int i = 0; i < 7; i++) {
        if (i == 2) apps[i].name[0] = 0;
        else snprintf(apps[i].name, sizeof apps[i].name, "\xC3\x89pic App Number %d", i);
        apps[i].running = i == 1;
    }
    ui_cv_clear(&cv);
    UiAppList al = { "A PC", apps, 7, 1, 5, 0, 0, { UI_HIT_NONE, 0, 0 } };
    ui_draw_app_list(&cv, &al, &hits);
    check_hits_inside("app list");
    CHECK(ui_hits_find(&hits, UI_HIT_LIST_ITEM, 5, -1) >= 0 && ui_hits_find(&hits, UI_HIT_LIST_ITEM, 4, -1) < 0);
    CHECK(ui_hits_find(&hits, UI_HIT_BUTTON, UI_BTN_LIST_BACK, -1) >= 0);
    UiAppList loading = { NULL, apps, 7, 0, -1, 1, 0, { UI_HIT_NONE, 0, 0 } };
    ui_draw_app_list(&cv, &loading, &hits);
    UiAppList failed = { "x", NULL, 0, 0, -1, 0, 1, { UI_HIT_NONE, 0, 0 } };
    ui_draw_app_list(&cv, &failed, &hits);
    CHECK(guard_ok(&g));
    free(g.base);
}

// Every combination of settings and view the panels branch on, drawn with guards
static void test_state_sweep(void) {
    Settings s;
    settings_defaults(&s);
    UiView v;
    int count = 0;
    for (unsigned seed = 0; seed < 40; seed++) {
        srand(seed);
        int n;
        const SettingDesc* t = settings_table(&n);
        for (int i = 0; i < n; i++) {
            if (t[i].type == SET_STR) continue;
            settings_set(&s, &t[i], t[i].min + rand() % (t[i].max - t[i].min + 1));
        }
        settings_clamp(&s);
        ui_view_init(&v, &s);
        v.session.head_aim = rand() & 1;
        v.session.pad_mode = rand() & 1;
        v.session.ray = rand() & 1;
        v.session.stereo_on = rand() & 1;
        v.curve_ok = rand() & 1;
        v.stereo_ok = rand() & 1;
        v.bar_show_aim = rand() & 1;
        v.bar_show_stereo = rand() & 1;
        static const int ROOMS[] = { -1, ENV_ID_HOME_THEATER, ENV_ID_GRAND_CINEMA, ENV_ID_SYNTHWAVE, ENV_ID_VOID };
        v.room_env = ROOMS[rand() % 5];
        v.env_selected = rand() % 6 - 1;
        v.kb_sheet = rand() % KB_STATE_COUNT;
        v.kb_mods = (rand() % 8) << 1;
        v.hover.kind = rand() % 12;
        v.hover.a = rand() % 12;
        v.hover.b = rand() % 3;
        v.version = seed & 1 ? NULL : "9.9.9-a-very-long-version-string-that-must-be-cut-somewhere";
        v.commit = seed & 2 ? NULL : "0123456789abcdef0123456789abcdef";
        v.log_path = seed & 4 ? NULL : "/home/steamos/a/really/quite/long/path/to/a/log/file/that/goes/on/and/on/moonlight.log";
        for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
            v.cog_tab = tab;
            Guarded g;
            draw(UI_PANEL_COG, &v, 0.8f, &g);
            CHECK(guard_ok(&g));
            check_hits_inside("sweep");
            free(g.base);
            count++;
        }
        for (int id = UI_PANEL_PICKER; id <= UI_PANEL_KOFI; id++) {
            Guarded g;
            draw((UiPanelId)id, &v, 1.0f, &g);
            CHECK(guard_ok(&g));
            free(g.base);
            count++;
        }
    }
    CHECK(count > 300);
}

// A change in a setting changes the pixels where the row is, so every row reads from the struct
static void test_rows_read_settings(void) {
    Settings a, b;
    settings_defaults(&a);
    a.head_locked = 1;
    b = a;
    UiView va, vb;
    ui_view_init(&va, &a);
    ui_view_init(&vb, &b);
    struct { int tab; const char* key; int value; } CASES[] = {
        { COG_TAB_DISPLAY, "sharpening", 2 }, { COG_TAB_DISPLAY, "supersampling", 0 }, { COG_TAB_DISPLAY, "stats", 1 },
        { COG_TAB_DISPLAY, "head_locked", 0 }, { COG_TAB_DISPLAY, "pointer_sleep", 0 }, { COG_TAB_DISPLAY, "controller_model", 1 },
        { COG_TAB_DISPLAY, "click_sound", 0 }, { COG_TAB_DISPLAY, "glow", 1 }, { COG_TAB_DISPLAY, "screen_light", 0 },
        { COG_TAB_DISPLAY, "glow_level", 10 }, { COG_TAB_DISPLAY, "gamepad_toggle", 1 },
        { COG_TAB_SCREEN, "distance_cm", 100 }, { COG_TAB_SCREEN, "height_cm", 100 }, { COG_TAB_SCREEN, "tilt_deg", 20 },
        { COG_TAB_SCREEN, "rotate_deg", 30 }, { COG_TAB_SCREEN, "curve", 40 }, { COG_TAB_SCREEN, "width_cm", 600 },
        { COG_TAB_SCREEN, "head_aim_sensitivity", 20 }, { COG_TAB_SCREEN, "head_aim_deadzone", 12 },
        { COG_TAB_3D, "separation", 12 }, { COG_TAB_3D, "convergence", 20 },
        { COG_TAB_PICTURE, "picture_brightness", 20 }, { COG_TAB_PICTURE, "picture_contrast", 80 },
        { COG_TAB_PICTURE, "picture_gamma", 150 }, { COG_TAB_PICTURE, "picture_saturation", 20 },
    };
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        b = a;
        const SettingDesc* d = settings_find(CASES[i].key);
        CHECK(d != NULL);
        if (!d) continue;
        settings_set(&b, d, CASES[i].value);
        CHECK(!settings_equal(&a, &b));
        va.cog_tab = vb.cog_tab = CASES[i].tab;
        va.curve_ok = vb.curve_ok = 1;
        Guarded ga, gb;
        draw(UI_PANEL_COG, &va, 1.0f, &ga);
        draw(UI_PANEL_COG, &vb, 1.0f, &gb);
        if (memcmp(ga.px, gb.px, ga.size) == 0) fprintf(stderr, "setting %s does not change the panel\n", CASES[i].key);
        CHECK(memcmp(ga.px, gb.px, ga.size) != 0);
        free(ga.base);
        free(gb.base);
    }
    // room values on the Room tab
    static const char* const ROOMKEYS[] = { "room_4_brightness", "room_4_glow", "room_4_light", "room_4_screen" };
    static const int ROOMVALS[] = { 100, 0, 20, 40 };
    for (int i = 0; i < 4; i++) {
        b = a;
        settings_set(&b, settings_find(ROOMKEYS[i]), ROOMVALS[i]);
        va.cog_tab = vb.cog_tab = COG_TAB_SCREEN;
        va.room_env = vb.room_env = ENV_ID_HOME_THEATER;
        Guarded ga, gb;
        draw(UI_PANEL_COG, &va, 1.0f, &ga);
        draw(UI_PANEL_COG, &vb, 1.0f, &gb);
        CHECK(memcmp(ga.px, gb.px, ga.size) != 0);
        free(ga.base);
        free(gb.base);
    }
}

// The atlas: every panel has a slot of its native size, inside the atlas, none overlapping,
// and drawing into a slot through a sub canvas is pixel-identical to drawing alone and
// leaves every other pixel of the atlas alone
static void test_atlas(void) {
    for (int a = 0; a < UI_PANEL_COUNT; a++) {
        UiRectPx ra = ui_atlas_rect((UiPanelId)a);
        int w, h;
        ui_panel_native_size((UiPanelId)a, &w, &h);
        CHECK(ra.w == w && ra.h == h);
        CHECK(ra.x >= 0 && ra.y >= 0 && ra.x + ra.w <= UI_ATLAS_W && ra.y + ra.h <= UI_ATLAS_H);
        for (int b = a + 1; b < UI_PANEL_COUNT; b++) {
            UiRectPx rb = ui_atlas_rect((UiPanelId)b);
            CHECK(ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x || ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y);
        }
    }
    CHECK(ui_atlas_rect(UI_PANEL_COUNT).w == 0);
    Settings s;
    settings_defaults(&s);
    UiView v;
    ui_view_init(&v, &s);
    v.version = "1";
    size_t bytes = (size_t)UI_ATLAS_W * UI_ATLAS_H * 4;
    uint8_t* atlas = malloc(bytes + 2 * GUARD);
    memset(atlas, 0xA5, bytes + 2 * GUARD);
    uint8_t* px = atlas + GUARD;
    memset(px, 0, bytes);
    for (int id = 0; id < UI_PANEL_COUNT; id++) {
        if (id == UI_PANEL_REPORT || id == UI_PANEL_TOAST || id == UI_PANEL_CLOCK || id == UI_PANEL_PC_LIST || id == UI_PANEL_APP_LIST) continue;
        UiRectPx r = ui_atlas_rect((UiPanelId)id);
        UiCanvas sub;
        ui_cv_sub(&sub, px, UI_ATLAS_W, r.x, r.y, r.w, r.h);
        static UiHits h2;
        ui_draw_panel((UiPanelId)id, &sub, &v, &h2);
        Guarded g;
        draw((UiPanelId)id, &v, 1.0f, &g);
        int same = 1;
        for (int y = 0; y < r.h && same; y++) {
            same = memcmp(px + ((size_t)(r.y + y) * UI_ATLAS_W + r.x) * 4, g.px + (size_t)y * r.w * 4, (size_t)r.w * 4) == 0;
        }
        CHECK(same);
        CHECK(h2.count == hits.count);   // the same regions whether drawn alone or in the atlas
        free(g.base);
    }
    // nothing outside the slots was touched
    int stray = 0;
    for (int y = 0; y < UI_ATLAS_H; y++) {
        for (int x = 0; x < UI_ATLAS_W; x++) {
            int inside = 0;
            for (int id = 0; id < UI_PANEL_COUNT; id++) {
                UiRectPx r = ui_atlas_rect((UiPanelId)id);
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) { inside = 1; break; }
            }
            if (!inside && px[((size_t)y * UI_ATLAS_W + x) * 4 + 3] != 0) stray++;
        }
    }
    CHECK(stray == 0);
    for (int i = 0; i < GUARD; i++) CHECK(atlas[i] == 0xA5 && atlas[GUARD + bytes + i] == 0xA5);
    free(atlas);
}

int main(void) {
    ui_panels_set_assets_dir(MLXR_ASSETS_DIR);
    test_all_panels_scales();
    test_cog_rows_present();
    test_cog_hits_agree_with_xr_layout();
    test_other_panels_hits();
    test_report_toast_lists();
    test_state_sweep();
    test_rows_read_settings();
    test_atlas();
    ui_panels_shutdown();
    return checksDone(
#ifdef UI_ASAN
        "ui_panels_asan"
#else
        "ui_panels"
#endif
    );
}
