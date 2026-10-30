// The bar: the row of buttons either side of the move pill under the picture,
// laid out as xr_layout's barSlotPlacement lays them out (fractions of the
// frame's width), drawn into one strip. XrPanels.java build*Button plus
// xr_assets.c's bar pill. GPLv3.
#include <math.h>

#include "panels_int.h"

// The strip covers this much of the frame's width: the outermost button's
// edge is 0.41 right of the centre (the Host button), and each hit zone
// reaches a little past that
#define STRIP_SPAN 0.86f

static const int FACE_CELL[BAR_SLOTS] = {
    BTN_CELL_ENV, BTN_CELL_COG, BTN_CELL_KB, BTN_CELL_EXIT, BTN_CELL_PAD, BTN_CELL_AIM,
    BTN_CELL_RAY, BTN_CELL_STEREO, BTN_CELL_HOST
};

void panel_bar(Dc* dc) {
    const UiView* v = dc->v;
    const float P = dc->nw / STRIP_SPAN;   // native px per frame width
    const float cy = dc->nh * 0.5f;

    // the move pill: white, a pill 0.14 wide and 256:24 in proportion
    float pw = BAR_WIDTH_FRAC * P, ph = BAR_HEIGHT_FRAC * P;
    dc_fill_rrect(dc, dc->nw * 0.5f - pw * 0.5f + 1, cy - ph * 0.5f + 1, dc->nw * 0.5f + pw * 0.5f - 1,
                  cy + ph * 0.5f - 1, ph * 0.5f, 0xEBFFFFFFu);
    float reach = BAR_WIDTH_FRAC * BAR_HOVER * 0.5f * P;
    dc_hit(dc, UI_HIT_BAR_MOVE, 0, 0, dc->nw * 0.5f - reach, 0, dc->nw * 0.5f + reach, dc->nh, 1);

    for (int slot = 0; slot < BAR_SLOTS; slot++) {
        if (slot == BAR_SLOT_AIM && !v->bar_show_aim) continue;
        if (slot == BAR_SLOT_STEREO && !v->bar_show_stereo) continue;
        Vec3 local;
        float side;
        barSlotPlacement(slot, 1.0f, 1.0f, &local, &side);
        float cx = dc->nw * 0.5f + local.x * P;
        float sp = side * P;
        int cell = FACE_CELL[slot];
        int on = 0;
        if (slot == BAR_SLOT_STEREO) on = v->session.stereo_on;
        else if (slot == BAR_SLOT_RAY) on = v->session.ray;
        else if (slot == BAR_SLOT_AIM) on = v->session.head_aim;
        else if (slot == BAR_SLOT_PAD) on = v->session.pad_mode;
        if (dc_hover(dc, UI_HIT_BAR_BUTTON, slot, -1)) {
            dc_fill_rrect(dc, cx - sp * 0.5f, cy - sp * 0.5f, cx + sp * 0.5f, cy + sp * 0.5f, sp * 0.17f, 0x30FFFFFFu);
        }
        // native (PLAN 4.3): the 3D button says which pair it shows (3D, SBS, TB)
        if (slot == BAR_SLOT_STEREO) dc_button_face(dc, barStereoCell(v->stereo_source, on), cx - sp * 0.5f, cy - sp * 0.5f, sp);
        else dc_button_face(dc, cell + on, cx - sp * 0.5f, cy - sp * 0.5f, sp);

        // reaches a little past the art like barSlotHit, but never into a neighbour
        float half = side * HOVER_MARGIN * 0.5f;
        float pitch2 = (side + ENV_GAP_FRAC) * 0.5f;
        if (half > pitch2) half = pitch2;
        dc_hit(dc, UI_HIT_BAR_BUTTON, slot, 0, cx - half * P, 0, cx + half * P, dc->nh, 1);
    }
}

void panel_button_atlas(Dc* dc) {
    for (int cell = 0; cell < BTN_CELLS; cell++) {
        dc_button_face(dc, cell, (float)cell * BUTTON_TEX, 0.0f, (float)BUTTON_TEX);
    }
}
