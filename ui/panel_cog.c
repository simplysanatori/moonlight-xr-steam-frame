// The settings panel: five tabs (Screen, Display, 3D, Picture, About) and the
// Room tab that takes the Screen tab's place in a room. XrPanels.buildCogSheet
// and its drawCog* helpers, with the things upstream puts on separate quads
// (thumbs, the Display tab's marks, the Room/Picture/Screen readouts, the
// preset and switch rings) drawn into the same buffer from the settings, so
// one upload is the whole panel. Geometry is xr_shared.h's COG_ constants and
// xr_layout's cogRowV/cogRowHalf/cogCellHalf/cogThumbSize. GPLv3.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

#define LABEL_X 0.06f
#define LABEL_SIZE 22.0f
#define WHITE 0xFFFFFFFFu
#define DIM 0x30FFFFFFu

static float row_y(Dc* dc, int face, int row) { return cogRowV(face, row) * dc->nh; }

// Native (lane wave6-i18n): a row's label is drawn once the row knows its room: up to the row's
// readout when it has one (readout below), else up to the track or the cells. A longer label (a
// translation) shrinks to fit, as the cells' names do; English labels fit and are drawn as before.
#define LABEL_MIN_SIZE 14.0f
static void label_flush(Dc* dc, float right) {
    const char* text = dc->label_text;
    if (!text) return;
    dc->label_text = NULL;
    float room = right - LABEL_X * dc->nw;
    float size = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, LABEL_SIZE * dc->k, LABEL_MIN_SIZE * dc->k, text,
                                  room * dc->k) / dc->k;
    dc_text_left_mid(dc, UI_WEIGHT_REGULAR, size, LABEL_X * dc->nw, dc->label_y, dc->label_live ? WHITE : DIM, text);
}

// Where a label may reach on a row without a readout: short of the track's step button (and of the
// cells, which start further right)
static float label_right(Dc* dc) { return (COG_TRACK_L - COG_CHEVRON_REACH) * dc->nw - 6.0f; }

static void label(Dc* dc, const char* text, float y, int live) {
    label_flush(dc, label_right(dc));
    dc->label_text = text;
    dc->label_y = y;
    dc->label_live = live;
}

// Why a row is greyed, across where its track or cells would be
static void greyed_reason(Dc* dc, float y, const char* reason) {
    dc_text_center(dc, UI_WEIGHT_REGULAR, 19.0f, (COG_TRACK_L + COG_TRACK_R) * 0.5f * dc->nw, y, 0x50FFFFFFu, reason);
}

// A step button at each end of a track: a narrow cell with a chevron in it
static void chevrons(Dc* dc, float y, int live, float cell_half) {
    float width = COG_CHEVRON_W * dc->nw;
    float lefts[2] = { COG_TRACK_L * dc->nw, COG_TRACK_R * dc->nw - width };
    uint32_t ink = live ? 0xEBFFFFFFu : 0x30FFFFFFu;
    for (int side = 0; side < 2; side++) {
        float x0 = lefts[side], x1 = x0 + width, cx = (x0 + x1) * 0.5f;
        dc_fill_rrect(dc, x0, y - cell_half, x1, y + cell_half, 8.0f, live ? 0x28FFFFFFu : 0x10FFFFFFu);
        dc_stroke_rrect(dc, x0, y - cell_half, x1, y + cell_half, 8.0f, 2.0f, live ? 0x50FFFFFFu : 0x20FFFFFFu);
        float dir = side == 0 ? -1.0f : 1.0f;   // points out of the track
        float reach = width * 0.16f, rise = cell_half * 0.42f;
        float pts[6] = { cx - dir * reach, y - rise, cx + dir * reach, y, cx - dir * reach, y + rise };
        dc_polyline(dc, pts, 3, 4.0f, ink, 0);
    }
}

static void tick(Dc* dc, float x, float y, float half, int live) {
    dc_fill_rrect(dc, x - 2.0f, y - half, x + 2.0f, y + half, 0.0f, live ? 0xCCFFFFFFu : 0x30FFFFFFu);
}

static void track_line(Dc* dc, float y, uint32_t c) {
    dc_line(dc, COG_RUN_L * dc->nw, y, COG_RUN_R * dc->nw, y, 6.0f, c);
}

static void thumb(Dc* dc, int face, int row, float y, float t) {
    float r = 0.42f * cogThumbSize(face) * dc->nh;
    if (dc_hover(dc, UI_HIT_TRACK_RUN, row, -1)) r *= 1.15f;
    dc_fill_circle(dc, cogRunU(t) * dc->nw, y, r, 0xEBFFFFFFu);
}

// Hit zones of a track row: the two step buttons and the run between them
static void track_hits(Dc* dc, int face, int row, int live) {
    float rv = cogRowV(face, row), half = cogRowHalf(face);
    dc_hit_uv(dc, UI_HIT_TRACK_DOWN, row, 0, COG_TRACK_L - COG_CHEVRON_REACH, rv - half, COG_TRACK_L + COG_CHEVRON_W, rv + half, live);
    dc_hit_uv(dc, UI_HIT_TRACK_RUN, row, 0, COG_TRACK_L + COG_CHEVRON_W, rv - half, COG_TRACK_R - COG_CHEVRON_W, rv + half, live);
    dc_hit_uv(dc, UI_HIT_TRACK_UP, row, 0, COG_TRACK_R - COG_CHEVRON_W, rv - half, COG_TRACK_R + COG_CHEVRON_REACH, rv + half, live);
}

