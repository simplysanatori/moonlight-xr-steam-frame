// The settings panel: seven tabs (Screen, Room, Display, 3D, Controls, Accessibility, About).
// XrPanels.buildCogSheet and its drawCog* helpers, with the things upstream
// puts on separate quads (thumbs, the marks on the cells in force, the values
// beside the tracks) drawn into the same buffer from the settings, so one
// upload is the whole panel. The rows, where they sit and whether they are
// live are xr_layout's table (cogRowOf, cogRowV, cogRowWhy). GPLv3.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "panels_int.h"

#define LABEL_X 0.06f
#define LABEL_SIZE 22.0f
#define WHITE 0xFFFFFFFFu
#define DIM 0x30FFFFFFu

static float row_y(Dc* dc, int face, int row) { return cogRowV(face, row) * dc->nh; }
static float cell_half_of(Dc* dc, int tab) { return cogCellHalf(tab) * dc->nh; }

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
        // a long name (Bring here, in the Screen tab's four cells) shrinks to fit, clear of the ring, rather than
        // being cut
        float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 19.0f * dc->k, 12.0f * dc->k, ui_str(names[i]),
                                     ((x1 - x0) - 11.0f) * dc->k) / dc->k;
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

static void readout(Dc* dc, float row_v, const char* text) {
    float right = COG_READOUT_L * dc->nw + COG_READOUT_TEX_W - 8.0f;
    // the row's label stops short of the readout
    label_flush(dc, right - dc_text_width(dc, UI_WEIGHT_REGULAR, 20.0f, text) - 10.0f);
    UiFontMetrics m;
    ui_font_metrics(dc->font, UI_WEIGHT_REGULAR, 20.0f * dc->k, &m);
    dc_text(dc, UI_WEIGHT_REGULAR, 20.0f, right, row_v * dc->nh + m.cap_height * 0.5f / dc->k, UI_ALIGN_RIGHT,
            0xB0FFFFFFu, text);
}

static void reset_button(Dc* dc, int live) {
    float x0 = COG_RESET_L * dc->nw, y0 = COG_RESET_T * dc->nh, x1 = COG_RESET_R * dc->nw, y1 = COG_RESET_B * dc->nh;
    int hot = live && dc_hover(dc, UI_HIT_BUTTON, UI_BTN_RESET, -1);
    if (hot) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x28FFFFFFu);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 14.0f, 4.0f, live ? 0xEEFFFFFFu : DIM);
    dc_text_center(dc, UI_WEIGHT_REGULAR, LABEL_SIZE, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, live ? WHITE : DIM, ui_str(UIS_RESET));
    dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_RESET, 0, COG_RESET_L, COG_RESET_T, COG_RESET_R, COG_RESET_B, live);
}

// A row's hits, all disabled: a greyed row's, which the ray passes over
static void dead_hits(Dc* dc, int tab, int row) {
    if (cogRowIsTrack(tab, row)) {
        track_hits(dc, tab, row, 0);
        return;
    }
    int n = cogRowCells(tab, row);
    float rv = cogRowV(tab, row), half = cogRowHalf(tab);
    for (int c = 0; c < n; c++) {
        dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / (float)n, rv - half,
                  COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / (float)n, rv + half, 0);
    }
}

// Why a row of these tabs is greyed, across where its track or cells would be
static void dead_row(Dc* dc, int tab, int row, const char* name, int why) {
    float y = row_y(dc, tab, row);
    UiStr reason = why == COG_DEAD_HEAD_LOCK ? UIS_NEEDS_HEAD_LOCK : why == COG_DEAD_NOT_IN_ROOM ? UIS_NOT_IN_ROOM
                 : why == COG_DEAD_ONLY_IN_ROOM ? UIS_ONLY_IN_ROOM : why == COG_DEAD_BY_GUN ? UIS_SET_BY_GUN
                 : UIS_SET_BY_ROOM;
    label(dc, name, y, 0);
    greyed_reason(dc, y, ui_str(reason));
    dead_hits(dc, tab, row);
}

// A live track row: its line, buttons, a tick at tick_t (under 0 none), the
// thumb at t and what is written beside it (NULL nothing)
static void track_row(Dc* dc, int tab, int row, const char* name, float tick_t, float t, const char* value) {
    float y = row_y(dc, tab, row), half = cell_half_of(dc, tab);
    label(dc, name, y, 1);
    track_line(dc, y, 0x66FFFFFFu);
    chevrons(dc, y, 1, half);
    if (tick_t >= 0.0f) tick(dc, cogRunU(tick_t) * dc->nw, y, half, 1);
    thumb(dc, tab, row, y, t);
    if (value) readout(dc, cogRowV(tab, row), value);
    track_hits(dc, tab, row, 1);
}

// A line centred on x at a baseline, cut with an ellipsis to fit room (native px)
static void line_fit_center(Dc* dc, float size, float x, float baseline, float room, uint32_t c,
                            const char* text, UiEllipsize mode) {
    char cut[320];
    ui_text_ellipsize(dc->font, UI_WEIGHT_REGULAR, size * dc->k, text, room * dc->k, mode, cut, sizeof cut, NULL);
    dc_text(dc, UI_WEIGHT_REGULAR, size, x, baseline, UI_ALIGN_CENTER, c, cut);
}

// ---- chrome --------------------------------------------------------------------------------

