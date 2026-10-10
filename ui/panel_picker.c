// The environment picker: a header strip and a row of tiles. Colours, sizes
// and labels from XrPanels.buildPickerGrid; the selection and hover rings that
// upstream draws as separate outline quads are drawn here. GPLv3.
#include "panels_int.h"

static const UiStr NAMES[ENV_CELL_COUNT] = { UIS_ENV_PASSTHROUGH, UIS_ENV_VOID, UIS_ENV_HOME_THEATER,
                                             UIS_ENV_GRAND_CINEMA, UIS_ENV_SYNTHWAVE };
static const uint32_t GROUND[ENV_CELL_COUNT] = { 0xFF2A3540u, 0xFF090909u, 0xFF14110Fu, 0xFF1A0A0Bu,
                                                 0xFF140B20u };
static const char* const THUMB[ENV_CELL_COUNT] = { NULL, NULL, "rooms/thumbs/home_theater.jpg",
                                                   "rooms/thumbs/grand_cinema.jpg",
                                                   "rooms/thumbs/synthwave.jpg" };

void panel_picker(Dc* dc) {
    const float cell_w = (float)PICKER_TEX_W / PICKER_COLS;
    const float pad = 7.0f;
    const float radius = cell_w * 0.125f;

    dc_fill_rrect(dc, 1.0f, 1.0f, dc->nw - 1.0f, dc->nh - 1.0f, radius * 0.6f, 0xE0141416u);
    dc_text_center(dc, UI_WEIGHT_REGULAR, 22.0f, dc->nw * 0.5f, PICKER_HEADER_PX * 0.5f, 0xB0FFFFFFu,
                   ui_str(UIS_ROOMS));

    for (int cell = 0; cell < ENV_CELL_COUNT; cell++) {
        float left = (float)(cell % PICKER_COLS) * cell_w;
        float top = (float)(cell / PICKER_COLS) * PICKER_BAND_PX + PICKER_HEADER_PX;
        float x0 = left + pad, y0 = top + pad, x1 = left + cell_w - pad, y1 = top + PICKER_CELL_PX - pad;

        dc_fill_rrect(dc, x0, y0, x1, y1, radius, GROUND[cell]);
        if (THUMB[cell]) {
            const UiImage* img = ui_asset_image(THUMB[cell]);
            if (img) {
                ui_image_cover(dc->cv, img->rgba, img->w, img->h, x0 * dc->kx, y0 * dc->ky,
                               x1 * dc->kx, y1 * dc->ky, radius * dc->k);
            }
        }
        // dark band under the label, clipped to the bottom of the tile so it
        // keeps the rounded corners it sits in
        ui_cv_set_clip(dc->cv, x0 * dc->kx, (y1 - 44.0f) * dc->ky, x1 * dc->kx, y1 * dc->ky);
        dc_fill_rrect(dc, x0, y0, x1, y1, radius, 0xC0000000u);
        ui_cv_reset_clip(dc->cv);
        dc_stroke_rrect(dc, x0, y0, x1, y1, radius, 2.0f, 0x50FFFFFFu);

        // a long name is drawn smaller rather than run into the tile's edge
        const char* name = ui_str(NAMES[cell]);
        float size = 21.0f;
        while (size > 15.0f && dc_text_width(dc, UI_WEIGHT_REGULAR, size, name) > (x1 - x0) - 16.0f) size -= 1.0f;
        dc_text(dc, UI_WEIGHT_REGULAR, size, (x0 + x1) * 0.5f, y1 - 15.0f, UI_ALIGN_CENTER, 0xFFFFFFFFu, name);

        if (dc->v->env_selected == cell) {
            dc_stroke_rrect(dc, x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y1 + 1.0f, radius + 1.0f, 3.5f, 0xFFFFFFFFu);
        } else if (dc_hover(dc, UI_HIT_ENV_TILE, cell, -1)) {
            dc_stroke_rrect(dc, x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y1 + 1.0f, radius + 1.0f, 2.5f, 0xB0FFFFFFu);
        }
        // upstream hit tests the whole cell, not the padded tile
        dc_hit(dc, UI_HIT_ENV_TILE, cell, 0, left, top, left + cell_w, top + PICKER_CELL_PX, 1);
    }
}