// A row of cells, one press wide each; mark is the cell in force (ringed), or -1
static void cells(Dc* dc, int face, int row, const UiStr* names, int n, float y, int live, float cell_half, int mark) {
    float track_l = COG_TRACK_L * dc->nw, track_r = COG_TRACK_R * dc->nw;
    float span = (track_r - track_l) / (float)n;
    float rv = cogRowV(face, row), half = cogRowHalf(face);
    for (int i = 0; i < n; i++) {
        float x0 = track_l + (float)i * span + 3.0f, x1 = track_l + (float)(i + 1) * span - 3.0f;
        int hot = live && dc_hover(dc, UI_HIT_CELL, row, i);
        dc_fill_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, live ? (hot ? 0x40FFFFFFu : 0x28FFFFFFu) : 0x10FFFFFFu);
        dc_stroke_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, 2.0f, live ? 0x50FFFFFFu : 0x20FFFFFFu);
        // a long name (Bring here, in the Screen tab's four cells) shrinks to fit rather than being cut
        float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 19.0f * dc->k, 12.0f * dc->k, ui_str(names[i]),
                                     ((x1 - x0) - 8.0f) * dc->k) / dc->k;
        dc_text_center(dc, UI_WEIGHT_REGULAR, fit, (x0 + x1) * 0.5f, y, live ? WHITE : DIM, ui_str(names[i]));
        if (live && i == mark) {
            // on the edge of the cell the sheet draws, inset the same way
            dc_stroke_rrect(dc, x0 + 1.5f, y - cell_half + 1.5f, x1 - 1.5f, y + cell_half - 1.5f, 10.0f, 3.0f, WHITE);
        } else if (hot) {
            dc_stroke_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, 2.5f, 0xB0FFFFFFu);
        }
        dc_hit_uv(dc, UI_HIT_CELL, row, i, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)i / (float)n,
                  rv - half, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(i + 1) / (float)n, rv + half, live);
    }
}

static void reset_button(Dc* dc) {
    float x0 = COG_RESET_L * dc->nw, y0 = COG_RESET_T * dc->nh, x1 = COG_RESET_R * dc->nw, y1 = COG_RESET_B * dc->nh;
    int hot = dc_hover(dc, UI_HIT_BUTTON, UI_BTN_RESET, -1);
    if (hot) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x28FFFFFFu);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 14.0f, 4.0f, 0xEEFFFFFFu);
    dc_text_center(dc, UI_WEIGHT_REGULAR, LABEL_SIZE, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, WHITE, ui_str(UIS_RESET));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_RESET, 0, COG_RESET_L, COG_RESET_T, COG_RESET_R, COG_RESET_B, 1);
}

static void readout(Dc* dc, float row_v, const char* text) {
    float right = COG_READOUT_L * dc->nw + COG_READOUT_TEX_W - 8.0f;
    // the row's label stops short of the readout
    label_flush(dc, right - dc_text_width(dc, UI_WEIGHT_REGULAR, 20.0f, text) - 10.0f);
    UiFontMetrics m;
    ui_font_metrics(dc->font, UI_WEIGHT_REGULAR, 20.0f * dc->k, &m);
    dc_text(dc, UI_WEIGHT_REGULAR, 20.0f, right, row_v * dc->nh + m.cap_height * 0.5f / dc->k, UI_ALIGN_RIGHT,
            0xB0FFFFFFu, text);
}

// A line centred on x at a baseline, cut with an ellipsis to fit room (native px)
static void line_fit_center(Dc* dc, float size, float x, float baseline, float room, uint32_t c,
                            const char* text, UiEllipsize mode) {
    char cut[320];
    ui_text_ellipsize(dc->font, UI_WEIGHT_REGULAR, size * dc->k, text, room * dc->k, mode, cut, sizeof cut, NULL);
    dc_text(dc, UI_WEIGHT_REGULAR, size, x, baseline, UI_ALIGN_CENTER, c, cut);
}

// ---- chrome --------------------------------------------------------------------------------

static void chrome(Dc* dc, int tab, int in_room) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    static const UiStr TABS[COG_TAB_COUNT] = { UIS_TAB_SCREEN, UIS_TAB_DISPLAY, UIS_TAB_3D, UIS_TAB_PICTURE, UIS_TAB_ABOUT };
    float bar_b = COG_TAB_BAR_B * dc->nh;
    float slot_w = dc->nw / (float)COG_TAB_COUNT;
    for (int i = 0; i < COG_TAB_COUNT; i++) {
        int current = i == tab;
        float x0 = (float)i * slot_w + 12.0f, x1 = (float)(i + 1) * slot_w - 12.0f, y0 = 12.0f, y1 = bar_b - 8.0f;
        if (current) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x28FFFFFFu);
        else if (dc_hover(dc, UI_HIT_TAB, i, -1)) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x14FFFFFFu);
        const char* name = ui_str(in_room && i == COG_TAB_SCREEN ? UIS_TAB_ROOM : TABS[i]);
        dc_text_center(dc, UI_WEIGHT_REGULAR, 25.0f, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, current ? WHITE : 0x60FFFFFFu, name);
        if (current) {
            // the underline is what carries at a glance, the fill alone is too subtle
            dc_fill_rrect(dc, x0 + 24.0f, y1 - 4.0f, x1 - 24.0f, y1, 0.0f, 0xEEFFFFFFu);
        }
        dc_hit_uv(dc, UI_HIT_TAB, i, 0, (float)i / COG_TAB_COUNT, 0.0f, (float)(i + 1) / COG_TAB_COUNT, COG_TAB_BAR_B, 1);
    }
    dc_fill_rrect(dc, 20.0f, bar_b, dc->nw - 20.0f, bar_b + 2.0f, 0.0f, 0x30FFFFFFu);
}

