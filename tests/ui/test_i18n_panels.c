// Every panel in every language (PLAN 6.1): each text drawn is traced (text.h ui_text_set_trace)
// and must be whole (no .notdef box, not cut with an ellipsis when it is a string of the table),
// on the panel, inside the cell, key, tab or button it sits in, clear of the cells it does not sit
// in, and clear of the other texts. The panels of a few languages are written as PNGs, one folder
// per language, into argv[1] (build/ui/tests/i18n-panels, part of CI's test-pictures), those that
// differ from the English ones (see write_pngs). Chinese, Japanese and Korean
// run when the system's Noto Sans CJK is found ($MLXR_SYSTEM_FONT_DIR or /usr/share/fonts), and
// are reported as skipped otherwise. GPLv3.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "panels.h"
#include "settings.h"
#include "text.h"
#include "xr_kbfull.h"
#include "ui_strings.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#define MAX_EVENTS 512
typedef struct {
    UiTextTraceEvent e;
    char text[400];
} Ev;
static Ev evs[MAX_EVENTS];
static int nev;
static int g_lang;
static const char* g_out;
static int problems_lang;   // this language's

static void on_text(const UiTextTraceEvent* e, void* user) {
    (void)user;
    if (nev >= MAX_EVENTS) return;
    Ev* v = &evs[nev++];
    v->e = *e;
    int n = e->len < (int)sizeof v->text - 1 ? e->len : (int)sizeof v->text - 1;
    memcpy(v->text, e->text, (size_t)n);
    v->text[n] = 0;
    v->e.text = v->text;
}

// ---- the states ------------------------------------------------------------------------------------

typedef struct {
    char name[64];
    UiPanelId id;
    UiView v;
    Settings s;
    int kind;              // 0 ui_draw_panel, 1 toast, 2 report, 3 pc list, 4 app list, 5 clock strip
    char toast[256], more[256];
    int more_is_path;
    UiReport report;
    int list_mode;         // pc: 0 full, 1 empty; app: 0 full, 1 loading, 2 failed, 3 empty
    UiClock clock;
    char modal[128];       // the first text of a dialog drawn over the panel: texts from it on are a layer
} State;

#define MAX_STATES 160
static State states[MAX_STATES];
static int nstates;

static State* add(const char* name, UiPanelId id, const Settings* s) {
    State* st = &states[nstates++];
    memset(st, 0, sizeof *st);
    snprintf(st->name, sizeof st->name, "%s", name);
    st->id = id;
    st->s = *s;
    ui_view_init(&st->v, &st->s);
    st->v.version = "1.2.0";
    st->v.commit = "46e738f";
    st->v.log_path = "/home/steamos/Games/MoonlightXRNative/moonlight.log";
    st->v.curve_ok = 1;
    st->v.stereo_ok = 1;
    st->v.env_live_cells = ENV_CELL_COUNT;
    st->v.host_confirm = -1;
    return st;
}

static void toast(const char* name, const char* text, const char* more, int path) {
    Settings s;
    settings_defaults(&s);
    State* st = add(name, UI_PANEL_TOAST, &s);
    st->kind = 1;
    snprintf(st->toast, sizeof st->toast, "%s", text);
    snprintf(st->more, sizeof st->more, "%s", more ? more : "");
    st->more_is_path = path;
}

