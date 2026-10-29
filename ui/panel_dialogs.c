// The exit prompt, the hand lock hint, the Ko-fi sheet. All on the dark sheet
// with white strokes, from XrPanels.buildExitPrompt / buildHandHint /
// buildKofiSheet. GPLv3.
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

void panel_exit(Dc* dc) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    // Native: during a stream the prompt has a third choice, Disconnect, which
    // leaves the stream and keeps the app running on the PC; the question
    // moves up for the line that says so
    int three = dc->v->exit_disconnect;
    dc_text_center(dc, UI_WEIGHT_REGULAR, 34.0f, dc->nw * 0.5f, dc->nh * (three ? 0.22f : 0.30f), 0xFFFFFFFFu,
                   dc->v->exit_question ? dc->v->exit_question : ui_str(UIS_EXIT_QUESTION));
    typedef struct { float l, r; UiStr label; uint32_t colour; int btn; } ExitBtn;
    ExitBtn B[3];
    int n = 0;
    if (three) {
        B[n++] = (ExitBtn){ EXIT3_QUIT_L, EXIT3_QUIT_R, UIS_EXIT_QUIT_APP, 0xFFE05A5Au, UI_BTN_EXIT_CONFIRM };
        B[n++] = (ExitBtn){ EXIT3_DISC_L, EXIT3_DISC_R, UIS_EXIT_DISCONNECT, 0xFFF0B040u, UI_BTN_EXIT_DISCONNECT };
        B[n++] = (ExitBtn){ EXIT3_CANCEL_L, EXIT3_CANCEL_R, UIS_CANCEL, 0xEEFFFFFFu, UI_BTN_EXIT_CANCEL };
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 20.0f, dc->nw * 0.5f, dc->nh * 0.40f, dc->nw * 0.92f,
                           0x99FFFFFFu, ui_str(UIS_EXIT_DISCONNECT_HINT));
    } else {
        B[n++] = (ExitBtn){ EXIT_EXIT_L, EXIT_EXIT_R, UIS_EXIT, 0xFFE05A5Au, UI_BTN_EXIT_CONFIRM };
        B[n++] = (ExitBtn){ EXIT_CANCEL_L, EXIT_CANCEL_R, UIS_CANCEL, 0xEEFFFFFFu, UI_BTN_EXIT_CANCEL };
    }
    for (int i = 0; i < n; i++) {
        float x0 = B[i].l * dc->nw, x1 = B[i].r * dc->nw, y0 = EXIT_BTN_T * dc->nh, y1 = EXIT_BTN_B * dc->nh;
        int hot = dc_hover(dc, UI_HIT_BUTTON, B[i].btn, -1);
        // hovering fills it, which says which of them a press would land on
        dc_button(dc, x0, y0, x1, y1, 16.0f, 3.0f, B[i].colour, ui_str(B[i].label), three ? 24.0f : 30.0f, hot);
        dc_hit_uv(dc, UI_HIT_BUTTON, B[i].btn, 0, B[i].l, EXIT_BTN_T, B[i].r, EXIT_BTN_B, 1);
    }
}

// Wraps text to width (native px), shrinking the size from size to min until
// lines * size * 1.3 fits room. Returns the size; *n lines in lines[].
static float fit_lines(Dc* dc, const char* text, float width, float room, float size, float min,
                       UiTextLine* lines, int* n) {
    float s = size;
    for (;;) {
        *n = ui_text_wrap(dc->font, UI_WEIGHT_REGULAR, s * dc->k, text, width * dc->k, lines, 12);
        if (*n > 12) *n = 12;
        if ((float)*n * s * 1.3f <= room || s <= min) break;
        s -= 2.0f;
    }
    return s;
}

static void draw_line(Dc* dc, float size, float x, float baseline, uint32_t c, const char* text,
                      const UiTextLine* ln) {
    char buf[512];
    int len = ln->len < 511 ? ln->len : 511;
    memcpy(buf, text + ln->start, (size_t)len);
    buf[len] = 0;
    dc_text(dc, UI_WEIGHT_REGULAR, size, x, baseline, UI_ALIGN_LEFT, c, buf);
}