// ---- tabs -------------------------------------------------------------------------------------

// The three rows of cells under the tracks: the presets with Bring here, the
// per app switch with Forget, and the angular-size lock. Bring here and Forget
// are presses rather than states, so neither is ever ringed.
static void screen_cells(Dc* dc, int row, float y, float cell_half) {
    const Settings* s = dc->v->settings;
    static const UiStr PRESET[COG_SCREEN_PRESET_CELLS] = { UIS_PRESET_DESK, UIS_PRESET_COUCH, UIS_PRESET_CINEMA,
                                                           UIS_BRING_HERE };
    static const UiStr PER_APP[COG_SCREEN_PER_APP_CELLS] = { UIS_OFF, UIS_ON, UIS_FORGET };
    static const UiStr LOCK[COG_SCREEN_LOCK_CELLS] = { UIS_OFF, UIS_ON };
    if (row == COG_SCREEN_ROW_PRESET) {
        cells(dc, COG_TAB_SCREEN, row, PRESET, COG_SCREEN_PRESET_CELLS, y, 1, cell_half,
              layoutPresetAt(s->screen_distance_cm, s->screen_width_cm));
    } else if (row == COG_SCREEN_ROW_PER_APP) {
        cells(dc, COG_TAB_SCREEN, row, PER_APP, COG_SCREEN_PER_APP_CELLS, y, 1, cell_half, s->per_app ? 1 : 0);
    } else {
        cells(dc, COG_TAB_SCREEN, row, LOCK, COG_SCREEN_LOCK_CELLS, y, 1, cell_half, s->angular_lock ? 1 : 0);
    }
}

static void tab_screen(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    static const UiStr ROWS[COG_SCREEN_ROW_COUNT] = { UIS_ROW_DISTANCE, UIS_ROW_HEIGHT, UIS_ROW_TILT,
        UIS_ROW_ROTATE, UIS_ROW_CURVE, UIS_ROW_SIZE, UIS_ROW_AIM_SENSITIVITY, UIS_ROW_AIM_DEAD_ZONE,
        UIS_ROW_PRESET, UIS_ROW_PER_APP, UIS_ROW_ANGULAR_LOCK };
    float cell_half = COG_SCREEN_CELL_HALF * dc->nh;
    for (int row = 0; row < COG_SCREEN_ROW_COUNT; row++) {
        int aim = row == COG_SLIDER_AIM_SENSITIVITY || row == COG_SLIDER_AIM_DEADZONE;
        int live = ui_cog_row_live(v, COG_TAB_SCREEN, row);
        float y = row_y(dc, COG_TAB_SCREEN, row);
        label(dc, ui_str(ROWS[row]), y, live);
        if (row >= COG_SCREEN_TRACK_ROWS) {
            screen_cells(dc, row, y, cell_half);
            continue;
        }
        if (aim && !live) {
            greyed_reason(dc, y, ui_str(UIS_NEEDS_HEAD_LOCK));
            track_hits(dc, COG_TAB_SCREEN, row, 0);
            continue;
        }
        track_line(dc, y, live ? 0x66FFFFFFu : 0x30FFFFFFu);
        chevrons(dc, y, live, cell_half);
        if (row == COG_SLIDER_TILT || row == COG_SLIDER_ROTATE) {
            tick(dc, (COG_RUN_L + COG_RUN_R) * 0.5f * dc->nw, y, cell_half, 1);   // level
        } else if (aim) {
            float t = row == COG_SLIDER_AIM_SENSITIVITY
                ? (float)(HEAD_AIM_SENSITIVITY_DEFAULT - HEAD_AIM_SENSITIVITY_MIN) / (HEAD_AIM_SENSITIVITY_MAX - HEAD_AIM_SENSITIVITY_MIN)
                : (float)(HEAD_AIM_DEADZONE_DEFAULT - HEAD_AIM_DEADZONE_MIN) / (HEAD_AIM_DEADZONE_MAX - HEAD_AIM_DEADZONE_MIN);
            tick(dc, cogRunU(t) * dc->nw, y, cell_half, 1);
        }
        if (live) thumb(dc, COG_TAB_SCREEN, row, y, settings_screen_slider(s, row));
        if (aim && live) {
            char buf[32];
            if (row == COG_SLIDER_AIM_SENSITIVITY) snprintf(buf, sizeof buf, "%d px/\xC2\xB0", s->head_aim_sensitivity);
            else snprintf(buf, sizeof buf, "%d\xC2\xB0/s", s->head_aim_deadzone);
            readout(dc, cogRowV(COG_TAB_SCREEN, row), buf);
        }
        track_hits(dc, COG_TAB_SCREEN, row, live);
    }
    reset_button(dc);
}

