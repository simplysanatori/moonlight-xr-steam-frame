// The report sheet, the toast, the clock strip and the native launcher's
// lists. Report, toast and clock follow XrPanels.ReportSheet / Toast /
// ClockStrip; the PC and app lists have no upstream counterpart (Moonlight's
// Android UI is a Leanback activity) and are written to match the sheet style. GPLv3.
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

// ---- helpers -------------------------------------------------------------------------------

static void draw_wrapped(Dc* dc, float size, float x, float first_baseline, float step, float room,
                         uint32_t c, const char* text, int max_lines) {
    UiTextLine lines[16];
    int n = ui_text_wrap(dc->font, UI_WEIGHT_REGULAR, size * dc->k, text, room * dc->k, lines, 16);
    if (n > 16) n = 16;
    for (int i = 0; i < n && i < max_lines; i++) {
        char buf[512];
        int len = lines[i].len < 511 ? lines[i].len : 511;
        memcpy(buf, text + lines[i].start, (size_t)len);
        buf[len] = 0;
        dc_text(dc, UI_WEIGHT_REGULAR, size, x, first_baseline + (float)i * step, UI_ALIGN_LEFT, c, buf);
    }
}

// ---- report sheet -------------------------------------------------------------------------------

#define NOTE_LINES 7
#define REPORT_PAD 14.0f

static void field_box(Dc* dc, float x0, float y0, float x1, float y1, int focused, int hot) {
    dc_fill_rrect(dc, x0, y0, x1, y1, 12.0f, focused ? 0x1EFFFFFFu : 0x12FFFFFFu);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 12.0f, focused ? 3.0f : 2.0f,
                    focused ? 0xEEFFFFFFu : hot ? 0x99FFFFFFu : 0x50FFFFFFu);
}

static void caret(Dc* dc, float x, float top, float height) {
    dc_fill_rrect(dc, x, top + height * 0.12f, x + 2.5f, top + height * 0.92f, 0.0f, 0xEEFFFFFFu);
}

static void report_button(Dc* dc, float l, float r, const char* text, int live, int primary, int hot) {
    float x0 = l * dc->nw, x1 = r * dc->nw, y0 = REPORT_BTN_T * dc->nh, y1 = REPORT_BTN_B * dc->nh;
    uint32_t ink = live ? 0xEEFFFFFFu : 0x40FFFFFFu;
    if (live && (hot || primary)) dc_fill_rrect(dc, x0, y0, x1, y1, 16.0f, hot ? 0x38FFFFFFu : 0x18FFFFFFu);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 16.0f, live && hot ? 5.0f : 3.0f, ink);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 28.0f, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (x1 - x0) - 24.0f,
                       live ? 0xFFFFFFFFu : 0x50FFFFFFu, text);
}