void panel_hint(Dc* dc) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    float left = HINT_OK_L * dc->nw;
    float width = (HINT_NEVER_R - HINT_OK_L) * dc->nw;
    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 40.0f, left, 0.17f * dc->nh, width, 0xFFFFFFFFu,
                     ui_str(UIS_HAND_HINT_TITLE));
    const char* said = ui_str(UIS_HAND_HINT_BODY);
    float top = 0.27f * dc->nh;
    float room = (HINT_BTN_T - 0.06f) * dc->nh - top;
    UiTextLine lines[12];
    int n;
    float size = fit_lines(dc, said, width, room, 30.0f, 20.0f, lines, &n);
    for (int i = 0; i < n; i++) {
        draw_line(dc, size, left, top + size + (float)i * size * 1.3f, 0xCCFFFFFFu, said, &lines[i]);
    }
    struct { float l, r; UiStr label; int btn; } B[2] = {
        { HINT_OK_L, HINT_OK_R, UIS_OK, UI_BTN_HINT_OK },
        { HINT_NEVER_L, HINT_NEVER_R, UIS_HAND_HINT_NEVER, UI_BTN_HINT_NEVER },
    };
    for (int i = 0; i < 2; i++) {
        int hot = dc_hover(dc, UI_HIT_BUTTON, B[i].btn, -1);
        dc_button(dc, B[i].l * dc->nw, HINT_BTN_T * dc->nh, B[i].r * dc->nw, HINT_BTN_B * dc->nh, 16.0f,
                  3.0f, 0xEEFFFFFFu, ui_str(B[i].label), 30.0f, hot);
        dc_hit_uv(dc, UI_HIT_BUTTON, B[i].btn, 0, B[i].l, HINT_BTN_T, B[i].r, HINT_BTN_B, 1);
    }
}

void panel_kofi(Dc* dc) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    float qr_left = (float)(int)(KOFI_QR_L * dc->nw + 0.5f);
    float qr_top = (float)(int)(KOFI_QR_T * dc->nh + 0.5f);
    float right = KOFI_CLOSE_R * dc->nw;
    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 40.0f, qr_left, 0.14f * dc->nh, right - qr_left, 0xFFFFFFFFu,
                     ui_str(UIS_KOFI_TITLE));

    // the code goes on a pixel for a pixel, on white even if it failed to load
    dc_fill_rrect(dc, qr_left, qr_top, qr_left + KOFI_QR_PX, qr_top + KOFI_QR_PX, 0.0f, 0xFFFFFFFFu);
    const UiImage* qr = ui_asset_image("images/kofi_qr.png");
    if (qr) {
        ui_image_nearest(dc->cv, qr->rgba, qr->w, qr->h, qr_left * dc->kx, qr_top * dc->ky,
                         (qr_left + KOFI_QR_PX) * dc->kx, (qr_top + KOFI_QR_PX) * dc->ky);
    }

    float column = qr_left + KOFI_QR_PX + 48.0f;
    float width = right - column;
    const char* url = dc->v->kofi_url ? dc->v->kofi_url : "https://ko-fi.com/moonlightxr";
    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 30.0f, column, qr_top + 36.0f, width, 0xFFFFFFFFu, url);

    char said[512];
    snprintf(said, sizeof said, "%s\n%s", ui_str(UIS_KOFI_SCAN), ui_str(UIS_KOFI_FREE));
    float top = qr_top + 72.0f;
    float room = (KOFI_BTN_T - 0.04f) * dc->nh - top;
    UiTextLine lines[12];
    int n;
    float size = fit_lines(dc, said, width, room, 28.0f, 20.0f, lines, &n);
    UiTextLine scan_lines[12];
    int scan = ui_text_wrap(dc->font, UI_WEIGHT_REGULAR, size * dc->k, ui_str(UIS_KOFI_SCAN),
                            width * dc->k, scan_lines, 12);
    for (int i = 0; i < n; i++) {
        draw_line(dc, size, column, top + size + (float)i * size * 1.3f,
                  i < scan ? 0xDDFFFFFFu : 0x99FFFFFFu, said, &lines[i]);
    }
    int hot = dc_hover(dc, UI_HIT_BUTTON, UI_BTN_KOFI_CLOSE, -1);
    dc_button(dc, KOFI_CLOSE_L * dc->nw, KOFI_BTN_T * dc->nh, KOFI_CLOSE_R * dc->nw, KOFI_BTN_B * dc->nh,
              16.0f, 3.0f, 0xEEFFFFFFu, ui_str(UIS_KOFI_CLOSE), 30.0f, hot);
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_KOFI_CLOSE, 0, KOFI_CLOSE_L, KOFI_BTN_T, KOFI_CLOSE_R, KOFI_BTN_B, 1);
}