static void tab_room(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    int r = settings_room_index(v->room_env);
    static const UiStr ROWS[COG_ROOM_ROW_COUNT] = { UIS_ROW_BRIGHTNESS, UIS_ROW_GLOW, UIS_ROW_SCREEN_LIGHT,
                                                    UIS_ROW_LIGHT_LEVEL, UIS_ROW_SIZE };
    static const UiStr SW[2] = { UIS_OFF, UIS_ON };
    float cell_half = COG_CELL_HALF * dc->nh;
    int size_live = ui_cog_row_live(v, COG_FACE_ROOM, COG_ROOM_ROW_SIZE);
    for (int row = 0; row < COG_ROOM_ROW_COUNT; row++) {
        int live = row != COG_ROOM_ROW_SIZE || size_live;
        float y = row_y(dc, COG_FACE_ROOM, row);
        label(dc, ui_str(ROWS[row]), y, live);
        if (row == COG_ROOM_ROW_GLOW) {
            cells(dc, COG_FACE_ROOM, row, SW, 2, y, 1, cell_half, r >= 0 ? s->room[r].glow : -1);
            continue;
        }
        if (row == COG_ROOM_ROW_LIGHT) {
            cells(dc, COG_FACE_ROOM, row, SW, 2, y, 1, cell_half, s->screen_light);
            continue;
        }
        track_line(dc, y, live ? 0x66FFFFFFu : 0x30FFFFFFu);
        chevrons(dc, y, live, cell_half);
        if (row == COG_ROOM_ROW_LIGHT_LEVEL) {
            tick(dc, cogRunU(lanePlace(ROOM_LIGHT_DEFAULT, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX)) * dc->nw, y, cell_half, 1);
        }
        if (live && r >= 0) {
            float t = row == COG_ROOM_ROW_BRIGHTNESS ? lanePlace(s->room[r].brightness, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX)
                    : row == COG_ROOM_ROW_LIGHT_LEVEL ? lanePlace(s->room[r].light, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX)
                    : lanePlace(s->room[r].screen, ROOM_SCREEN_MIN, ROOM_SCREEN_MAX);
            thumb(dc, COG_FACE_ROOM, row, y, t);
            char buf[16];
            int pct = row == COG_ROOM_ROW_BRIGHTNESS ? lanePercent(s->room[r].brightness, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX)
                    : row == COG_ROOM_ROW_LIGHT_LEVEL ? lanePercent(s->room[r].light, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX)
                    : s->room[r].screen;
            snprintf(buf, sizeof buf, "%d%%", pct);
            readout(dc, cogRowV(COG_FACE_ROOM, row), buf);
        }
        track_hits(dc, COG_FACE_ROOM, row, live);
    }
    if (!size_live) {
        float y = (COG_ROW_V0 + (COG_ROOM_ROW_SIZE + 1) * COG_ROW_STEP) * dc->nh;
        dc_text(dc, UI_WEIGHT_REGULAR, 17.0f, dc->nw * 0.5f, y, UI_ALIGN_CENTER, 0x50FFFFFFu, ui_str(UIS_FIXED_SIZE));
    }
}