// Built for each language: the toasts format the language's strings
static void build_states(void) {
    nstates = 0;
    Settings s;
    settings_defaults(&s);
    static const char* const TABS[COG_TAB_COUNT] = { "screen", "display", "3d", "picture", "about" };
    char name[64];
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        snprintf(name, sizeof name, "cog-%s", TABS[tab]);
        State* st = add(name, UI_PANEL_COG, &s);
        st->v.cog_tab = tab;
    }
    // head locked (the aim rows live), the 3D tab greyed, Display rows later, a room
    Settings locked = s;
    locked.head_locked = 1;
    locked.head_aim = 1;
    State* st = add("cog-screen-locked", UI_PANEL_COG, &locked);
    st->v.cog_tab = COG_TAB_SCREEN;
    st = add("cog-screen-nocurve", UI_PANEL_COG, &s);
    st->v.cog_tab = COG_TAB_SCREEN;
    st->v.curve_ok = 0;
    st = add("cog-3d-off", UI_PANEL_COG, &s);
    st->v.cog_tab = COG_TAB_3D;
    st->v.stereo_ok = 0;
    st = add("cog-3d-nomodel", UI_PANEL_COG, &s);
    st->v.cog_tab = COG_TAB_3D;
    st->v.stereo_ok = 0;
    st->v.stereo_reason = ui_str(UIS_NATIVE_3D_NO_MODEL);
    st = add("cog-display-later", UI_PANEL_COG, &s);
    st->v.cog_tab = COG_TAB_DISPLAY;
    st->v.display_later = 0xFFFFFFFFu;
    st = add("cog-about-nolog", UI_PANEL_COG, &s);
    st->v.cog_tab = COG_TAB_ABOUT;
    st->v.log_path = NULL;
    st->v.commit = NULL;
    static const int ROOMS[3] = { ENV_ID_HOME_THEATER, ENV_ID_GRAND_CINEMA, ENV_ID_SYNTHWAVE };
    for (int r = 0; r < 3; r++) {
        snprintf(name, sizeof name, "cog-room-%d", ROOMS[r]);
        st = add(name, UI_PANEL_COG, &s);
        st->v.room_env = ROOMS[r];
        st->v.cog_tab = COG_TAB_SCREEN;
    }
    st = add("cog-room-display", UI_PANEL_COG, &s);
    st->v.room_env = ENV_ID_HOME_THEATER;
    st->v.cog_tab = COG_TAB_DISPLAY;
    st = add("picker", UI_PANEL_PICKER, &s);
    st->v.env_selected = ENV_CELL_HOME_THEATER;
    st = add("picker-later", UI_PANEL_PICKER, &s);
    st->v.env_live_cells = 2;
    for (int layout = 0; layout < 2; layout++) {
        for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
            snprintf(name, sizeof name, "keyboard-%s-%d", layout ? "full" : "compact", sheet);
            st = add(name, UI_PANEL_KEYBOARD, &s);
            st->v.kb_layout = layout;
            st->v.kb_sheet = sheet;
            st->v.kb_chip = 1;
        }
    }
    st = add("keyboard-chord", UI_PANEL_KEYBOARD, &s);
    st->v.kb_layout = KB_LAYOUT_FULL;
    st->v.kb_chord = 1;
    st = add("bar", UI_PANEL_BAR, &s);
    st->v.bar_show_aim = 1;
    st->v.bar_show_stereo = 1;
    add("button-atlas", UI_PANEL_BUTTON_ATLAS, &s);
    add("exit", UI_PANEL_EXIT, &s);
    st = add("exit-stream", UI_PANEL_EXIT, &s);
    st->v.exit_disconnect = 1;
    st = add("exit-app", UI_PANEL_EXIT, &s);
    st->v.exit_question = ui_str(UIS_NATIVE_EXIT_APP);
    add("hint", UI_PANEL_HINT, &s);
    add("kofi", UI_PANEL_KOFI, &s);
    add("host", UI_PANEL_HOST, &s);
    st = add("host-macros", UI_PANEL_HOST, &s);
    st->v.host_macros = 3;
    snprintf(st->v.host_macro[0], XR_MACRO_NAME, "Screenshot");
    snprintf(st->v.host_macro[1], XR_MACRO_NAME, "OBS record");
    snprintf(st->v.host_macro[2], XR_MACRO_NAME, "Mute mic");
    st = add("host-confirm", UI_PANEL_HOST, &s);
    st->v.host_confirm = 0;
    snprintf(st->v.host_confirm_label, XR_MACRO_NAME, "Alt+Tab");
    snprintf(st->modal, sizeof st->modal, ui_str(UIS_HOST_CONFIRM_Q), "Alt+Tab");
    // the report sheet
    static const char* const NOTE = "The picture froze when I opened the keyboard.";
    st = add("report", UI_PANEL_REPORT, &s);
    st->kind = 2;
    st->report = (UiReport){ "", REPORT_ZONE_NOTE, REPORT_ZONE_NONE, 0 };
    st = add("report-filled", UI_PANEL_REPORT, &s);
    st->kind = 2;
    st->report = (UiReport){ NOTE, REPORT_ZONE_NOTE, REPORT_ZONE_SEND, 1 };
    // the toasts as xr_panels.c toastText and the app's notices make them
    char a[256];
    snprintf(a, sizeof a, ui_str(UIS_TOAST_RATE), 120);
    toast("toast-rate", a, NULL, 0);
    toast("toast-hands-locked", ui_str(UIS_TOAST_HANDS_LOCKED), NULL, 0);
    toast("toast-hands-unlocked", ui_str(UIS_TOAST_HANDS_UNLOCKED), NULL, 0);
    toast("toast-3d-on", ui_str(UIS_TOAST_3D_ON), NULL, 0);
    toast("toast-3d-off", ui_str(UIS_TOAST_3D_OFF), NULL, 0);
    toast("toast-head-aim-on", ui_str(UIS_TOAST_HEAD_AIM_ON), ui_str(UIS_TOAST_HEAD_AIM_ON_MORE), 0);
    toast("toast-head-aim-off", ui_str(UIS_TOAST_HEAD_AIM_OFF), NULL, 0);
    toast("toast-pad-menu", ui_str(UIS_TOAST_GAMEPAD_MODE), ui_str(UIS_TOAST_BACK_MENU_GRIP), 0);
    toast("toast-pad-sticks", ui_str(UIS_TOAST_GAMEPAD_MODE), ui_str(UIS_TOAST_BACK_STICKS), 0);
    toast("toast-pad-triggers", ui_str(UIS_TOAST_GAMEPAD_MODE), ui_str(UIS_TOAST_BACK_TRIGGERS_GRIPS), 0);
    toast("toast-pointer", ui_str(UIS_TOAST_POINTER_MODE), NULL, 0);
    toast("toast-3d-failed", ui_str(UIS_NATIVE_3D_FAILED), NULL, 0);
    toast("toast-zip", ui_str(UIS_NATIVE_REPORT_ZIP_DONE),
          "/home/steamos/Games/MoonlightXRNative/reports/mlxr-report-20261030-034937.zip", 1);
    snprintf(a, sizeof a, ui_str(UIS_NATIVE_RECONNECTING), 2, 5);
    toast("toast-reconnecting", a, NULL, 0);
    // the clock strip, the lists
    st = add("clock", UI_PANEL_CLOCK, &s);
    st->kind = 5;
    st->clock = (UiClock){ "10:42", 54, 1, 1 };
    for (int m = 0; m < 2; m++) {
        st = add(m ? "pc-list-empty" : "pc-list", UI_PANEL_PC_LIST, &s);
        st->kind = 3;
        st->list_mode = m;
    }
    static const char* const APPM[4] = { "app-list", "app-list-loading", "app-list-failed", "app-list-empty" };
    for (int m = 0; m < 4; m++) {
        st = add(APPM[m], UI_PANEL_APP_LIST, &s);
        st->kind = 4;
        st->list_mode = m;
    }
}