static void chrome(Dc* dc, int tab) {
    dc_sheet(dc, 32.0f, 0xF0141416u);
    static const UiStr TABS[COG_TAB_COUNT] = { UIS_TAB_SCREEN, UIS_TAB_ROOM, UIS_TAB_DISPLAY, UIS_TAB_3D,
                                               UIS_TAB_CONTROLS, UIS_TAB_ACCESS, UIS_TAB_ABOUT };
    float bar_b = COG_TAB_BAR_B * dc->nh;
    for (int i = 0; i < COG_TAB_COUNT; i++) {
        int current = i == tab;
        // native (lane a11y-tab): the slots are of their own widths (xr_layout.h cogTabLeft)
        float x0 = cogTabLeft(i) * dc->nw + 4.0f, x1 = cogTabRight(i) * dc->nw - 4.0f, y0 = 12.0f, y1 = bar_b - 8.0f;
        if (current) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x28FFFFFFu);
        else if (dc_hover(dc, UI_HIT_TAB, i, -1)) dc_fill_rrect(dc, x0, y0, x1, y1, 14.0f, 0x14FFFFFFu);
        const char* name = ui_str(TABS[i]);
        // a name longer than its slot (a translation's) shrinks to fit
        float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 25.0f * dc->k, 15.0f * dc->k, name,
                                     ((x1 - x0) - 8.0f) * dc->k) / dc->k;
        dc_text_center(dc, UI_WEIGHT_REGULAR, fit, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, current ? WHITE : 0x60FFFFFFu, name);
        if (current) {
            // the underline is what carries at a glance, the fill alone is too subtle
            dc_fill_rrect(dc, x0 + 12.0f, y1 - 4.0f, x1 - 12.0f, y1, 0.0f, 0xEEFFFFFFu);
        }
        dc_hit_uv(dc, UI_HIT_TAB, i, 0, cogTabLeft(i), 0.0f, cogTabRight(i), COG_TAB_BAR_B, 1);
    }
    dc_fill_rrect(dc, 20.0f, bar_b, dc->nw - 20.0f, bar_b + 2.0f, 0.0f, 0x30FFFFFFu);
}

// ---- tabs -------------------------------------------------------------------------------------

// The options' names and their cells', in COG_OPTION_ order. Off, On first; a
// native row with other cells says so in xr_shared.h beside its cells' count.
static const UiStr OPT_OFF_ON[] = { UIS_OFF, UIS_ON };
static const UiStr OPT_ON_OFF[] = { UIS_ON, UIS_OFF };
static const UiStr OPT_OFF_NORMAL_QUALITY[] = { UIS_OFF, UIS_NORMAL, UIS_QUALITY };
static const UiStr OPT_STATS[] = { UIS_OFF, UIS_STATS_FULL, UIS_STATS_COMPACT, UIS_STATS_MINIMAL };
static const UiStr OPT_HEAD_LOCK[] = { UIS_OFF, UIS_ON, UIS_FOLLOW };
static const UiStr OPT_MODE[] = { UIS_POINTER, UIS_GAMEPAD };
static const UiStr OPT_MODEL[] = { UIS_OFF, UIS_MODEL_FRAME, UIS_MODEL_GUN };
static const UiStr OPT_RAY[] = { UIS_OFF, UIS_NATIVE_DOT, UIS_NATIVE_RAY };
static const UiStr OPT_STEADY[] = { UIS_OFF, UIS_NORMAL, UIS_STEADY };
static const UiStr OPT_NOTICES[] = { UIS_NOTICE_CELL_ALL, UIS_NOTICE_CELL_IMPORTANT, UIS_OFF };
static const UiStr OPT_MOUSE_MODE[] = { UIS_MOUSE_ABSOLUTE, UIS_MOUSE_RELATIVE };
static const struct {
    UiStr name;
    const UiStr* cells;
} OPTIONS[COG_OPTION_COUNT] = {
    [COG_OPTION_SHARPEN] = { UIS_ROW_SHARPEN, OPT_OFF_NORMAL_QUALITY },
    [COG_OPTION_SUPERSAMPLE] = { UIS_ROW_SUPERSAMPLE, OPT_OFF_NORMAL_QUALITY },
    [COG_OPTION_STATS] = { UIS_ROW_STATS, OPT_STATS },
    [COG_OPTION_HEAD_LOCK] = { UIS_ROW_HEAD_LOCKED, OPT_HEAD_LOCK },
    [COG_OPTION_HEAD_AIM] = { UIS_ROW_HEAD_AIM, OPT_OFF_ON },
    [COG_OPTION_GAMEPAD] = { UIS_ROW_MODE, OPT_MODE },
    [COG_OPTION_CONTROLLERS] = { UIS_ROW_SHOW_CONTROLLERS, OPT_MODEL },
    [COG_OPTION_RAY] = { UIS_ROW_RAY, OPT_RAY },
    [COG_OPTION_POINTER_SLEEP] = { UIS_ROW_POINTER_SLEEP, OPT_OFF_ON },
    [COG_OPTION_CLICK_STEADY] = { UIS_ROW_CLICK_STEADY, OPT_STEADY },
    [COG_OPTION_CLICK_SOUND] = { UIS_ROW_CLICK_SOUND, OPT_OFF_ON },
    [COG_OPTION_AMBILIGHT] = { UIS_ROW_GLOW, OPT_OFF_ON },
    [COG_OPTION_ROOM_LIGHT] = { UIS_ROW_SCREEN_LIGHT, OPT_OFF_ON },
    [COG_OPTION_NOTICES] = { UIS_ROW_NOTICES, OPT_NOTICES },
    [COG_OPTION_FOVEATED] = { UIS_ROW_FOVEATED, OPT_ON_OFF },
    [COG_OPTION_MOUSE_MODE] = { UIS_ROW_MOUSE_MODE, OPT_MOUSE_MODE },
};

static const char* row_name(int tab, int row) {
    static const UiStr SCREEN[COG_SCREEN_ROW_COUNT] = { UIS_ROW_DISTANCE, UIS_ROW_HEIGHT, UIS_ROW_TILT, UIS_ROW_ROTATE,
        UIS_ROW_CURVE, UIS_ROW_SIZE, UIS_ROW_PRESET, UIS_ROW_HEAD_LOCKED, UIS_ROW_PER_APP, UIS_ROW_ANGULAR_LOCK };
    static const UiStr ROOM[COG_ROOM_ROW_COUNT] = { UIS_ROW_BACKGROUND, UIS_ROW_GLOW, UIS_ROW_GLOW_LEVEL,
        UIS_ROW_GLOW_SPREAD, UIS_ROW_ROOM_BRIGHTNESS, UIS_ROW_SCREEN_LIGHT, UIS_ROW_LIGHT_LEVEL };
    static const UiStr PICTURE[PICTURE_VALUES] = { UIS_ROW_BRIGHTNESS, UIS_ROW_CONTRAST, UIS_ROW_GAMMA, UIS_ROW_SATURATION };
    static const UiStr CONTROLS[COG_CONTROLS_ROW_COUNT] = {
        [COG_CONTROLS_ROW_SCROLL] = UIS_ROW_SCROLL_SPEED,
        [COG_CONTROLS_ROW_AIM_SENSITIVITY] = UIS_ROW_AIM_SENSITIVITY,
        [COG_CONTROLS_ROW_AIM_DEADZONE] = UIS_ROW_AIM_DEAD_ZONE,
    };
    int option = cogRowOption(tab, row);
    return ui_str(option >= 0 ? OPTIONS[option].name : tab == COG_TAB_SCREEN ? SCREEN[row] : tab == COG_TAB_ROOM ? ROOM[row]
                  : tab == COG_TAB_DISPLAY ? PICTURE[row] : CONTROLS[row]);
}