static void tab_display(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    static const UiStr ROWS[COG_OPTION_COUNT] = { UIS_ROW_SHARPEN, UIS_ROW_SUPERSAMPLE, UIS_ROW_STATS,
        UIS_ROW_HEAD_LOCKED, UIS_ROW_HEAD_AIM, UIS_ROW_MODE, UIS_ROW_SHOW_CONTROLLERS, UIS_ROW_RAY,
        UIS_ROW_POINTER_SLEEP, UIS_ROW_CLICK_STEADY, UIS_ROW_CLICK_SOUND, UIS_ROW_GLOW, UIS_ROW_SCREEN_LIGHT,
        UIS_ROW_NOTICES };
    static const UiStr OFFNQ[3] = { UIS_OFF, UIS_NORMAL, UIS_QUALITY };
    static const UiStr OFFON[2] = { UIS_OFF, UIS_ON };
    static const UiStr PADPTR[2] = { UIS_POINTER, UIS_GAMEPAD };
    // native: the Head locked row's third cell, the screen that follows the head late
    static const UiStr HEADLOCK3[3] = { UIS_OFF, UIS_ON, UIS_FOLLOW };
    // native: which notices the toast says
    static const UiStr NOTICES3[3] = { UIS_NOTICE_CELL_ALL, UIS_NOTICE_CELL_IMPORTANT, UIS_OFF };
    // native: how still the cursor is held around a click
    static const UiStr STEADY3[3] = { UIS_OFF, UIS_NORMAL, UIS_STEADY };
    // native: the ray row has a third cell, the dot without the ray
    static const UiStr POINTER3[3] = { UIS_OFF, UIS_NATIVE_DOT, UIS_NATIVE_RAY };
    // native: which controller to draw (a custom model has no cell: the file and the launcher pick it)
    static const UiStr MODEL4[4] = { UIS_OFF, UIS_MODEL_FRAME, UIS_MODEL_GENERIC, UIS_MODEL_GUN };
    float cell_half = COG_DISPLAY_CELL_HALF * dc->nh;
    int in_room = v->room_env >= 0 && settings_room_index(v->room_env) >= 0;
    for (int row = 0; row < COG_OPTION_COUNT; row++) {
        int live = ui_cog_row_live(v, COG_TAB_DISPLAY, row);
        float y = row_y(dc, COG_TAB_DISPLAY, row);
        uint32_t ink = live ? WHITE : DIM;
        if (row == COG_OPTION_GAMEPAD) {
            // the controllers' own shortcut under the label
            static const UiStr HINT[3] = { UIS_SHORTCUT_MENU_GRIP, UIS_SHORTCUT_STICKS, UIS_SHORTCUT_TRIGGERS_GRIPS };
            int sc = s->gamepad_toggle < 0 || s->gamepad_toggle > 2 ? 0 : s->gamepad_toggle;
            // native (lane wave6-i18n): under a label that reaches below its baseline (a translation's
            // p, j, р) the shortcut line moves down clear of it, into the room above the next row;
            // "Mode" does not, and both stay where they were. A long label shrinks to its room.
            float below = 0.0f;
            ui_text_ink_extent(dc->font, UI_WEIGHT_REGULAR, LABEL_SIZE * dc->k, ui_str(ROWS[row]), NULL, &below);
            float drop = below / dc->k > 2.5f ? below / dc->k - 2.5f : 0.0f;
            float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, LABEL_SIZE * dc->k, LABEL_MIN_SIZE * dc->k, ui_str(ROWS[row]),
                                         (label_right(dc) - LABEL_X * dc->nw) * dc->k) / dc->k;
            dc_text(dc, UI_WEIGHT_REGULAR, fit, LABEL_X * dc->nw, y - 2.0f, UI_ALIGN_LEFT, ink, ui_str(ROWS[row]));
            dc_text(dc, UI_WEIGHT_REGULAR, 15.0f, LABEL_X * dc->nw, y - 2.0f + 15.0f + drop, UI_ALIGN_LEFT, 0x99FFFFFFu, ui_str(HINT[sc]));
        } else {
            label(dc, ui_str(ROWS[row]), y, live);
        }
        if (!live) {
            int later = (v->display_later & (1u << row)) != 0;
            greyed_reason(dc, y, later ? (v->later_reason ? v->later_reason : ui_str(UIS_NATIVE_LATER))
                                       : ui_str(in_room ? UIS_NOT_IN_ROOM : UIS_NEEDS_HEAD_LOCK));
            // one disabled hit per cell so every row is still reported
            for (int c = 0; c < 2; c++) {
                float rv = cogRowV(COG_TAB_DISPLAY, row), half = cogRowHalf(COG_TAB_DISPLAY);
                dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / 2, rv - half,
                          COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / 2, rv + half, 0);
            }
            continue;
        }
        const UiStr* names = row <= COG_OPTION_SUPERSAMPLE ? OFFNQ : row == COG_OPTION_GAMEPAD ? PADPTR
                           : row == COG_OPTION_RAY ? POINTER3 : row == COG_OPTION_NOTICES ? NOTICES3
                           : row == COG_OPTION_HEAD_LOCK ? HEADLOCK3
                           : row == COG_OPTION_CONTROLLERS ? MODEL4
                           : row == COG_OPTION_CLICK_STEADY ? STEADY3 : OFFON;
        int n = ui_cog_row_cells(COG_TAB_DISPLAY, row);
        cells(dc, COG_TAB_DISPLAY, row, names, n, y, 1, cell_half, settings_option_cell(s, &v->session, row));
    }
    // the glow level: the one track on this tab
    float y = row_y(dc, COG_TAB_DISPLAY, COG_DISPLAY_SLIDER_ROW);
    if (v->display_later & (1u << COG_DISPLAY_SLIDER_ROW)) {
        // native: the glow is not drawn yet, so its level is dead too
        label(dc, ui_str(UIS_ROW_GLOW_LEVEL), y, 0);
        greyed_reason(dc, y, v->later_reason ? v->later_reason : ui_str(UIS_NATIVE_LATER));
        track_hits(dc, COG_TAB_DISPLAY, COG_DISPLAY_SLIDER_ROW, 0);
        return;
    }
    label(dc, ui_str(UIS_ROW_GLOW_LEVEL), y, 1);
    track_line(dc, y, 0x66FFFFFFu);
    chevrons(dc, y, 1, cell_half);
    tick(dc, (COG_RUN_L + COG_RUN_R) * 0.5f * dc->nw, y, cell_half, 1);
    thumb(dc, COG_TAB_DISPLAY, COG_DISPLAY_SLIDER_ROW, y, (float)s->glow_level / 100.0f);
    track_hits(dc, COG_TAB_DISPLAY, COG_DISPLAY_SLIDER_ROW, 1);
}

// Native (PLAN 4.3): a row with nothing to set while the PC draws the pair,
// greyed with the reason across it and its hits disabled
static void host_drawn_row(Dc* dc, int row, int n, int track) {
    float y = row_y(dc, COG_TAB_3D, row);
    greyed_reason(dc, y, ui_str(UIS_SBS_NO_DEPTH));
    if (track) {
        track_hits(dc, COG_TAB_3D, row, 0);
        return;
    }
    float rv = cogRowV(COG_TAB_3D, row), half = cogRowHalf(COG_TAB_3D);
    for (int c = 0; c < n; c++) {
        dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / (float)n, rv - half,
                  COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / (float)n, rv + half, 0);
    }
}