void ui_draw_report(UiCanvas* cv, const UiReport* r, UiHits* hits) {
    UiView view;
    memset(&view, 0, sizeof view);
    Dc dc;
    dc_init(&dc, cv, hits, &view, REPORT_TEX_W, REPORT_TEX_H);
    if (!dc.font) return;
    const char* note = r->note ? r->note : "";
    const float left = REPORT_FIELD_L * dc.nw, right = REPORT_FIELD_R * dc.nw;

    dc_sheet(&dc, 32.0f, 0xF0141416u);
    {
        char cut[200];
        ui_text_ellipsize(dc.font, UI_WEIGHT_REGULAR, 32.0f * dc.k, ui_str(UIS_REPORT_TITLE), (right - left) * dc.k,
                          UI_ELLIPSIZE_END, cut, sizeof cut, NULL);
        dc_text(&dc, UI_WEIGHT_REGULAR, 32.0f, dc.nw * 0.5f, 0.085f * dc.nh, UI_ALIGN_CENTER, 0xFFFFFFFFu, cut);
    }

    // the note: its label, the box, and its last lines
    float nt = REPORT_NOTE_T * dc.nh, nb = REPORT_NOTE_B * dc.nh;
    dc_text_fit_left(&dc, UI_WEIGHT_REGULAR, 21.0f, left, nt - 12.0f, right - left, 0xB0FFFFFFu, ui_str(UIS_REPORT_NOTE_LABEL));
    field_box(&dc, left, nt, right, nb, r->focus == REPORT_ZONE_NOTE, r->hover == REPORT_ZONE_NOTE);
    float line_h = ((nb - nt) - 2.0f * REPORT_PAD) / NOTE_LINES;
    float text_w = (right - left) - 2.0f * REPORT_PAD;
    const float hint_in = 8.0f;
    if (!note[0]) {
        dc_text_fit_left(&dc, UI_WEIGHT_REGULAR, 25.0f, left + REPORT_PAD + hint_in, nt + REPORT_PAD + line_h * 0.78f,
                         text_w - hint_in, 0x60FFFFFFu, ui_str(UIS_REPORT_NOTE_HINT));
        if (r->focus == REPORT_ZONE_NOTE) caret(&dc, left + REPORT_PAD, nt + REPORT_PAD, line_h);
    } else {
        UiTextLine lines[64];
        int n = ui_text_wrap(dc.font, UI_WEIGHT_REGULAR, 25.0f * dc.k, note, text_w * dc.k, lines, 64);
        int have = n > 64 ? 64 : n;
        if (n > 64) {
            // the tail of a very long note: show the last lines of what we have wrapped, in order
        }
        int first = have > NOTE_LINES ? have - NOTE_LINES : 0;
        for (int i = first; i < have; i++) {
            char buf[512];
            int len = lines[i].len < 511 ? lines[i].len : 511;
            memcpy(buf, note + lines[i].start, (size_t)len);
            buf[len] = 0;
            dc_text(&dc, UI_WEIGHT_REGULAR, 25.0f, left + REPORT_PAD, nt + REPORT_PAD + (float)(i - first) * line_h + line_h * 0.78f,
                    UI_ALIGN_LEFT, 0xFFFFFFFFu, buf);
        }
        if (r->focus == REPORT_ZONE_NOTE && have > 0) {
            float w = lines[have - 1].width / dc.k;
            caret(&dc, left + REPORT_PAD + w + 2.0f, nt + REPORT_PAD + (float)(have - 1 - first) * line_h, line_h);
        }
    }
    dc_hit_uv(&dc, UI_HIT_FIELD, REPORT_ZONE_NOTE, 0, REPORT_FIELD_L, REPORT_NOTE_T, REPORT_FIELD_R, REPORT_NOTE_B, 1);

    // what goes, and where
    draw_wrapped(&dc, 18.0f, left, 0.69f * dc.nh, 23.0f, right - left, 0x99FFFFFFu, ui_str(UIS_REPORT_GOES), 3);

    report_button(&dc, REPORT_CANCEL_L, REPORT_CANCEL_R, ui_str(UIS_CANCEL), 1, 0, r->hover == REPORT_ZONE_CANCEL);
    dc_hit_uv(&dc, UI_HIT_FIELD, REPORT_ZONE_CANCEL, 0, REPORT_CANCEL_L, REPORT_BTN_T, REPORT_CANCEL_R, REPORT_BTN_B, 1);
    report_button(&dc, REPORT_SEND_L, REPORT_SEND_R, ui_str(UIS_REPORT_SEND), r->can_send, 1, r->hover == REPORT_ZONE_SEND);
    dc_hit_uv(&dc, UI_HIT_FIELD, REPORT_ZONE_SEND, 0, REPORT_SEND_L, REPORT_BTN_T, REPORT_SEND_R, REPORT_BTN_B, r->can_send);
}

// ---- toast and clock -----------------------------------------------------------------------------