// A live option row of any tab: its name, its cells, the one in force ringed
static void option_row(Dc* dc, int tab, int row) {
    int option = cogRowOption(tab, row);
    float y = row_y(dc, tab, row);
    label(dc, ui_str(OPTIONS[option].name), y, 1);
    cells(dc, tab, row, OPTIONS[option].cells, cogRowCells(tab, row), y, 1, cell_half_of(dc, tab),
          ui_cog_option_cell(dc->v, option));
}

static int row_why(const UiView* v, int tab, int row) {
    CogState state = ui_cog_state(v);
    return cogRowWhy(&state, tab, row);
}

static int reset_live(const UiView* v, int tab) {
    CogState state = ui_cog_state(v);
    return cogResetLive(&state, tab);
}

// The rows of cells that are not options: the presets with Bring here, the per
// app switch with Forget, and the angular-size lock. Bring here and Forget are
// presses rather than states, so neither is ever ringed.
static void screen_cells(Dc* dc, int row) {
    const Settings* s = dc->v->settings;
    static const UiStr PRESET[COG_SCREEN_PRESET_CELLS] = { UIS_PRESET_DESK, UIS_PRESET_COUCH, UIS_PRESET_CINEMA,
                                                           UIS_BRING_HERE };
    static const UiStr PER_APP[COG_SCREEN_PER_APP_CELLS] = { UIS_OFF, UIS_ON, UIS_FORGET };
    static const UiStr LOCK[COG_SCREEN_LOCK_CELLS] = { UIS_OFF, UIS_ON };
    const UiStr* names = row == COG_SCREEN_ROW_PRESET ? PRESET : row == COG_SCREEN_ROW_PER_APP ? PER_APP : LOCK;
    int mark = row == COG_SCREEN_ROW_PRESET ? layoutPresetAt(s->screen_distance_cm, s->screen_width_cm)
             : row == COG_SCREEN_ROW_PER_APP ? (s->per_app ? 1 : 0) : (s->angular_lock ? 1 : 0);
    float y = row_y(dc, COG_TAB_SCREEN, row);
    label(dc, row_name(COG_TAB_SCREEN, row), y, 1);
    cells(dc, COG_TAB_SCREEN, row, names, cogRowCells(COG_TAB_SCREEN, row), y, 1, cell_half_of(dc, COG_TAB_SCREEN), mark);
}

static void tab_screen(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    int r = settings_room_index(v->room_env);
    for (int row = 0; row < COG_SCREEN_ROW_COUNT; row++) {
        const char* name = row_name(COG_TAB_SCREEN, row);
        int why = row_why(v, COG_TAB_SCREEN, row);
        if (why == COG_DEAD_NO_CURVE) {
            // nothing to say of it: the row alone, dimmed
            float y = row_y(dc, COG_TAB_SCREEN, row);
            label(dc, name, y, 0);
            track_line(dc, y, 0x30FFFFFFu);
            chevrons(dc, y, 0, cell_half_of(dc, COG_TAB_SCREEN));
            dead_hits(dc, COG_TAB_SCREEN, row);
        } else if (why != COG_LIVE) {
            dead_row(dc, COG_TAB_SCREEN, row, name, why);
        } else if (cogRowOption(COG_TAB_SCREEN, row) >= 0) {
            option_row(dc, COG_TAB_SCREEN, row);
        } else if (!cogRowIsTrack(COG_TAB_SCREEN, row)) {
            screen_cells(dc, row);
        } else if (row == COG_SLIDER_SIZE && r >= 0) {
            // in a room, the share of the room's screen the picture takes
            char buf[16];
            snprintf(buf, sizeof buf, "%d%%", s->room[r].screen);
            track_row(dc, COG_TAB_SCREEN, row, name, -1.0f, lanePlace(s->room[r].screen, ROOM_SCREEN_MIN, ROOM_SCREEN_MAX), buf);
        } else {
            int level = row == COG_SLIDER_TILT || row == COG_SLIDER_ROTATE;
            track_row(dc, COG_TAB_SCREEN, row, name, level ? 0.5f : -1.0f, settings_screen_slider(s, row), NULL);
        }
    }
    reset_button(dc, reset_live(v, COG_TAB_SCREEN));
}