// Native (lane wave3-tidy): the depth model's five tracks under the source
// row. Each is the percent its [3d] key is stored in, a tick at the value the
// model makes without it, its value beside it; with the PC drawing the pair
// they are dimmed and carry no thumb, since there is no depth to set.
static void depth_tracks(Dc* dc, int live, float tick_half) {
    static const struct {
        int row;
        UiStr name;
        int min, max, def;
    } ROWS[] = {
        { COG_ROW3D_EDGE, UIS_ROW_EDGE, EDGE_SHARPNESS_MIN, EDGE_SHARPNESS_MAX, EDGE_SHARPNESS_DEFAULT },
        { COG_ROW3D_DETAIL, UIS_ROW_DEPTH_DETAIL, DEPTH_SCALE_MIN, DEPTH_SCALE_MAX, DEPTH_SCALE_DEFAULT },
        { COG_ROW3D_SHAPE, UIS_ROW_DEPTH_SHAPE, DEPTH_SCALE_MIN, DEPTH_SCALE_MAX, DEPTH_SCALE_DEFAULT },
        { COG_ROW3D_MOTION, UIS_ROW_MOTION_RESET, MOTION_RESET_MIN, MOTION_RESET_MAX, MOTION_RESET_DEFAULT },
        { COG_ROW3D_BALANCE, UIS_ROW_EYE_BALANCE, EYE_BALANCE_MIN, EYE_BALANCE_MAX, EYE_BALANCE_DEFAULT },
    };
    const Settings* s = dc->v->settings;
    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        int row = ROWS[i].row;
        int value = row == COG_ROW3D_EDGE ? s->edge_sharpness
                  : row == COG_ROW3D_DETAIL ? s->depth_scale
                  : row == COG_ROW3D_SHAPE ? s->depth_global
                  : row == COG_ROW3D_MOTION ? s->motion_reset : s->eye_balance;
        float y = row_y(dc, COG_TAB_3D, row);
        label(dc, ui_str(ROWS[i].name), y, live);
        track_line(dc, y, live ? 0x66FFFFFFu : 0x30FFFFFFu);
        if (ROWS[i].def != ROWS[i].min) {
            tick(dc, cogRunU(lanePlace(ROWS[i].def, ROWS[i].min, ROWS[i].max)) * dc->nw, y, tick_half, live);
        }
        chevrons(dc, y, live, tick_half);
        if (live) {
            char buf[16];
            if (row == COG_ROW3D_BALANCE && value != 0) snprintf(buf, sizeof buf, "%+d%%", value);
            else snprintf(buf, sizeof buf, "%d%%", value);
            thumb(dc, COG_TAB_3D, row, y, lanePlace(value, ROWS[i].min, ROWS[i].max));
            readout(dc, cogRowV(COG_TAB_3D, row), buf);
        }
        track_hits(dc, COG_TAB_3D, row, live);
    }
}

