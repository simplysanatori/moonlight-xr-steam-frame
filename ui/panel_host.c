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
#define DIM 0x70FFFFFFu

static void rectPx(const Dc* dc, const float r[4], float* x0, float* y0, float* x1, float* y1) {
    *x0 = r[0] * dc->nw;
    *y0 = r[1] * dc->nh;
    *x1 = r[2] * dc->nw;
    *y1 = r[3] * dc->nh;
}

// One cell: a rounded outline, the keys' name (at size px), and under it what they do
static void cell(Dc* dc, int index, const char* label, float size, const char* hint, uint32_t ink, int hot) {
    float r[4];
    float x0, y0, x1, y1;
    xrHostCellRect(index, r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    if (hot) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, (ink & 0x00FFFFFFu) | 0x38000000u);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 14.0f, hot ? 4.0f : 2.5f, ink);
    float cx = (x0 + x1) * 0.5f, w = (x1 - x0) - 20.0f;
    if (hint != NULL && hint[0] != '\0') {
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, size, cx, y0 + (y1 - y0) * 0.38f, w, ink, label);
        // the hint is fainter than the label, also on a dimmed cell (the PC page's DIM)
        uint32_t a = ink >> 24, ha = a >= 0xEEu ? 0x90u : a * 0x90u / 0xEEu;
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 17.0f, cx, y0 + (y1 - y0) * 0.74f, w,
                           (ink & 0x00FFFFFFu) | (ha << 24), hint);
    } else {
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, size, cx, (y0 + y1) * 0.5f, w, ink, label);
    }
}

// Native (lane wave4-apollo): the PC page, its clipboard entries and the host's commands. An entry the
// host cannot do is drawn dim but stays a hit, so a press can say why not.
static void pcPage(Dc* dc, int confirm) {
    const UiView* v = dc->v;
    const XrHostInfo* info = &v->host_info;
    int cmds = info->cmdCount < 0 ? 0 : info->cmdCount > XR_HOST_CMD_SLOTS ? XR_HOST_CMD_SLOTS : info->cmdCount;
    float r[4];
    float x0, y0, x1, y1;
    for (int i = 0; i < XR_HOST_CELLS; i++) {
        int kind = xrHostPcCellKind(i, cmds);
        if (kind == XR_HOST_CELL_NONE) continue;
        // Native (lane fix-polish): the cells say only their names
        const char* label;
        uint32_t ink = INK;
        if (kind == XR_HOST_CELL_PASTE) {
            label = ui_str(UIS_HOST_PASTE);
            if (!info->clipRead) ink = DIM;
        } else if (kind == XR_HOST_CELL_COPY) {
            label = ui_str(UIS_HOST_COPY);
            if (!info->clipSet) ink = DIM;
        } else {
            label = info->cmd[i - XR_HOST_PC_CMD_FIRST];
        }
        // a label a little too long for the cell ("Send text to PC", a host's command name) gets smaller
        // before it is cut short
        xrHostCellRect(i, r);
        float room = (r[2] - r[0]) * dc->nw - 20.0f, size = 25.0f;
        while (size > 19.0f && dc_text_width(dc, UI_WEIGHT_REGULAR, size, label) > room) size -= 1.0f;
        cell(dc, i, label, size, NULL, ink, !confirm && dc_hover(dc, UI_HIT_HOST_CELL, i, -1));
        if (!confirm) {
            dc_hit_uv(dc, UI_HIT_HOST_CELL, i, kind, r[0], r[1], r[2], r[3], 1);
        }
    }
    // Why there is nothing to run, where the cells below the clipboard ones would be
    UiStr why = UIS_HOST_PC_NO_COMMANDS;
    int say = 0;
    if (!info->apollo) {
        why = UIS_HOST_PC_NOT_APOLLO;
        say = 1;
    } else if (info->cmdWhy == XR_HOSTINFO_NO_PERMISSION) {
        why = UIS_HOST_PC_NO_PERMISSION;
        say = 1;
    } else if (cmds == 0) {
        say = 1;
    }
    if (say) {
        xrHostCellRect(XR_HOST_PC_CMD_FIRST, r);
        rectPx(dc, r, &x0, &y0, &x1, &y1);
        dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 21.0f, 34.0f, (y0 + y1) * 0.5f + 8.0f, dc->nw - 68.0f,
                         0x80FFFFFFu, ui_str(why));
    }
}