static void tab_room(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    int r = settings_room_index(v->room_env);
    for (int row = 0; row < COG_ROOM_ROW_COUNT; row++) {
        const char* name = row_name(COG_TAB_ROOM, row);
        int why = row_why(v, COG_TAB_ROOM, row);
        if (why != COG_LIVE) {
            dead_row(dc, COG_TAB_ROOM, row, name, why);
            continue;
        }
        if (cogRowOption(COG_TAB_ROOM, row) >= 0) {
            option_row(dc, COG_TAB_ROOM, row);
            continue;
        }
        // the percent shown: of the lane for a room's own, the value for the rest
        float t, mark;
        int pct;
        if (row == COG_ROOM_ROW_BRIGHTNESS) {
            t = lanePlace(s->room[r].brightness, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
            pct = lanePercent(s->room[r].brightness, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
            mark = -1.0f;
        } else if (row == COG_ROOM_ROW_LIGHT_LEVEL) {
            t = lanePlace(s->room[r].light, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
            pct = lanePercent(s->room[r].light, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
            mark = lanePlace(ROOM_LIGHT_DEFAULT, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
        } else if (row == COG_ROOM_ROW_GLOW_LEVEL) {
            t = (float)s->glow_level / 100.0f;
            pct = s->glow_level;
            mark = (float)GLOW_LEVEL_DEFAULT / 100.0f;
        } else if (row == COG_ROOM_ROW_GLOW_SPREAD) {   // native (issue #7)
            t = lanePlace(s->glow_spread, GLOW_SPREAD_MIN, GLOW_SPREAD_MAX);
            pct = s->glow_spread;
            mark = lanePlace(GLOW_SPREAD_DEFAULT, GLOW_SPREAD_MIN, GLOW_SPREAD_MAX);
        } else {
            t = lanePlace(s->bg_dim, BG_DIM_MIN, BG_DIM_MAX);
            pct = s->bg_dim;
            mark = lanePlace(BG_DIM_DEFAULT, BG_DIM_MIN, BG_DIM_MAX);
        }
        char buf[16];
        snprintf(buf, sizeof buf, "%d%%", pct);
        track_row(dc, COG_TAB_ROOM, row, name, mark, t, buf);
    }
    reset_button(dc, reset_live(v, COG_TAB_ROOM));
}

static void picture_text(int row, int units, char* out, size_t cap) {
    switch (row) {
    case PICTURE_CONTRAST:
    case PICTURE_SATURATION: snprintf(out, cap, "%d%%", units); break;
    case PICTURE_GAMMA: snprintf(out, cap, "%.2f", units / 100.0); break;
    default: snprintf(out, cap, units > 0 ? "+%d" : "%d", units); break;
    }
}

static void tab_display(Dc* dc) {
    const Settings* s = dc->v->settings;
    static const int MIN[PICTURE_VALUES] = { PICTURE_BRIGHTNESS_MIN, PICTURE_CONTRAST_MIN, PICTURE_GAMMA_MIN, PICTURE_SATURATION_MIN };
    static const int MAX[PICTURE_VALUES] = { PICTURE_BRIGHTNESS_MAX, PICTURE_CONTRAST_MAX, PICTURE_GAMMA_MAX, PICTURE_SATURATION_MAX };
    static const int DEF[PICTURE_VALUES] = { PICTURE_BRIGHTNESS_DEFAULT, PICTURE_CONTRAST_DEFAULT, PICTURE_GAMMA_DEFAULT,
                                             PICTURE_SATURATION_DEFAULT };
    for (int row = 0; row < COG_DISPLAY_ROW_COUNT; row++) {
        if (cogRowOption(COG_TAB_DISPLAY, row) >= 0) {
            option_row(dc, COG_TAB_DISPLAY, row);
            continue;
        }
        int units = settings_picture_value(s, row);
        char buf[24];
        picture_text(row, units, buf, sizeof buf);
        track_row(dc, COG_TAB_DISPLAY, row, row_name(COG_TAB_DISPLAY, row), lanePlace(DEF[row], MIN[row], MAX[row]),
                  lanePlace(units, MIN[row], MAX[row]), buf);
    }
    reset_button(dc, reset_live(dc->v, COG_TAB_DISPLAY));
}

// The Mode row, with the controllers' own shortcut under its label
static void mode_row(Dc* dc) {
    const Settings* s = dc->v->settings;
    const int row = COG_CONTROLS_ROW_MODE;
    static const UiStr HINT[3] = { UIS_SHORTCUT_MENU_GRIP, UIS_SHORTCUT_STICKS, UIS_SHORTCUT_TRIGGERS_GRIPS };
    int sc = s->gamepad_toggle < 0 || s->gamepad_toggle > 2 ? 0 : s->gamepad_toggle;
    const char* name = row_name(COG_TAB_CONTROLS, row);
    float y = row_y(dc, COG_TAB_CONTROLS, row);
    // native (lane wave6-i18n): under a label that reaches below its baseline (a translation's
    // p, j, Ñ) the shortcut line moves down clear of it, into the room above the next row;
    // "Mode" does not, and both stay where they were. A long label shrinks to its room.
    float below = 0.0f;
    ui_text_ink_extent(dc->font, UI_WEIGHT_REGULAR, LABEL_SIZE * dc->k, name, NULL, &below);
    float drop = below / dc->k > 2.5f ? below / dc->k - 2.5f : 0.0f;
    float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, LABEL_SIZE * dc->k, LABEL_MIN_SIZE * dc->k, name,
                                 (label_right(dc) - LABEL_X * dc->nw) * dc->k) / dc->k;
    dc_text(dc, UI_WEIGHT_REGULAR, fit, LABEL_X * dc->nw, y, UI_ALIGN_LEFT, WHITE, name);
    dc_text(dc, UI_WEIGHT_REGULAR, 15.0f, LABEL_X * dc->nw, y + 15.0f + drop, UI_ALIGN_LEFT, 0x99FFFFFFu, ui_str(HINT[sc]));
    cells(dc, COG_TAB_CONTROLS, row, OPT_MODE, COG_GAMEPAD_CELLS, y, 1, cell_half_of(dc, COG_TAB_CONTROLS),
          ui_cog_option_cell(dc->v, COG_OPTION_GAMEPAD));
}

static void tab_controls(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    float half = cell_half_of(dc, COG_TAB_CONTROLS);
    for (int row = 0; row < COG_CONTROLS_ROW_COUNT; row++) {
        const char* name = row_name(COG_TAB_CONTROLS, row);
        float y = row_y(dc, COG_TAB_CONTROLS, row);
        int why = row_why(v, COG_TAB_CONTROLS, row);
        if (why != COG_LIVE) {
            dead_row(dc, COG_TAB_CONTROLS, row, name, why);
        } else if (row == COG_CONTROLS_ROW_MODE) {
            mode_row(dc);
        } else if (row == COG_CONTROLS_ROW_MODELS && (v->pt_hides & UI_PT_HIDES_CONTROLLERS)) {
            // native (1.3.4): greyed with why, and a press on it asks (the sheet "Controllers in Passthrough?")
            label(dc, name, y, 0);
            greyed_reason(dc, y, ui_str(UIS_OFF_IN_PASSTHROUGH));
            int n = cogRowCells(COG_TAB_CONTROLS, row);
            float rv = cogRowV(COG_TAB_CONTROLS, row), rh = cogRowHalf(COG_TAB_CONTROLS);
            for (int c = 0; c < n; c++) {
                dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / (float)n, rv - rh,
                          COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / (float)n, rv + rh, 1);
            }
        } else if (cogRowOption(COG_TAB_CONTROLS, row) >= 0) {
            option_row(dc, COG_TAB_CONTROLS, row);
        } else {
            char buf[32];
            float mark, t;
            if (row == COG_CONTROLS_ROW_SCROLL) {
                // native (1.3.4): its places are the only speeds it has, so each has a tick, the usual one's the tallest
                for (int i = 0; i <= COG_SCROLL_STEPS; i++) {
                    if (SCROLL_STEP_PERCENT[i] == SCROLL_SPEED_DEFAULT) continue;
                    tick(dc, cogRunU((float)i / COG_SCROLL_STEPS) * dc->nw, y, half * 0.55f, 0);
                }
                mark = scrollTrackPlace(SCROLL_SPEED_DEFAULT);
                t = scrollTrackPlace(s->scroll_speed);
                snprintf(buf, sizeof buf, "%d%%", s->scroll_speed);
            } else if (row == COG_CONTROLS_ROW_AIM_SENSITIVITY) {
                mark = lanePlace(HEAD_AIM_SENSITIVITY_DEFAULT, HEAD_AIM_SENSITIVITY_MIN, HEAD_AIM_SENSITIVITY_MAX);
                t = lanePlace(s->head_aim_sensitivity, HEAD_AIM_SENSITIVITY_MIN, HEAD_AIM_SENSITIVITY_MAX);
                snprintf(buf, sizeof buf, "%d px/\xC2\xB0", s->head_aim_sensitivity);
            } else {
                mark = lanePlace(HEAD_AIM_DEADZONE_DEFAULT, HEAD_AIM_DEADZONE_MIN, HEAD_AIM_DEADZONE_MAX);
                t = lanePlace(s->head_aim_deadzone, HEAD_AIM_DEADZONE_MIN, HEAD_AIM_DEADZONE_MAX);
                snprintf(buf, sizeof buf, "%d\xC2\xB0/s", s->head_aim_deadzone);
            }
            track_row(dc, COG_TAB_CONTROLS, row, name, mark, t, buf);
        }
    }
}

// Native (PLAN 4.3): a row with nothing to set while the PC draws the pair,
// greyed with the reason across it and its hits disabled
static void host_drawn_row(Dc* dc, int row) {
    greyed_reason(dc, row_y(dc, COG_TAB_3D, row), ui_str(UIS_SBS_NO_DEPTH));
    dead_hits(dc, COG_TAB_3D, row);
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
    static const UiStr SOURCES[COG_SOURCE_CELLS] = { UIS_ROW_DEPTH_MODEL, UIS_SOURCE_SBS, UIS_SOURCE_TB };
    float tick_half = COG_3D_CELL_HALF * dc->nh;
    int presets[COG_PRESET_CELLS];
    ui_presets_for(s, presets);
    int sep = settings_separation_units(s);

    float y = row_y(dc, COG_TAB_3D, COG_ROW3D_PRESET);
    label(dc, ui_str(UIS_ROW_PRESET), y, ok && !host_drawn);
    if (host_drawn) {
        host_drawn_row(dc, COG_ROW3D_PRESET);
    } else {
        cells(dc, COG_TAB_3D, COG_ROW3D_PRESET, PRESETS, COG_PRESET_CELLS, y, ok, tick_half, ok ? cogPresetAt(sep, presets) : -1);
    }

    int def_sep = settings_model_default_separation(s);
    for (int i = 0; i < 2; i++) {
        int row = COG_ROW3D_SEPARATION + i;
        y = row_y(dc, COG_TAB_3D, row);
        label(dc, ui_str(i == 0 ? UIS_ROW_DEPTH : UIS_ROW_CONVERGENCE), y, ok && (i == 1 || !host_drawn));
        if (i == 0 && host_drawn) {
            host_drawn_row(dc, row);
            continue;
        }
        float mark_t = row == COG_ROW3D_SEPARATION ? (float)def_sep / COG_SEP_STEPS : 0.5f;
        float mark_x = (COG_RUN_L + mark_t * (COG_RUN_R - COG_RUN_L)) * dc->nw;
        if (row == COG_ROW3D_SEPARATION) {
            // past the model's default depth stops growing and only the strain does
            dc_line(dc, COG_RUN_L * dc->nw, y, mark_x, y, 6.0f, ok ? 0x66FFFFFFu : 0x30FFFFFFu);
            dc_line(dc, mark_x, y, COG_RUN_R * dc->nw, y, 6.0f, ok ? 0x66FFB74Du : 0x30FFB74Du);
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
    // native (1.3.3, issue #6): the depth model, Light or Full (depth_source zipdepth-int8 or zipdepth); with
    // the 3D off in settings its place says why instead
    static const UiStr MODELS[COG_MODEL_CELLS] = { UIS_DEPTH_LIGHT, UIS_DEPTH_FULL };
    y = row_y(dc, COG_TAB_3D, COG_ROW3D_MODEL);
    int model = s->depth_source == DEPTH_SOURCE_LIGHT ? COG_MODEL_LIGHT
              : s->depth_source == DEPTH_SOURCE_FULL ? COG_MODEL_FULL : -1;
    if (!ok) {
        // (no hits there: the words take the row, and the ray passes over a dead row anyway)
        const char* why = v->stereo_reason ? v->stereo_reason : ui_str(UIS_THREE_D_OFF_IN_SETTINGS);
        float room = (COG_TRACK_R - COG_TRACK_L) * dc->nw;
        float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 19.0f * dc->k, 12.0f * dc->k, why, room * dc->k) / dc->k;
        label(dc, ui_str(UIS_ROW_DEPTH_MODEL), y, 0);
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, fit, (COG_TRACK_L + COG_TRACK_R) * 0.5f * dc->nw, y, room, 0x50FFFFFFu, why);
    } else if (host_drawn) {
        label(dc, ui_str(UIS_ROW_DEPTH_MODEL), y, 0);
        host_drawn_row(dc, COG_ROW3D_MODEL);
    } else {
        label(dc, ui_str(UIS_ROW_DEPTH_MODEL), y, 1);
        cells(dc, COG_TAB_3D, COG_ROW3D_MODEL, MODELS, COG_MODEL_CELLS, y, 1, tick_half, model);
    }
    reset_button(dc, reset_live(v, COG_TAB_3D));
}

// Native (lane a11y-tab, a11y-2): the Accessibility tab. Pointer (Controller, Head, Eyes; Eyes
// dimmed only where the runtime has no eye gaze), the dwell click's cells and the Dot size track;
// under them the eye tracking line and a few short lines on how to use it and turn it off, and in
// the footer one sentence on what the row under the pointer does.

// The pointer row's cells, one of which (Eyes) may be dimmed on its own: drawn as cells() draws a
// row, with each cell live or not
// Native (lane a11y-final): a cell's name with a small, quieter tag under it ("Eyes" over
// "experimental"), the two inside the cell. 0 (nothing drawn) when either does not fit: the caller
// draws the name alone, and the footer's sentence still says it.
static int tagged_cell(Dc* dc, const char* name, const char* tag, float x0, float x1, float y, float cell_half,
                       int live) {
    const float room = (x1 - x0) - 12.0f, ns = 16.0f, ts = 12.0f;
    if (dc_text_width(dc, UI_WEIGHT_REGULAR, ns, name) > room || dc_text_width(dc, UI_WEIGHT_REGULAR, ts, tag) > room) {
        return 0;
    }
    float cx = (x0 + x1) * 0.5f;
    dc_text_center(dc, UI_WEIGHT_REGULAR, ns, cx, y - cell_half * 0.35f, live ? WHITE : DIM, name);
    dc_text_center(dc, UI_WEIGHT_REGULAR, ts, cx, y + cell_half * 0.55f, live ? 0xB0FFFFFFu : 0x20FFFFFFu, tag);
    return 1;
}

static void access_cells(Dc* dc, int row, const UiStr* names, int n, float y, int row_live, float cell_half,
                         int mark) {
    const UiView* v = dc->v;
    float track_l = COG_TRACK_L * dc->nw, track_r = COG_TRACK_R * dc->nw;
    float span = (track_r - track_l) / (float)n;
    float rv = cogRowV(COG_TAB_ACCESS, row), half = cogRowHalf(COG_TAB_ACCESS);
    for (int i = 0; i < n; i++) {
        int live = row_live && ui_cog_access_cell_live(v, row, i);
        float x0 = track_l + (float)i * span + 3.0f, x1 = track_l + (float)(i + 1) * span - 3.0f;
        int hot = live && dc_hover(dc, UI_HIT_CELL, row, i);
        dc_fill_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, live ? (hot ? 0x40FFFFFFu : 0x28FFFFFFu) : 0x10FFFFFFu);
        dc_stroke_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, 2.0f, live ? 0x50FFFFFFu : 0x20FFFFFFu);
        // the Eyes pointer is experimental, and its cell says so (lane a11y-final)
        int tagged = row == COG_ACCESS_ROW_POINTER && i == POINTER_SOURCE_GAZE
                && tagged_cell(dc, ui_str(names[i]), ui_str(UIS_CELL_EYES_EXPERIMENTAL), x0, x1, y, cell_half, live);
        if (!tagged) {
            float fit = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 19.0f * dc->k, 12.0f * dc->k, ui_str(names[i]),
                                         ((x1 - x0) - 8.0f) * dc->k) / dc->k;
            dc_text_center(dc, UI_WEIGHT_REGULAR, fit, (x0 + x1) * 0.5f, y, live ? WHITE : DIM, ui_str(names[i]));
        }
        if (live && i == mark) {
            dc_stroke_rrect(dc, x0 + 1.5f, y - cell_half + 1.5f, x1 - 1.5f, y + cell_half - 1.5f, 10.0f, 3.0f, WHITE);
        } else if (hot) {
            dc_stroke_rrect(dc, x0, y - cell_half, x1, y + cell_half, 10.0f, 2.5f, 0xB0FFFFFFu);
        }
        dc_hit_uv(dc, UI_HIT_CELL, row, i, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)i / (float)n,
                  rv - half, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(i + 1) / (float)n, rv + half, live);
    }
}