static void tab_3d(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    int ok = v->stereo_ok;
    int src = v->stereo_source >= 0 && v->stereo_source < STEREO_SOURCE_COUNT ? v->stereo_source : STEREO_SOURCE_DEPTH;
    // native (PLAN 4.3): the PC draws the pair, so only convergence and the switch act
    int host_drawn = ok && src != STEREO_SOURCE_DEPTH;
    static const UiStr PRESETS[COG_PRESET_CELLS] = { UIS_COMFORT, UIS_BALANCED, UIS_STRONG };
    static const UiStr SW[2] = { UIS_OFF, UIS_ON };
    static const UiStr SOURCES[COG_SOURCE_CELLS] = { UIS_SOURCE_DEPTH, UIS_SOURCE_SBS, UIS_SOURCE_TB };
    float tick_half = COG_3D_CELL_HALF * dc->nh;
    int presets[COG_PRESET_CELLS];
    ui_presets_for(s, presets);
    int sep = settings_separation_units(s);

    float y = row_y(dc, COG_TAB_3D, COG_ROW3D_PRESET);
    label(dc, ui_str(UIS_ROW_PRESET), y, ok && !host_drawn);
    if (host_drawn) {
        host_drawn_row(dc, COG_ROW3D_PRESET, COG_PRESET_CELLS, 0);
    } else {
        cells(dc, COG_TAB_3D, COG_ROW3D_PRESET, PRESETS, COG_PRESET_CELLS, y, ok, tick_half, ok ? cogPresetAt(sep, presets) : -1);
    }

    int def_sep = settings_model_default_separation(s);
    for (int i = 0; i < 2; i++) {
        int row = COG_ROW3D_SEPARATION + i;
        y = row_y(dc, COG_TAB_3D, row);
        label(dc, ui_str(i == 0 ? UIS_ROW_DEPTH : UIS_ROW_CONVERGENCE), y, ok && (i == 1 || !host_drawn));
        if (i == 0 && host_drawn) {
            host_drawn_row(dc, row, 0, 1);
            continue;
        }
        float mark_t = row == COG_ROW3D_SEPARATION ? (float)def_sep / COG_SEP_STEPS : 0.5f;
        float mark_x = (COG_RUN_L + mark_t * (COG_RUN_R - COG_RUN_L)) * dc->nw;
        if (row == COG_ROW3D_SEPARATION) {
            // past the model's default depth stops growing and only the strain does
            dc_line(dc, COG_RUN_L * dc->nw, y, mark_x, y, 6.0f, ok ? 0x66FFFFFFu : 0x30FFFFFFu);
            dc_line(dc, mark_x, y, COG_RUN_R * dc->nw, y, 6.0f, ok ? 0x66FFB74Du : 0x30FFB74Du);
            dc_text(dc, UI_WEIGHT_REGULAR, 15.0f, (mark_x + COG_RUN_R * dc->nw) * 0.5f, y + 0.04f * dc->nh,
                    UI_ALIGN_CENTER, ok ? 0xA0FFB74Du : 0x30FFB74Du, ui_str(UIS_HARDER_ON_EYES));
        } else {
            track_line(dc, y, ok ? 0x66FFFFFFu : 0x30FFFFFFu);
        }
        tick(dc, mark_x, y, tick_half, ok);
        chevrons(dc, y, ok, tick_half);
        if (ok) {
            float t = row == COG_ROW3D_SEPARATION ? lanePlace(sep, 0, COG_SEP_STEPS)
                                                  : (float)settings_convergence_percent(s) / 100.0f;
            thumb(dc, COG_TAB_3D, row, y, t);
        }
        track_hits(dc, COG_TAB_3D, row, ok);
    }
    y = row_y(dc, COG_TAB_3D, COG_ROW3D_SWITCH);
    label(dc, ui_str(UIS_TAB_3D), y, ok);
    cells(dc, COG_TAB_3D, COG_ROW3D_SWITCH, SW, 2, y, ok, tick_half, ok ? (v->session.stereo_on ? 1 : 0) : -1);
    // native (PLAN 4.3): where the 3D comes from, live whatever else is
    y = row_y(dc, COG_TAB_3D, COG_ROW3D_SOURCE);
    label(dc, ui_str(UIS_ROW_3D_SOURCE), y, 1);
    cells(dc, COG_TAB_3D, COG_ROW3D_SOURCE, SOURCES, COG_SOURCE_CELLS, y, 1, tick_half, src);
    depth_tracks(dc, ok && !host_drawn, tick_half);
    reset_button(dc);
    if (!ok) {
        dc_text(dc, UI_WEIGHT_REGULAR, 17.0f, dc->nw * 0.5f, cogRowV(COG_TAB_3D, COG_ROW3D_COUNT) * dc->nh,
                UI_ALIGN_CENTER, 0x50FFFFFFu, v->stereo_reason ? v->stereo_reason : ui_str(UIS_THREE_D_OFF_IN_SETTINGS));
    }
}

static void picture_text(int row, int units, char* out, size_t cap) {
    switch (row) {
    case PICTURE_CONTRAST:
    case PICTURE_SATURATION: snprintf(out, cap, "%d%%", units); break;
    case PICTURE_GAMMA: snprintf(out, cap, "%.2f", units / 100.0); break;
    default: snprintf(out, cap, units > 0 ? "+%d" : "%d", units); break;
    }
}

static void tab_picture(Dc* dc) {
    const Settings* s = dc->v->settings;
    // The four grade values, then Background (percent dimmed) and Glow edge
    // (off, soft, wide on a three place track)
    static const UiStr ROWS[PICTURE_TAB_ROWS] = { UIS_ROW_BRIGHTNESS, UIS_ROW_CONTRAST, UIS_ROW_GAMMA, UIS_ROW_SATURATION,
                                                  UIS_ROW_BACKGROUND, UIS_ROW_GLOW_EDGE };
    static const int MIN[PICTURE_TAB_ROWS] = { PICTURE_BRIGHTNESS_MIN, PICTURE_CONTRAST_MIN, PICTURE_GAMMA_MIN, PICTURE_SATURATION_MIN,
                                               BG_DIM_MIN, GLOW_FEATHER_OFF };
    static const int MAX[PICTURE_TAB_ROWS] = { PICTURE_BRIGHTNESS_MAX, PICTURE_CONTRAST_MAX, PICTURE_GAMMA_MAX, PICTURE_SATURATION_MAX,
                                               BG_DIM_MAX, GLOW_FEATHER_COUNT - 1 };
    static const int DEF[PICTURE_TAB_ROWS] = { PICTURE_BRIGHTNESS_DEFAULT, PICTURE_CONTRAST_DEFAULT, PICTURE_GAMMA_DEFAULT, PICTURE_SATURATION_DEFAULT,
                                               BG_DIM_DEFAULT, GLOW_FEATHER_OFF };
    static const UiStr FEATHER[GLOW_FEATHER_COUNT] = { UIS_OFF, UIS_FEATHER_SOFT, UIS_FEATHER_WIDE };
    float tick_half = COG_CELL_HALF * dc->nh;
    for (int row = 0; row < PICTURE_TAB_ROWS; row++) {
        float y = row_y(dc, COG_TAB_PICTURE, row);
        label(dc, ui_str(ROWS[row]), y, 1);
        track_line(dc, y, 0x66FFFFFFu);
        chevrons(dc, y, 1, tick_half);
        tick(dc, cogRunU(lanePlace(DEF[row], MIN[row], MAX[row])) * dc->nw, y, tick_half, 1);
        int units = row == PICTURE_ROW_BACKGROUND ? s->bg_dim
                  : row == PICTURE_ROW_GLOW_EDGE ? s->glow_feather
                  : settings_picture_value(s, row);
        thumb(dc, COG_TAB_PICTURE, row, y, lanePlace(units, MIN[row], MAX[row]));
        char buf[24];
        if (row == PICTURE_ROW_BACKGROUND) {
            snprintf(buf, sizeof buf, "%d%%", units);
        } else if (row == PICTURE_ROW_GLOW_EDGE) {
            snprintf(buf, sizeof buf, "%s", ui_str(FEATHER[units < 0 ? 0 : (units >= GLOW_FEATHER_COUNT ? GLOW_FEATHER_COUNT - 1 : units)]));
        } else {
            picture_text(row, units, buf, sizeof buf);
        }
        readout(dc, cogRowV(COG_TAB_PICTURE, row), buf);
        track_hits(dc, COG_TAB_PICTURE, row, 1);
    }
    reset_button(dc);
}