void panel_host(Dc* dc) {
    const UiView* v = dc->v;
    int confirm = v->host_confirm >= 0;
    int pc = v->host_page == XR_HOST_PAGE_PC;
    int macros = v->host_macros < 0 ? 0 : v->host_macros > XR_HOST_MACRO_SLOTS ? XR_HOST_MACRO_SLOTS : v->host_macros;
    float r[4];
    float x0, y0, x1, y1;

    dc_sheet(dc, 32.0f, 0xF0141416u);
    dc_text_left_mid(dc, UI_WEIGHT_REGULAR, pc ? 30.0f : 38.0f, 34.0f, 46.0f, 0xFFFFFFFFu,
                     ui_str(pc ? UIS_HOST_PC_TITLE : UIS_HOST_TITLE));

    // Close, top right
    xrHostCloseRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 14.0f, 3.0f, INK, ui_str(UIS_HOST_CLOSE), 24.0f,
              !confirm && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CLOSE, -1));
    if (!confirm) dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CLOSE, 0, r[0], r[1], r[2], r[3], 1);

    // The button beside it between the main page and the PC page
    xrHostPageRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 14.0f, 3.0f, INK, ui_str(pc ? UIS_HOST_PAGE_BACK : UIS_HOST_PAGE_PC), 24.0f,
              !confirm && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_PAGE, -1));
    if (!confirm) dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_PAGE, 0, r[0], r[1], r[2], r[3], 1);

    // PC VR (PLAN 4.1), left of Close on both pages: hands the headset to Steam Link, after asking
    xrHostPcvrRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 14.0f, 3.0f, INK, ui_str(UIS_HOST_PCVR), 24.0f,
              !confirm && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_PCVR, -1));
    if (!confirm) dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_PCVR, 0, r[0], r[1], r[2], r[3], 1);

    if (pc) {
        pcPage(dc, confirm);
    }
    for (int i = 0; i < XR_HOST_CELLS && !pc; i++) {
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
        cell(dc, i, label, 25.0f, hint, ink, !confirm && dc_hover(dc, UI_HIT_HOST_CELL, i, -1));
        if (!confirm) {
            xrHostCellRect(i, r);
            dc_hit_uv(dc, UI_HIT_HOST_CELL, i, kind, r[0], r[1], r[2], r[3], 1);
        }
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
    if (pcvr) snprintf(q, sizeof q, "%s", ui_str(UIS_HOST_VR_Q));   // lane pcvr-steamvr
    else snprintf(q, sizeof q, ui_str(v->host_confirm_cmd ? UIS_HOST_CONFIRM_RUN_Q : UIS_HOST_CONFIRM_Q),
                  v->host_confirm_label);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 32.0f, (x0 + x1) * 0.5f, y0 + (y1 - y0) * 0.30f,
                       (x1 - x0) - 48.0f, 0xFFFFFFFFu, q);

    xrHostConfirmSendRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 16.0f, 3.0f, DANGER,
              ui_str(pcvr ? UIS_HOST_PCVR_GO : v->host_confirm_cmd ? UIS_HOST_CONFIRM_RUN : UIS_HOST_CONFIRM_SEND), 28.0f,
              dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CONFIRM, -1));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CONFIRM, 0, r[0], r[1], r[2], r[3], 1);

    xrHostConfirmCancelRect(r);
    rectPx(dc, r, &x0, &y0, &x1, &y1);
    dc_button(dc, x0, y0, x1, y1, 16.0f, 3.0f, INK, ui_str(UIS_CANCEL), 28.0f,
              dc_hover(dc, UI_HIT_BUTTON, UI_BTN_HOST_CANCEL, -1));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_HOST_CANCEL, 0, r[0], r[1], r[2], r[3], 1);
}