// The Dot size track: a tick at every 25 % step (the default's taller), the thumb and its percent
static void access_dot_track(Dc* dc, int live, float tick_half) {
    const Settings* s = dc->v->settings;
    int row = COG_ACCESS_ROW_DOT;
    float y = row_y(dc, COG_TAB_ACCESS, row);
    track_line(dc, y, live ? 0x66FFFFFFu : 0x30FFFFFFu);
    for (int i = 0; i <= ACCESS_DOT_STEPS; i++) {
        int pct = ACCESS_DOT_MIN + i * ACCESS_DOT_STEP;
        float x = cogRunU(lanePlace(pct, ACCESS_DOT_MIN, ACCESS_DOT_MAX)) * dc->nw;
        tick(dc, x, y, pct == ACCESS_DOT_DEFAULT ? tick_half : tick_half * 0.45f, live);
    }
    chevrons(dc, y, live, tick_half);
    if (live) {
        int pct = accessDotClamp(s->assist_dot_size);
        char buf[16];
        snprintf(buf, sizeof buf, "%d%%", pct);
        thumb(dc, COG_TAB_ACCESS, row, y, lanePlace(pct, ACCESS_DOT_MIN, ACCESS_DOT_MAX));
        readout(dc, cogRowV(COG_TAB_ACCESS, row), buf);
    }
    track_hits(dc, COG_TAB_ACCESS, row, live);
}