// ---- drawing and checking --------------------------------------------------------------------------

static UiHits hits;

static void draw_state(const State* st, UiCanvas* cv) {
    ui_hits_clear(&hits);
    switch (st->kind) {
    case 1: ui_draw_toast(cv, st->toast, st->more[0] ? st->more : NULL, st->more_is_path); break;
    case 2: ui_draw_report(cv, &st->report, &hits); break;
    case 3: {
        static UiPcItem pcs[3];
        const char* names[3] = { "GAMING-PC", "Living room PC", "steamdeck" };
        for (int i = 0; i < 3; i++) {
            snprintf(pcs[i].name, sizeof pcs[i].name, "%s", names[i]);
            pcs[i].state = i == 2 ? UI_PC_REFRESHING : (i == 1 ? UI_PC_OFFLINE : UI_PC_ONLINE);
            pcs[i].paired = i != 1;
            pcs[i].game_running = i == 0;
        }
        UiPcList l = { pcs, st->list_mode ? 0 : 3, 0, -1, { UI_HIT_NONE, 0, 0 } };
        ui_draw_pc_list(cv, &l, &hits);
        break;
    }
    case 4: {
        static UiAppItem apps[3];
        const char* an[3] = { "Desktop", "Steam Big Picture", "Portal 2" };
        for (int i = 0; i < 3; i++) {
            snprintf(apps[i].name, sizeof apps[i].name, "%s", an[i]);
            apps[i].running = i == 0;
        }
        UiAppList l = { "GAMING-PC", apps, st->list_mode == 0 ? 3 : 0, 0, -1, st->list_mode == 1, st->list_mode == 2,
                        { UI_HIT_NONE, 0, 0 } };
        ui_draw_app_list(cv, &l, &hits);
        break;
    }
    case 5: ui_draw_clock_strip(cv, &st->clock); break;
    default: {
        UiView v = st->v;
        v.settings = &st->s;
        ui_draw_panel(st->id, cv, &v, &hits);
    }
    }
}