void ui_draw_toast(UiCanvas* cv, const char* text, const char* more, int more_is_path) {
    UiView view;
    memset(&view, 0, sizeof view);
    Dc dc;
    dc_init(&dc, cv, NULL, &view, TOAST_TEX_W, TOAST_TEX_H);
    if (!dc.font || !text) return;
    dc_sheet(&dc, 40.0f, 0xF0141416u);
    const float room = TOAST_TEX_W - 80.0f, mid = TOAST_TEX_W * 0.5f;
    char cut[512];
    ui_text_ellipsize(dc.font, UI_WEIGHT_REGULAR, 44.0f * dc.k, text, room * dc.k, UI_ELLIPSIZE_END, cut, sizeof cut, NULL);
    if (!more || !*more) {
        dc_text_center(&dc, UI_WEIGHT_REGULAR, 44.0f, mid, TOAST_TEX_H * 0.5f, 0xFFFFFFFFu, cut);
        return;
    }
    dc_text(&dc, UI_WEIGHT_REGULAR, 44.0f, mid, TOAST_TEX_H * 0.44f, UI_ALIGN_CENTER, 0xFFFFFFFFu, cut);
    // a path is what the second line most often carries, and its end is the
    // part that matters, so it gets smaller before it gets cut
    float size = 32.0f;
    while (size > 22.0f && dc_text_width(&dc, UI_WEIGHT_REGULAR, size, more) > room) size -= 1.0f;
    ui_text_ellipsize(dc.font, UI_WEIGHT_REGULAR, size * dc.k, more, room * dc.k,
                      more_is_path ? UI_ELLIPSIZE_MIDDLE : UI_ELLIPSIZE_END, cut, sizeof cut, NULL);
    dc_text(&dc, UI_WEIGHT_REGULAR, size, mid, TOAST_TEX_H * 0.78f, UI_ALIGN_CENTER, 0xB0FFFFFFu, cut);
}

void ui_draw_clock(UiCanvas* cv, const char* line) {
    UiView view;
    memset(&view, 0, sizeof view);
    Dc dc;
    dc_init(&dc, cv, NULL, &view, COG_CLOCK_TEX_W, COG_CLOCK_TEX_H);
    if (!dc.font || !line) return;
    dc_fill_rrect(&dc, 1.0f, 1.0f, COG_CLOCK_TEX_W - 1.0f, COG_CLOCK_TEX_H - 1.0f, 20.0f, 0xF0141416u);
    dc_text_center(&dc, UI_WEIGHT_REGULAR, 26.0f, COG_CLOCK_TEX_W * 0.5f, COG_CLOCK_TEX_H * 0.5f, 0xD0FFFFFFu, line);
}

// ---- launcher lists ------------------------------------------------------------------------------------

#define L_W 1024.0f
#define L_H 640.0f
#define ROW_X0 40.0f
#define ROW_X1 984.0f
#define ROW_PITCH 92.0f
#define ROW_H 82.0f
#define PC_ROW_Y0 96.0f
#define FOOT_Y0 574.0f
#define FOOT_Y1 618.0f

static uint32_t state_colour(int state) {
    return state == UI_PC_ONLINE ? 0xFF4ADE80u : state == UI_PC_REFRESHING ? 0xFFFFB74Du : 0xFF9CA3AFu;
}

static void footer_button(Dc* dc, int btn, float x0, float x1, const char* label, int enabled, int hot) {
    uint32_t ink = enabled ? 0xEEFFFFFFu : 0x40FFFFFFu;
    if (hot && enabled) dc_fill_rrect(dc, x0, FOOT_Y0, x1, FOOT_Y1, 14.0f, 0x28FFFFFFu);
    dc_stroke_rrect(dc, x0, FOOT_Y0, x1, FOOT_Y1, 14.0f, 3.0f, ink);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 22.0f, (x0 + x1) * 0.5f, (FOOT_Y0 + FOOT_Y1) * 0.5f, (x1 - x0) - 16.0f, enabled ? 0xFFFFFFFFu : 0x50FFFFFFu, label);
    dc_hit(dc, UI_HIT_BUTTON, btn, 0, x0, FOOT_Y0, x1, FOOT_Y1, enabled);
}