// Up to max_lines of text wrapped to room from a baseline down; returns the baseline after them
static float access_wrap(Dc* dc, const char* text, float left, float baseline, float room, float step, int max_lines,
                         uint32_t color) {
    UiTextLine lines[12];
    int got = ui_text_wrap(dc->font, UI_WEIGHT_REGULAR, 19.0f * dc->k, text, room * dc->k, lines, 12);
    if (got > max_lines) got = max_lines;
    for (int i = 0; i < got && baseline < dc->nh - 8.0f; i++) {
        char buf[512];
        int len = lines[i].len < 511 ? lines[i].len : 511;
        memcpy(buf, text + lines[i].start, (size_t)len);
        buf[len] = 0;
        dc_text(dc, UI_WEIGHT_REGULAR, 19.0f, left, baseline, UI_ALIGN_LEFT, color, buf);
        baseline += step;
    }
    return baseline;
}

// Native (lane eye-calibration): the Eye calibration row's name with the calibration's state under
// it (what config.ini holds, or how this session's last run failed), both shrunk to the label's room
static void eyecal_label(Dc* dc, float y, int live) {
    const UiView* v = dc->v;
    char line[96];
    uint32_t ink = 0xB0FFFFFFu;
    EyeCal stored;
    int have = eyeCalParse(v->settings->eye_calibration, &stored);
    if (v->access_cal > EYECAL_OK && v->access_cal < EYECAL_RESULTS) {
        static const UiStr WHY[EYECAL_RESULTS] = { UIS_EYE_CAL_DONE, UIS_EYE_CAL_FAIL_FEW, UIS_EYE_CAL_FAIL_GAIN,
                                                   UIS_EYE_CAL_FAIL_EXTREME };
        snprintf(line, sizeof line, "%s", ui_str(WHY[v->access_cal]));
        ink = 0xEEFFC870u;
    } else if (have) {
        snprintf(line, sizeof line, ui_str(UIS_EYE_CAL_DONE), (double)stored.meanAfter);
        ink = 0xEE9BE7A0u;
    } else {
        snprintf(line, sizeof line, "%s", ui_str(UIS_EYE_CAL_NONE));
    }
    float left = LABEL_X * dc->nw, room = label_right(dc) - left;
    float name = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 20.0f * dc->k, LABEL_MIN_SIZE * dc->k,
                                  ui_str(UIS_ROW_EYE_CAL), room * dc->k) / dc->k;
    float state = ui_text_fit_size(dc->font, UI_WEIGHT_REGULAR, 15.0f * dc->k, 11.0f * dc->k, line,
                                   room * dc->k) / dc->k;
    // greyed, the name alone, level with the reason beside it
    dc_text_left_mid(dc, UI_WEIGHT_REGULAR, name, left, live ? y - 9.0f : y, live ? WHITE : DIM, ui_str(UIS_ROW_EYE_CAL));
    if (live) {
        dc_text_left_mid(dc, UI_WEIGHT_REGULAR, state, left, y + 12.0f, ink, line);
    }
}