// Containers: what a text may sit in. Track parts are joined into one zone per row.
static int container_kind(int k) {
    return k == UI_HIT_TAB || k == UI_HIT_CELL || k == UI_HIT_BUTTON || k == UI_HIT_KEY || k == UI_HIT_ENV_TILE ||
           k == UI_HIT_BAR_BUTTON || k == UI_HIT_LIST_ITEM || k == UI_HIT_HOST_CELL || k == UI_HIT_TRACK_DOWN ||
           k == UI_HIT_TRACK_RUN || k == UI_HIT_TRACK_UP;
}
static int is_track(int k) { return k == UI_HIT_TRACK_DOWN || k == UI_HIT_TRACK_RUN || k == UI_HIT_TRACK_UP; }

typedef struct { float x0, y0, x1, y1; } Box;

static Box hit_box(const UiHit* h, const UiCanvas* cv) {
    Box b = { h->u0 * (float)cv->w, h->v0 * (float)cv->h, h->u1 * (float)cv->w, h->v1 * (float)cv->h };
    if (is_track(h->kind) || !h->enabled) {
        // the whole row's track (its three parts), and a greyed row's cells, which are not drawn:
        // the row's reason spans them
        for (int i = 0; i < hits.count; i++) {
            const UiHit* o = &hits.hit[i];
            int same = is_track(h->kind) ? is_track(o->kind) : o->kind == h->kind && !o->enabled;
            if (!same || o->a != h->a || o->v0 != h->v0) continue;
            if (o->u0 * (float)cv->w < b.x0) b.x0 = o->u0 * (float)cv->w;
            if (o->u1 * (float)cv->w > b.x1) b.x1 = o->u1 * (float)cv->w;
        }
    }
    return b;
}

static float overlap(float a0, float a1, float b0, float b1) {
    float lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi - lo;
}

// Is s a string of the table, in the language or English (a cut one would be "clipped")
static int table_string(const char* s) {
    for (int id = 0; id < UIS_COUNT; id++) {
        if (strcmp(s, ui_str_lang(g_lang, (UiStr)id)) == 0 || strcmp(s, ui_str_lang(0, (UiStr)id)) == 0) return 1;
    }
    return 0;
}

static void problem(const State* st, const char* what, const char* text) {
    problems_lang++;
    checksFailed++;
    fprintf(stderr, "%s %s: %s: \"%s\"\n", ui_lang_code(g_lang), st->name, what, text);
}