static void page_controls(Dc* dc, int page, int count, const UiHover* hover) {
    int pages = (count + UI_LIST_ROWS - 1) / UI_LIST_ROWS;
    if (pages < 1) pages = 1;
    char buf[32];
    snprintf(buf, sizeof buf, ui_str(UIS_NATIVE_PAGE_OF), page + 1, pages);
    UiView hv;
    memset(&hv, 0, sizeof hv);
    hv.hover = *hover;
    Dc d2 = *dc;
    d2.v = &hv;
    // up and down are chevron buttons either side of the page number
    float ux0 = 740.0f, ux1 = 800.0f, dx0 = 924.0f, dx1 = 984.0f;
    int up_on = page > 0, down_on = page + 1 < pages;
    for (int i = 0; i < 2; i++) {
        float x0 = i ? dx0 : ux0, x1 = i ? dx1 : ux1;
        int on = i ? down_on : up_on;
        int btn = i ? UI_BTN_LIST_PAGE_DOWN : UI_BTN_LIST_PAGE_UP;
        uint32_t ink = on ? 0xEEFFFFFFu : 0x40FFFFFFu;
        if (on && dc_hover(&d2, UI_HIT_BUTTON, btn, -1)) dc_fill_rrect(dc, x0, FOOT_Y0, x1, FOOT_Y1, 14.0f, 0x28FFFFFFu);
        dc_stroke_rrect(dc, x0, FOOT_Y0, x1, FOOT_Y1, 14.0f, 3.0f, ink);
        float cx = (x0 + x1) * 0.5f, cy = (FOOT_Y0 + FOOT_Y1) * 0.5f;
        // chevron: up = ^, down = v
        float a = i ? -5.0f : 5.0f;
        float pts[6] = { cx - 11.0f, cy + a, cx, cy - a, cx + 11.0f, cy + a };
        dc_polyline(dc, pts, 3, 4.0f, ink, 0);
        dc_hit(dc, UI_HIT_BUTTON, btn, 0, x0, FOOT_Y0, x1, FOOT_Y1, on);
    }
    dc_text_center(dc, UI_WEIGHT_REGULAR, 22.0f, (ux1 + dx0) * 0.5f, (FOOT_Y0 + FOOT_Y1) * 0.5f, 0xB0FFFFFFu, buf);
}

static void monitor_icon(Dc* dc, float x, float cy, uint32_t ink) {
    dc_stroke_rrect(dc, x, cy - 20.0f, x + 50.0f, cy + 12.0f, 5.0f, 3.5f, ink);
    dc_line(dc, x + 25.0f, cy + 14.0f, x + 25.0f, cy + 21.0f, 3.5f, ink);
    dc_line(dc, x + 14.0f, cy + 22.0f, x + 36.0f, cy + 22.0f, 3.5f, ink);
}

static void row_card(Dc* dc, float y0, int selected, int hot) {
    dc_fill_rrect(dc, ROW_X0, y0, ROW_X1, y0 + ROW_H, 16.0f, hot ? 0x38FFFFFFu : 0x20FFFFFFu);
    dc_stroke_rrect(dc, ROW_X0, y0, ROW_X1, y0 + ROW_H, 16.0f, selected ? 3.5f : 2.0f, selected ? 0xFFFFFFFFu : 0x50FFFFFFu);
}

static void chevron_right(Dc* dc, float cx, float cy, uint32_t ink) {
    float pts[6] = { cx - 5.0f, cy - 11.0f, cx + 5.0f, cy, cx - 5.0f, cy + 11.0f };
    dc_polyline(dc, pts, 3, 4.0f, ink, 0);
}