static void about_button(Dc* dc, float l, float t, float r, float b, const char* text, int btn) {
    float x0 = l * dc->nw, y0 = t * dc->nh, x1 = r * dc->nw, y1 = b * dc->nh;
    if (dc_hover(dc, UI_HIT_BUTTON, btn, -1)) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x28FFFFFFu);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 14.0f, 4.0f, 0xEEFFFFFFu);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 24.0f, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (x1 - x0) - 24.0f, WHITE, text);
    dc_hit_uv(dc, UI_HIT_BUTTON, btn, 0, l, t, r, b, 1);
}

static void tab_about(Dc* dc) {
    const UiView* v = dc->v;
    float mid = dc->nw * 0.5f, room = dc->nw - 80.0f;
    dc_text(dc, UI_WEIGHT_REGULAR, 30.0f, mid, 0.25f * dc->nh, UI_ALIGN_CENTER, WHITE, "Moonlight XR for Frame");
    char line[256], path[200];
    const char* ver = v->version && *v->version ? v->version : "dev";
    if (v->commit && *v->commit) snprintf(line, sizeof line, ui_str(UIS_VERSION), ver, v->commit);
    else snprintf(line, sizeof line, ui_str(UIS_VERSION_NO_COMMIT), ver);
    line_fit_center(dc, 19.0f, mid, 0.32f * dc->nh, room, 0xB0FFFFFFu, line, UI_ELLIPSIZE_END);
    if (v->log_path) {
        ui_basename_short(v->log_path, path, sizeof path);
        snprintf(line, sizeof line, ui_str(UIS_LOG_FILE), path);
    } else {
        snprintf(line, sizeof line, "%s", ui_str(UIS_LOG_FILE_OFF));
    }
    line_fit_center(dc, 19.0f, mid, 0.38f * dc->nh, room, 0xB0FFFFFFu, line, UI_ELLIPSIZE_MIDDLE);

    about_button(dc, COG_KOFI_L, COG_KOFI_T, COG_KOFI_R, COG_KOFI_B, ui_str(UIS_KOFI_TITLE), UI_BTN_KOFI);
    line_fit_center(dc, 17.0f, mid, (COG_KOFI_B + 0.05f) * dc->nh, room, 0x80FFFFFFu, ui_str(UIS_KOFI_FREE), UI_ELLIPSIZE_END);
    about_button(dc, COG_REPORT_L, COG_REPORT_T, COG_REPORT_R, COG_REPORT_B, ui_str(UIS_REPORT_TITLE), UI_BTN_REPORT);
    line_fit_center(dc, 17.0f, mid, (COG_REPORT_B + 0.05f) * dc->nh, room, 0x80FFFFFFu, ui_str(UIS_REPORT_SENDS), UI_ELLIPSIZE_END);
    line_fit_center(dc, 17.0f, mid, 0.915f * dc->nh, room, 0x90FFFFFFu, ui_str(UIS_ABOUT_BUILT_BY), UI_ELLIPSIZE_END);
    line_fit_center(dc, 15.0f, mid, 0.96f * dc->nh, room, 0x70FFFFFFu, ui_str(UIS_ABOUT_BASED_ON), UI_ELLIPSIZE_END);
}

void panel_cog(Dc* dc) {
    int face = ui_cog_face(dc->v);
    int in_room = face == COG_FACE_ROOM;
    int tab = dc->v->cog_tab < 0 || dc->v->cog_tab >= COG_TAB_COUNT ? COG_TAB_SCREEN : dc->v->cog_tab;
    // in a room the Screen tab's slot is the Room tab, and every other tab is as ever
    int room_up = dc->v->room_env >= 0 && settings_room_index(dc->v->room_env) >= 0;
    dc->label_text = NULL;
    chrome(dc, tab, room_up);
    switch (face) {
    case COG_TAB_SCREEN: tab_screen(dc); break;
    case COG_FACE_ROOM: tab_room(dc); break;
    case COG_TAB_DISPLAY: tab_display(dc); break;
    case COG_TAB_3D: tab_3d(dc); break;
    case COG_TAB_PICTURE: tab_picture(dc); break;
    case COG_TAB_ABOUT: tab_about(dc); break;
    default: break;
    }
    label_flush(dc, label_right(dc));
    (void)in_room;
}