// Native (lane eye-calibration): the run's label, which the cog's panel shows over the middle of the
// picture while a calibration is up: what to do, the dot's number and how to stop. Clear round it,
// so the dots either side of the middle stay in view (xr_layers.c).
static void eyecal_face(Dc* dc) {
    const UiView* v = dc->v;
    float x0 = 0.25f * dc->nw, x1 = 0.75f * dc->nw, y0 = 0.38f * dc->nh, y1 = 0.62f * dc->nh;
    float mid = dc->nw * 0.5f, room = (x1 - x0) - 24.0f;
    dc_fill_rrect(dc, x0, y0, x1, y1, 18.0f, 0xE0141416u);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 30.0f, mid, 0.44f * dc->nh, room, WHITE, ui_str(UIS_EYE_CAL_LOOK));
    char progress[32];
    snprintf(progress, sizeof progress, ui_str(UIS_EYE_CAL_PROGRESS), v->eyecal_step, EYECAL_DOTS);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 25.0f, mid, 0.505f * dc->nh, room, 0xD0FFFFFFu, progress);
    dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 19.0f, mid, 0.57f * dc->nh, room, 0x90FFFFFFu, ui_str(UIS_EYE_CAL_CANCEL));
}

static void tab_access(Dc* dc) {
    const UiView* v = dc->v;
    const Settings* s = v->settings;
    static const UiStr ROWS[COG_ACCESS_ROW_COUNT] = { UIS_ROW_POINTER_SOURCE, UIS_ROW_DWELL, UIS_ROW_DWELL_STYLE,
                                                      UIS_ROW_DOT_SIZE, UIS_ROW_EYE_CAL };
    static const UiStr POINTER[COG_ACCESS_POINTER_CELLS] = { UIS_CELL_CONTROLLER, UIS_CELL_HEAD, UIS_CELL_EYES };
    static const UiStr DWELL[COG_ACCESS_DWELL_CELLS] = { UIS_OFF, UIS_DWELL_06, UIS_DWELL_09, UIS_DWELL_12, UIS_DWELL_15 };
    static const UiStr STYLE[COG_ACCESS_STYLE_CELLS] = { UIS_CELL_DWELL_SIMPLE, UIS_CELL_DWELL_RADIAL };
    static const UiStr DESC[COG_ACCESS_ROW_COUNT] = { UIS_ACCESS_DESC_POINTER, UIS_ACCESS_DESC_DWELL,
                                                      UIS_ACCESS_DESC_DWELL_STYLE, UIS_ACCESS_DESC_DOT,
                                                      UIS_ACCESS_DESC_EYE_CAL };
    static const UiStr EYECAL[COG_ACCESS_EYECAL_CELLS] = { UIS_CELL_EYE_CAL_START, UIS_RESET };
    static const UiStr EYES[ACCESS_EYES_COUNT] = { UIS_EYES_READY, UIS_EYES_WAITING, UIS_EYES_NO_ACTION,
                                                   UIS_EYES_UNAVAILABLE };
    float cell_half = COG_ACCESS_CELL_HALF * dc->nh;
    int eyes = v->access_eyes >= 0 && v->access_eyes < ACCESS_EYES_COUNT ? v->access_eyes : ACCESS_EYES_UNAVAILABLE;
    for (int row = 0; row < COG_ACCESS_ROW_COUNT; row++) {
        int live = ui_cog_row_live(v, COG_TAB_ACCESS, row);
        float y = row_y(dc, COG_TAB_ACCESS, row);
        if (row == COG_ACCESS_ROW_EYECAL) {
            // native (lane eye-calibration): Start and Reset while the eyes reach the app, else why not
            label_flush(dc, label_right(dc));
            eyecal_label(dc, y, live);
            int n = ui_cog_access_cells(v, row);
            if (live) {
                access_cells(dc, row, EYECAL, n, y, 1, cell_half, -1);
            } else {
                dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 19.0f, (COG_TRACK_L + COG_TRACK_R) * 0.5f * dc->nw, y,
                                   (COG_TRACK_R - COG_TRACK_L) * dc->nw, 0xEEFFC870u, ui_str(EYES[eyes]));
                float rv = cogRowV(COG_TAB_ACCESS, row), half = cogRowHalf(COG_TAB_ACCESS);
                for (int c = 0; c < n; c++) {
                    dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / (float)n,
                              rv - half, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / (float)n, rv + half, 0);
                }
            }
            continue;
        }
        label(dc, ui_str(ROWS[row]), y, live);
        if (row == COG_ACCESS_ROW_DOT) {
            if (!live) {
                label_flush(dc, label_right(dc));
                greyed_reason(dc, y, ui_str(UIS_NEEDS_HEAD_POINTER));
                track_hits(dc, COG_TAB_ACCESS, row, 0);
            } else {
                access_dot_track(dc, 1, cell_half);
            }
            continue;
        }
        int n = ui_cog_access_cells(v, row);
        if (!live) {
            greyed_reason(dc, y, ui_str(UIS_NEEDS_HEAD_POINTER));
            float rv = cogRowV(COG_TAB_ACCESS, row), half = cogRowHalf(COG_TAB_ACCESS);
            for (int c = 0; c < n; c++) {
                dc_hit_uv(dc, UI_HIT_CELL, row, c, COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)c / (float)n, rv - half,
                          COG_TRACK_L + (COG_TRACK_R - COG_TRACK_L) * (float)(c + 1) / (float)n, rv + half, 0);
            }
            continue;
        }
        if (row == COG_ACCESS_ROW_POINTER) {
            access_cells(dc, row, POINTER, n, y, 1, cell_half, s->pointer_source < n ? s->pointer_source : -1);
        } else if (row == COG_ACCESS_ROW_DWELL) {
            cells(dc, COG_TAB_ACCESS, row, DWELL, n, y, 1, cell_half, accessDwellCell(s->head_dwell_ms));
        } else if (row == COG_ACCESS_ROW_STYLE) {
            cells(dc, COG_TAB_ACCESS, row, STYLE, n, y, 1, cell_half,
                  s->assist_dwell_style == DWELL_STYLE_RADIAL ? DWELL_STYLE_RADIAL : DWELL_STYLE_SIMPLE);
        }
    }
    label_flush(dc, label_right(dc));
    // whether eye tracking reaches the app, so the user sees why Eyes does nothing (and why it is dimmed);
    // native (lane eye-calibration): while it does not, the calibration's row says it in its place
    float left = LABEL_X * dc->nw, room = COG_TRACK_R * dc->nw - left;
    if (eyes == ACCESS_EYES_READY) {
        dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 19.0f, left, COG_ACCESS_STATUS_V * dc->nh, room, 0xEE9BE7A0u,
                         ui_str(EYES[eyes]));
    }
    // the instructions, and how to get the eyes working while they are not
    float baseline = COG_ACCESS_HELP_T * dc->nh, step = 26.0f;
    float footer = COG_ACCESS_FOOTER_RULE * dc->nh;
    baseline = access_wrap(dc, ui_str(UIS_ACCESS_HELP), left, baseline, room, step, 8, 0xB0FFFFFFu) + 6.0f;
    if (eyes != ACCESS_EYES_READY && baseline < footer - 8.0f) {
        access_wrap(dc, ui_str(UIS_ACCESS_HELP_EYES), left, baseline, room, step,
                    (int)((footer - 8.0f - baseline) / step) + 1, 0xB0FFFFFFu);
    }
    // native (lane a11y-2): the footer, what the row under the pointer (or the dot) does
    int hover = v->access_row;
    if (hover < 0 && (v->hover.kind == UI_HIT_CELL || v->hover.kind == UI_HIT_TRACK_RUN
                      || v->hover.kind == UI_HIT_TRACK_UP || v->hover.kind == UI_HIT_TRACK_DOWN)) {
        hover = v->hover.a;
    }
    dc_fill_rrect(dc, 20.0f, footer, dc->nw - 20.0f, footer + 2.0f, 0.0f, 0x30FFFFFFu);
    const char* desc = hover >= 0 && hover < COG_ACCESS_ROW_COUNT ? ui_str(DESC[hover]) : ui_str(UIS_ACCESS_DESC_NONE);
    access_wrap(dc, desc, left, COG_ACCESS_FOOTER_T * dc->nh, room, step, 3,
                hover >= 0 && hover < COG_ACCESS_ROW_COUNT ? 0xF0FFFFFFu : 0x70FFFFFFu);
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
    about_button(dc, COG_REPORT_L, COG_REPORT_T, COG_REPORT_R, COG_REPORT_B, ui_str(UIS_REPORT_TITLE), UI_BTN_REPORT);
    line_fit_center(dc, 17.0f, mid, (COG_REPORT_B + 0.05f) * dc->nh, room, 0x80FFFFFFu, ui_str(UIS_REPORT_SENDS), UI_ELLIPSIZE_END);
    line_fit_center(dc, 17.0f, mid, 0.915f * dc->nh, room, 0x90FFFFFFu, ui_str(UIS_ABOUT_BUILT_BY), UI_ELLIPSIZE_END);
    line_fit_center(dc, 15.0f, mid, 0.96f * dc->nh, room, 0x70FFFFFFu, ui_str(UIS_ABOUT_BASED_ON), UI_ELLIPSIZE_END);
}

void panel_cog(Dc* dc) {
    int tab = dc->v->cog_tab < 0 || dc->v->cog_tab >= COG_TAB_COUNT ? COG_TAB_SCREEN : dc->v->cog_tab;
    dc->label_text = NULL;
    // native (lane eye-calibration): a run's label in place of the whole panel
    if (dc->v->eyecal_step > 0) {
        eyecal_face(dc);
        return;
    }
    chrome(dc, tab);
    switch (tab) {
    case COG_TAB_SCREEN: tab_screen(dc); break;
    case COG_TAB_ROOM: tab_room(dc); break;
    case COG_TAB_DISPLAY: tab_display(dc); break;
    case COG_TAB_3D: tab_3d(dc); break;
    case COG_TAB_CONTROLS: tab_controls(dc); break;
    case COG_TAB_ACCESS: tab_access(dc); break;
    case COG_TAB_ABOUT: tab_about(dc); break;
    default: break;
    }
    label_flush(dc, label_right(dc));
}