static void check_state(const State* st, const UiCanvas* cv) {
    const float TOL = 1.0f;
    // a dialog over the panel: the texts under it are covered, so overlaps count within a layer
    int layer_from = nev;
    for (int i = 0; st->modal[0] && i < nev; i++) {
        if (strcmp(evs[i].text, st->modal) == 0) { layer_from = i; break; }
    }
    for (int i = 0; i < nev; i++) {
        const UiTextTraceEvent* e = &evs[i].e;
        checksRun++;
        if (e->kind == UI_TEXT_TRACE_CUT) {
            if (table_string(e->text)) problem(st, "cut with an ellipsis", e->text);
            continue;
        }
        if (e->missing) problem(st, "glyph missing", e->text);
        if (e->ink_x0 > e->ink_x1) continue;   // nothing inked (spaces)
        Box ink = { e->ink_x0, e->ink_y0, e->ink_x1, e->ink_y1 };
        if (ink.x0 < -TOL || ink.y0 < -TOL || ink.x1 > (float)cv->w + TOL || ink.y1 > (float)cv->h + TOL) {
            problem(st, "off the panel", e->text);
            continue;
        }
        // the smallest container its middle is in
        float cx = (ink.x0 + ink.x1) * 0.5f, cy = (ink.y0 + ink.y1) * 0.5f;
        int best = -1;
        float best_area = 1e30f;
        for (int h = 0; h < hits.count; h++) {
            if (!container_kind(hits.hit[h].kind)) continue;
            Box b = hit_box(&hits.hit[h], cv);
            if (cx < b.x0 || cx > b.x1 || cy < b.y0 || cy > b.y1) continue;
            float area = (b.x1 - b.x0) * (b.y1 - b.y0);
            if (area < best_area) { best_area = area; best = h; }
        }
        if (i >= layer_from) {
            // over the dialog: its own buttons are the last hits; the cells under it do not count
            best = -1;
            best_area = 1e30f;
            for (int h = 0; h < hits.count; h++) {
                if (hits.hit[h].kind != UI_HIT_BUTTON) continue;
                Box b = hit_box(&hits.hit[h], cv);
                if (cx < b.x0 || cx > b.x1 || cy < b.y0 || cy > b.y1) continue;
                float area = (b.x1 - b.x0) * (b.y1 - b.y0);
                if (area < best_area) { best_area = area; best = h; }
            }
            if (best >= 0) {
                Box b = hit_box(&hits.hit[best], cv);
                if (ink.x0 < b.x0 - TOL || ink.x1 > b.x1 + TOL) problem(st, "wider than its button", e->text);
            }
        } else if (best >= 0) {
            Box b = hit_box(&hits.hit[best], cv);
            if (ink.x0 < b.x0 - TOL || ink.x1 > b.x1 + TOL) problem(st, "wider than its cell", e->text);
        } else {
            for (int h = 0; h < hits.count; h++) {
                if (!container_kind(hits.hit[h].kind)) continue;
                Box b = hit_box(&hits.hit[h], cv);
                if (overlap(ink.x0, ink.x1, b.x0, b.x1) > TOL && overlap(ink.y0, ink.y1, b.y0, b.y1) > TOL) {
                    problem(st, "runs into a cell it is not in", e->text);
                    break;
                }
            }
        }
        // clear of the other texts
        for (int j = i >= layer_from ? layer_from : 0; j < i; j++) {
            const UiTextTraceEvent* o = &evs[j].e;
            if (o->kind != UI_TEXT_TRACE_DRAW || o->ink_x0 > o->ink_x1) continue;
            if (overlap(ink.x0, ink.x1, o->ink_x0, o->ink_x1) > TOL && overlap(ink.y0, ink.y1, o->ink_y0, o->ink_y1) > TOL) {
                char both[820];
                snprintf(both, sizeof both, "%s\" over \"%s", e->text, evs[j].text);
                problem(st, "overlaps another text", both);
                break;
            }
        }
    }
}