void ui_draw_pc_list(UiCanvas* cv, const UiPcList* l, UiHits* hits) {
    UiView view;
    memset(&view, 0, sizeof view);
    view.hover = l->hover;
    Dc dc;
    dc_init(&dc, cv, hits, &view, 1024, 640);
    if (!dc.font) return;
    dc_sheet(&dc, 32.0f, 0xF0141416u);
    dc_text(&dc, UI_WEIGHT_MEDIUM, 36.0f, ROW_X0 + 8.0f, 62.0f, UI_ALIGN_LEFT, 0xFFFFFFFFu, ui_str(UIS_NATIVE_PC_LIST_TITLE));
    dc_fill_rrect(&dc, 20.0f, 78.0f, L_W - 20.0f, 80.0f, 0.0f, 0x30FFFFFFu);

    if (l->count == 0) {
        UiTextLine lines[4];
        const char* msg = ui_str(UIS_NATIVE_PC_NONE);
        int n = ui_text_wrap(dc.font, UI_WEIGHT_REGULAR, 24.0f * dc.k, msg, 700.0f * dc.k, lines, 4);
        for (int i = 0; i < n && i < 4; i++) {
            char buf[256];
            int len = lines[i].len < 255 ? lines[i].len : 255;
            memcpy(buf, msg + lines[i].start, (size_t)len);
            buf[len] = 0;
            dc_text(&dc, UI_WEIGHT_REGULAR, 24.0f, L_W * 0.5f, 270.0f + (float)i * 34.0f, UI_ALIGN_CENTER, 0xB0FFFFFFu, buf);
        }
    }
    int first = l->page * UI_LIST_ROWS;
    for (int i = 0; i < UI_LIST_ROWS && first + i < l->count; i++) {
        const UiPcItem* it = &l->items[first + i];
        float y0 = PC_ROW_Y0 + (float)i * ROW_PITCH, cy = y0 + ROW_H * 0.5f;
        int idx = first + i;
        row_card(&dc, y0, l->selected == idx, dc_hover(&dc, UI_HIT_LIST_ITEM, idx, -1));
        monitor_icon(&dc, ROW_X0 + 24.0f, cy, it->state == UI_PC_ONLINE ? 0xEEFFFFFFu : 0x90FFFFFFu);
        dc_text_fit_left(&dc, UI_WEIGHT_REGULAR, 28.0f, ROW_X0 + 100.0f, cy - 2.0f, 520.0f, 0xFFFFFFFFu, it->name);
        dc_fill_circle(&dc, ROW_X0 + 107.0f, cy + 22.0f, 6.0f, state_colour(it->state));
        char st[96];
        const char* state = ui_str(it->state == UI_PC_ONLINE ? UIS_PC_ONLINE : it->state == UI_PC_REFRESHING ? UIS_PC_REFRESHING : UIS_PC_OFFLINE);
        snprintf(st, sizeof st, "%s  \xC2\xB7  %s", state, ui_str(it->paired ? UIS_NATIVE_PC_PAIRED : UIS_NATIVE_PC_NOT_PAIRED));
        dc_text(&dc, UI_WEIGHT_REGULAR, 19.0f, ROW_X0 + 122.0f, cy + 29.0f, UI_ALIGN_LEFT, 0xB0FFFFFFu, st);
        if (it->game_running) {
            float tw = dc_text_width(&dc, UI_WEIGHT_REGULAR, 18.0f, ui_str(UIS_NATIVE_PC_RUNNING)) + 28.0f;
            float x1 = ROW_X1 - 70.0f, x0 = x1 - tw;
            dc_stroke_rrect(&dc, x0, cy - 16.0f, x1, cy + 16.0f, 16.0f, 2.0f, 0xFF4ADE80u);
            dc_text_center(&dc, UI_WEIGHT_REGULAR, 18.0f, (x0 + x1) * 0.5f, cy, 0xFF4ADE80u, ui_str(UIS_NATIVE_PC_RUNNING));
        }
        chevron_right(&dc, ROW_X1 - 34.0f, cy, 0xB0FFFFFFu);
        dc_hit(&dc, UI_HIT_LIST_ITEM, idx, 0, ROW_X0, y0, ROW_X1, y0 + ROW_H, 1);
    }
    footer_button(&dc, UI_BTN_LIST_ADD, ROW_X0, 380.0f, ui_str(UIS_PC_ADD), 1, dc_hover(&dc, UI_HIT_BUTTON, UI_BTN_LIST_ADD, -1));
    page_controls(&dc, l->page, l->count, &l->hover);
}

