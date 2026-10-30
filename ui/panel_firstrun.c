// The first-run controls card (PLAN 3.9): one sheet on the first stream that says where the
// bar is, what opens the cog and the keyboard, how to switch to gamepad mode (worded for the
// shortcut config.ini has), the Host button, the keyboard chord that disconnects, and the
// gamepad-as-mouse chord worded for the pad in use. No buttons: it goes by itself (see
// xr_cards.h) or when a menu opens. Native, no upstream counterpart. GPLv3.
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

#define FIRSTRUN_ROWS 7

void panel_firstrun(Dc* dc) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    const float left = 48.0f;
    const float labelW = 180.0f;
    const float textX = left + labelW + 22.0f;
    const float textW = dc->nw - textX - left;

    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 40.0f, left, 78.0f, dc->nw - 2.0f * left, 0xFFFFFFFFu,
                     ui_str(UIS_FIRSTRUN_TITLE));

    UiStr padText = UIS_FIRSTRUN_PAD_MENU_GRIP;
    if (dc->v->settings) {
        switch (dc->v->settings->gamepad_toggle) {
        case PAD_SHORTCUT_STICKS: padText = UIS_FIRSTRUN_PAD_STICKS; break;
        case PAD_SHORTCUT_TRIGGERS_GRIPS: padText = UIS_FIRSTRUN_PAD_TRIGGERS_GRIPS; break;
        default: break;
        }
    }
    char mouse[160];
    snprintf(mouse, sizeof mouse, ui_str(UIS_FIRSTRUN_MOUSE), ui_pad_start_name(dc->v->pad.style));

    struct { UiStr label; const char* text; } rows[FIRSTRUN_ROWS] = {
        { UIS_FIRSTRUN_BAR_L, ui_str(UIS_FIRSTRUN_BAR) },
        { UIS_FIRSTRUN_COG_L, ui_str(UIS_FIRSTRUN_COG) },
        { UIS_FIRSTRUN_KB_L, ui_str(UIS_FIRSTRUN_KB) },
        { UIS_FIRSTRUN_PAD_L, ui_str(padText) },
        { UIS_FIRSTRUN_HOST_L, ui_str(UIS_FIRSTRUN_HOST) },
        { UIS_FIRSTRUN_KEYS_L, ui_str(UIS_FIRSTRUN_KEYS) },
        { UIS_FIRSTRUN_MOUSE_L, mouse },
    };
    const float top = 112.0f;
    const float pitch = 56.0f;
    for (int i = 0; i < FIRSTRUN_ROWS; i++) {
        float y = top + (float)i * pitch;
        if (i > 0) {
            dc_line(dc, left, y, dc->nw - left, y, 1.5f, 0x22FFFFFFu);
        }
        float base = y + 37.0f;
        dc_text_fit_left(dc, UI_WEIGHT_MEDIUM, 26.0f, left, base, labelW, 0xFF8CC8FFu, ui_str(rows[i].label));
        dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 26.0f, textX, base, textW, 0xEEFFFFFFu, rows[i].text);
    }
    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 22.0f, left, dc->nh - 26.0f, dc->nw - 2.0f * left, 0x99FFFFFFu,
                     ui_str(UIS_FIRSTRUN_FOOT));
}
