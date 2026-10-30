// The Host sheet (PLAN 4b.1, 4b.2): a grid of keys to send to the PC. The 11
// quick actions, "Send keys" (which opens the keyboard to type one combo), and
// the user's macros from macros.ini. Where the cells and zones sit is
// xr_hostkeys.c's, so what is drawn and what the ray hits agree. A key with a
// hard to undo effect (Alt+F4) asks first, on the same sheet. GPLv3.
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

static const UiStr HINT[XR_HOST_QUICK_COUNT] = {
    UIS_HOST_HINT_ESC, UIS_HOST_HINT_WIN, UIS_HOST_HINT_DESKTOP, UIS_HOST_HINT_FULLSCREEN,
    UIS_HOST_HINT_SWITCHER, UIS_HOST_HINT_STEAM, UIS_HOST_HINT_GAMEBAR, UIS_HOST_HINT_MOVE,
    UIS_HOST_HINT_PASTE, UIS_HOST_HINT_TASKMGR, UIS_HOST_HINT_CLOSE_WINDOW,
};

#define INK 0xEEFFFFFFu
#define DANGER 0xFFE05A5Au

static void rectPx(const Dc* dc, const float r[4], float* x0, float* y0, float* x1, float* y1) {
    *x0 = r[0] * dc->nw;
    *y0 = r[1] * dc->nh;
    *x1 = r[2] * dc->nw;
    *y1 = r[3] * dc->nh;
}

// One cell: a rounded outline, the keys' name, and under it what they do
static void cell(Dc* dc, int index, const char* label, const char* hint, uint32_t ink, int hot) {
    float r[4];
    float x0, y0, x1, y1;
    xrHostCellRect(index, r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    if (hot) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, (ink & 0x00FFFFFFu) | 0x38000000u);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 14.0f, hot ? 4.0f : 2.5f, ink);
    float cx = (x0 + x1) * 0.5f, w = (x1 - x0) - 20.0f;
    if (hint != NULL && hint[0] != '\0') {
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 25.0f, cx, y0 + (y1 - y0) * 0.38f, w, ink, label);
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 17.0f, cx, y0 + (y1 - y0) * 0.74f, w,
                           (ink & 0x00FFFFFFu) | 0x90000000u, hint);
    } else {
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 25.0f, cx, (y0 + y1) * 0.5f, w, ink, label);
    }
}

void panel_host(Dc* dc) {
    const UiView* v = dc->v;
    int confirm = v->host_confirm >= 0;
    int macros = v->host_macros < 0 ? 0 : v->host_macros > XR_HOST_MACRO_SLOTS ? XR_HOST_MACRO_SLOTS : v->host_macros;
    float r[4];
    float x0, y0, x1, y1;

    dc_sheet(dc, 32.0f, 0xF0141416u);
    dc_text_left_mid(dc, UI_WEIGHT_REGULAR, 38.0f, 34.0f, 46.0f, 0xFFFFFFFFu, ui_str(UIS_HOST_TITLE));

    // Close, top right
    xrHostCloseRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 14.0f, 3.0f, INK, ui_str(UIS_HOST_CLOSE), 24.0f,
              !confirm && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CLOSE, -1));
    if (!confirm) dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CLOSE, 0, r[0], r[1], r[2], r[3], 1);

    // PC VR (PLAN 4.1), left of Close: hands the headset to Steam Link, after asking
    xrHostPcvrRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 14.0f, 3.0f, INK, ui_str(UIS_HOST_PCVR), 24.0f,
              !confirm && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_PCVR, -1));
    if (!confirm) dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_PCVR, 0, r[0], r[1], r[2], r[3], 1);

    for (int i = 0; i < XR_HOST_CELLS; i++) {
        int kind = xrHostCellKind(i, macros);
        if (kind == XR_HOST_CELL_NONE) continue;
        const char* label;
        const char* hint = NULL;
        uint32_t ink = INK;
        if (kind == XR_HOST_CELL_QUICK) {
            label = xrHostQuick(i)->label;
            hint = ui_str(HINT[i]);
            if (xrHostQuick(i)->confirm) ink = DANGER;
        } else if (kind == XR_HOST_CELL_KEYS) {
            label = ui_str(UIS_HOST_SEND_KEYS);
            hint = ui_str(UIS_HOST_SEND_KEYS_HINT);
        } else {
            label = v->host_macro[i - XR_HOST_MACRO_FIRST];
        }
        cell(dc, i, label, hint, ink, !confirm && dc_hover(dc, UI_HIT_HOST_CELL, i, -1));
        if (!confirm) {
            xrHostCellRect(i, r);
            dc_hit_uv(dc, UI_HIT_HOST_CELL, i, kind, r[0], r[1], r[2], r[3], 1);
        }
    }
    if (macros == 0) {
        // where the macros would be
        xrHostCellRect(XR_HOST_MACRO_FIRST + 3, r);
        rectPx(dc, r, &x0, &y0, &x1, &y1);
        dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 21.0f, 34.0f, (y0 + y1) * 0.5f + 8.0f, dc->nw - 68.0f,
                         0x80FFFFFFu, ui_str(UIS_HOST_NO_MACROS));
    }

    if (!confirm) return;

    // The question: the grid dimmed, a box over it with Send and Cancel
    dc_fill_rrect(dc, 1.0f, 88.0f, dc->nw - 1.0f, dc->nh - 1.0f, 24.0f, 0xC8141416u);
    xrHostConfirmBox(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_fill_rrect(dc, x0, y0, x1, y1, 24.0f, 0xFF232328u);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 24.0f, 3.0f, 0xFFB0B0B8u);
    char q[160];
    int pcvr = v->host_confirm == XR_HOST_CONFIRM_PCVR;
    if (pcvr) snprintf(q, sizeof q, "%s", ui_str(UIS_HOST_PCVR_Q));
    else snprintf(q, sizeof q, ui_str(UIS_HOST_CONFIRM_Q), v->host_confirm_label);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 32.0f, (x0 + x1) * 0.5f, y0 + (y1 - y0) * 0.30f,
                       (x1 - x0) - 48.0f, 0xFFFFFFFFu, q);

    xrHostConfirmSendRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 16.0f, 3.0f, DANGER, ui_str(pcvr ? UIS_HOST_PCVR_GO : UIS_HOST_CONFIRM_SEND), 28.0f,
              dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CONFIRM, -1));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CONFIRM, 0, r[0], r[1], r[2], r[3], 1);

    xrHostConfirmCancelRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 16.0f, 3.0f, INK, ui_str(UIS_CANCEL), 28.0f,
              dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CANCEL, -1));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CANCEL, 0, r[0], r[1], r[2], r[3], 1);
}