void ui_draw_app_list(UiCanvas* cv, const UiAppList* l, UiHits* hits) {
    UiView view;
    memset(&view, 0, sizeof view);
    view.hover = l->hover;
    Dc dc;
    dc_init(&dc, cv, hits, &view, 1024, 640);
    if (!dc.font) return;
    dc_sheet(&dc, 32.0f, 0xF0141416u);
    dc_text_fit_left(&dc, UI_WEIGHT_MEDIUM, 34.0f, ROW_X0 + 8.0f, 52.0f, 800.0f, 0xFFFFFFFFu, l->pc_name ? l->pc_name : "");
    dc_text(&dc, UI_WEIGHT_REGULAR, 19.0f, ROW_X0 + 8.0f, 78.0f, UI_ALIGN_LEFT, 0xB0FFFFFFu, ui_str(UIS_APP_LIST_TITLE));
    dc_fill_rrect(&dc, 20.0f, 90.0f, L_W - 20.0f, 92.0f, 0.0f, 0x30FFFFFFu);

    if (l->loading || l->failed || l->count == 0) {
        const char* msg = l->loading ? ui_str(UIS_APP_REFRESHING) : l->failed ? ui_str(UIS_APP_ERROR) : ui_str(UIS_NATIVE_APP_NONE);
        dc_text_fit_center(&dc, UI_WEIGHT_REGULAR, 26.0f, L_W * 0.5f, 320.0f, 800.0f, l->failed ? 0xFFFFB74Du : 0xB0FFFFFFu, msg);
    }
    int first = l->page * UI_LIST_ROWS;
    for (int i = 0; i < UI_LIST_ROWS && !l->loading && !l->failed && first + i < l->count; i++) {
        const UiAppItem* it = &l->items[first + i];
        int idx = first + i;
        float y0 = 100.0f + (float)i * ROW_PITCH, cy = y0 + ROW_H * 0.5f;
        row_card(&dc, y0, l->selected == idx, dc_hover(&dc, UI_HIT_LIST_ITEM, idx, -1));
        // a tile with the app's first letter, since there is no art to fetch here
        dc_fill_rrect(&dc, ROW_X0 + 18.0f, cy - 26.0f, ROW_X0 + 70.0f, cy + 26.0f, 12.0f, 0x30FFFFFFu);
        char init[5] = { 0 };
        uint32_t cp = 0;
        if (it->name[0]) {
            ui_utf8_decode(it->name, it->name + strlen(it->name), &cp);
            if (cp >= 'a' && cp <= 'z') cp -= 32;
            ui_utf8_encode(cp, init);
        }
        dc_text_center(&dc, UI_WEIGHT_MEDIUM, 28.0f, ROW_X0 + 44.0f, cy, 0xFFFFFFFFu, init);
        dc_text_fit_left(&dc, UI_WEIGHT_REGULAR, 28.0f, ROW_X0 + 94.0f, cy + 10.0f, 640.0f, 0xFFFFFFFFu, it->name);
        if (it->running) {
            const char* word = ui_str(UIS_NATIVE_APP_RUNNING);
            float tw = dc_text_width(&dc, UI_WEIGHT_REGULAR, 18.0f, word) + 28.0f;
            float x1 = ROW_X1 - 70.0f, x0 = x1 - tw;
            dc_stroke_rrect(&dc, x0, cy - 16.0f, x1, cy + 16.0f, 16.0f, 2.0f, 0xFF4ADE80u);
            dc_text_center(&dc, UI_WEIGHT_REGULAR, 18.0f, (x0 + x1) * 0.5f, cy, 0xFF4ADE80u, word);
        }
        chevron_right(&dc, ROW_X1 - 34.0f, cy, 0xB0FFFFFFu);
        dc_hit(&dc, UI_HIT_LIST_ITEM, idx, 0, ROW_X0, y0, ROW_X1, y0 + ROW_H, 1);
    }
    footer_button(&dc, UI_BTN_LIST_BACK, ROW_X0, 250.0f, ui_str(UIS_NATIVE_BACK), 1, dc_hover(&dc, UI_HIT_BUTTON, UI_BTN_LIST_BACK, -1));
    page_controls(&dc, l->page, l->loading || l->failed ? 0 : l->count, &l->hover);
}