static int mkdir_p(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char* c = tmp + 1; *c; c++) {
        if (*c == '/') { *c = 0; mkdir(tmp, 0755); *c = '/'; }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static void save_png(const char* dir, const char* name, const UiCanvas* cv) {
    stbi_write_png_compression_level = 9;
    uint8_t* flat = malloc((size_t)cv->w * cv->h * 3);
    if (!flat) return;
    for (int y = 0; y < cv->h; y++) {
        for (int x = 0; x < cv->w; x++) {
            const uint8_t* p = cv->px + (size_t)y * cv->stride + (size_t)x * 4;
            int g = (((x >> 4) + (y >> 4)) & 1) ? 96 : 120, inv = 255 - p[3];
            for (int c = 0; c < 3; c++) {
                int v = p[c] + g * inv / 255;
                flat[((size_t)y * cv->w + x) * 3 + c] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    stbi_write_png(path, cv->w, cv->h, 3, flat, cv->w * 3);
    free(flat);
}

// The English panels' pixels, by state: a language's PNG is written only where it differs (a
// panel with nothing translated in it is the English one), which keeps the artefact small
static uint64_t english_hash[MAX_STATES];
static int english_done;

static uint64_t pixels_hash(const UiCanvas* cv) {
    uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < cv->h; y++) {
        const uint8_t* row = cv->px + (size_t)y * cv->stride;
        for (int x = 0; x < cv->w * 4; x++) h = (h ^ row[x]) * 1099511628211ull;
    }
    return h;
}

// Which languages are written as PNGs: every one is drawn and checked, but the pictures of a
// few with the longest words and the other scripts say what the rest look like (CI uploads them).
// MLXR_I18N_PNGS=all writes every language.
static int write_pngs(int lang) {
    const char* env = getenv("MLXR_I18N_PNGS");
    if (env && strcmp(env, "all") == 0) return 1;
    static const char* const SOME[] = { "de", "fr", "pl", "ru", "ja", "ko", "zh-CN", "zh-TW" };
    for (size_t i = 0; i < sizeof SOME / sizeof SOME[0]; i++) {
        if (strcmp(ui_lang_code(lang), SOME[i]) == 0) return 1;
    }
    return 0;
}

static void run_language(int lang) {
    g_lang = lang;
    problems_lang = 0;
    build_states();
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/%s", g_out, ui_lang_code(lang));
    if (g_out && write_pngs(lang)) mkdir_p(dir);
    for (int i = 0; i < nstates; i++) {
        const State* st = &states[i];
        int w, h;
        ui_panel_native_size(st->id, &w, &h);
        uint8_t* buf = calloc((size_t)w * h, 4);
        UiCanvas cv;
        ui_cv_init(&cv, buf, w, h);
        nev = 0;
        ui_text_set_trace(on_text, NULL);
        draw_state(st, &cv);
        ui_text_set_trace(NULL, NULL);
        check_state(st, &cv);
        uint64_t hash = pixels_hash(&cv);
        if (lang == 0) english_hash[i] = hash;
        if (g_out && write_pngs(lang) && (lang == 0 || !english_done || hash != english_hash[i])) save_png(dir, st->name, &cv);
        free(buf);
    }
    if (lang == 0) english_done = 1;
    printf("ui_i18n_panels: %-6s %d panels, %d problem(s)\n", ui_lang_code(lang), nstates, problems_lang);
}

int main(int argc, char** argv) {
    g_out = argc > 1 ? argv[1] : NULL;
    UiFont* font = ui_font_default();
    CHECK(font != NULL);
    if (!font) return checksDone("ui_i18n_panels");
    const char* only = getenv("MLXR_I18N_ONLY");   // one language, for a person looking at it
    int skipped = 0;
    for (int lang = 0; lang < ui_lang_count(); lang++) {
        if (only && *only && strcmp(only, ui_lang_code(lang)) != 0) continue;
        char msg[768];
        int got = ui_lang_start(lang, msg, sizeof msg);
        if (got != lang) {
            // only Chinese, Japanese and Korean may miss their font here; the rest is bundled
            CHECK(ui_lang_script(lang) >= UI_SCRIPT_CJK_JA);
            printf("ui_i18n_panels: %-6s skipped: %s\n", ui_lang_code(lang), msg);
            skipped++;
            continue;
        }
        run_language(lang);
        ui_font_clear_fallback(font);
    }
    ui_lang_start(0, NULL, 0);
    printf("ui_i18n_panels: %d language(s) skipped for want of a font\n", skipped);
    ui_panels_shutdown();
    return checksDone("ui_i18n_panels");
}
