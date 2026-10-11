// mlxr-panel-test: renders every panel and tab to PNGs in build-ui/panels/ so a person can look at them.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "panels.h"
#include "panel_render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int written;

static int mkdir_p(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char* c = tmp + 1; *c; c++) {
        if (*c == '/') { *c = 0; mkdir(tmp, 0755); *c = '/'; }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

// Panels are transparent where nothing is drawn; composite over mid grey so the corners are visible
static void save(const char* dir, const char* name, const UiCanvas* cv, int scale_note) {
    (void)scale_note;
    uint8_t* flat = malloc((size_t)cv->w * cv->h * 3);
    if (!flat) return;
    for (int y = 0; y < cv->h; y++) {
        for (int x = 0; x < cv->w; x++) {
            const uint8_t* p = cv->px + (size_t)y * cv->stride + (size_t)x * 4;
            // checker backdrop in two greys, premultiplied source over it
            int g = (((x >> 4) + (y >> 4)) & 1) ? 96 : 120;
            int inv = 255 - p[3];
            for (int c = 0; c < 3; c++) {
                int v = p[c] + g * inv / 255;
                flat[((size_t)y * cv->w + x) * 3 + c] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if (stbi_write_png(path, cv->w, cv->h, 3, flat, cv->w * 3)) written++;
    free(flat);
}

static UiCanvas make(uint8_t** buf, UiPanelId id, float scale) {
    int w, h;
    ui_panel_native_size(id, &w, &h);
    w = (int)(w * scale + 0.5f);
    h = (int)(h * scale + 0.5f);
    *buf = calloc((size_t)w * h, 4);
    UiCanvas cv;
    ui_cv_init(&cv, *buf, w, h);
    return cv;
}

static void panel_to_png(const char* dir, const char* name, UiPanelId id, const UiView* v, float scale) {
    uint8_t* buf;
    UiCanvas cv = make(&buf, id, scale);
    static UiHits hits;
    ui_draw_panel(id, &cv, v, &hits);
    save(dir, name, &cv, 0);
    free(buf);
}

int render_all_panels(const char* dir) {
    if (mkdir_p(dir) != 0) return -1;
    written = 0;
    Settings s;
    settings_defaults(&s);
    UiView v;
    ui_view_init(&v, &s);
    char name[96];

    static const char* const TABS[COG_TAB_COUNT] = { "screen", "display", "3d", "picture", "about" };
    v.version = "0.1.0";
    v.commit = "b7a431a";
    v.log_path = "/home/steamos/Games/MoonlightXRNative/moonlight.log";
    v.curve_ok = 0;   // the Frame has no cylinder layer
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        v.cog_tab = tab;
        snprintf(name, sizeof name, "cog_%s", TABS[tab]);
        panel_to_png(dir, name, UI_PANEL_COG, &v, 1.0f);
    }
    // head locked: aim rows live
    Settings locked = s;
    locked.head_locked = 1;
    locked.head_aim = 1;
    locked.picture_brightness = 12;
    locked.picture_gamma = 130;
    locked.screen_distance_cm = 450;
    locked.screen_tilt_deg = -12;
    locked.separation = 9;
    UiView vl;
    ui_view_init(&vl, &locked);
    vl.curve_ok = 1;
    vl.version = v.version;
    vl.commit = v.commit;
    vl.log_path = NULL;
    for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
        vl.cog_tab = tab;
        snprintf(name, sizeof name, "cog_%s_locked", TABS[tab]);
        panel_to_png(dir, name, UI_PANEL_COG, &vl, 1.0f);
    }
    // rooms
    static const int ENVS[3] = { ENV_ID_HOME_THEATER, ENV_ID_GRAND_CINEMA, ENV_ID_SYNTHWAVE };
    static const char* const ENVN[3] = { "theater", "grand_cinema", "synthwave" };
    for (int r = 0; r < 3; r++) {
        UiView vr = v;
        vr.room_env = ENVS[r];
        for (int tab = 0; tab < COG_TAB_COUNT; tab++) {
            if (r != 0 && tab > COG_TAB_DISPLAY) continue;
            vr.cog_tab = tab;
            snprintf(name, sizeof name, "cog_room_%s_%s", ENVN[r], tab == 0 ? "room" : TABS[tab]);
            panel_to_png(dir, name, UI_PANEL_COG, &vr, 1.0f);
        }
    }
    // 3D off
    {
        Settings off = s;
        off.depth_source = 2;
        UiView vo;
        ui_view_init(&vo, &off);
        vo.cog_tab = COG_TAB_3D;
        panel_to_png(dir, "cog_3d_off", UI_PANEL_COG, &vo, 1.0f);
    }
    // hover on a cell and a thumb
    {
        UiView vh = v;
        vh.cog_tab = COG_TAB_DISPLAY;
        vh.hover = (UiHover){ UI_HIT_CELL, COG_OPTION_STATS, 1 };
        panel_to_png(dir, "cog_display_hover", UI_PANEL_COG, &vh, 1.0f);
    }

    // picker
    v.env_selected = ENV_CELL_HOME_THEATER;
    v.hover = (UiHover){ UI_HIT_ENV_TILE, ENV_CELL_VOID, 0 };
    panel_to_png(dir, "picker", UI_PANEL_PICKER, &v, 1.0f);
    v.hover.kind = UI_HIT_NONE;

    // keyboard
    static const char* const SHEET[KB_STATE_COUNT] = { "lower", "upper", "symbols", "fn" };
    for (int sh = 0; sh < KB_STATE_COUNT; sh++) {
        v.kb_sheet = sh;
        v.kb_mods = 0;
        snprintf(name, sizeof name, "keyboard_%s", SHEET[sh]);
        panel_to_png(dir, name, UI_PANEL_KEYBOARD, &v, 1.0f);
    }
    v.kb_sheet = KB_STATE_LOWER;
    v.kb_mods = KB_MOD_CTRL | KB_MOD_ALT;
    panel_to_png(dir, "keyboard_lower_ctrl_alt", UI_PANEL_KEYBOARD, &v, 1.0f);
    v.kb_sheet = KB_STATE_FN;
    v.kb_mods = KB_MOD_CTRL | KB_MOD_WIN;
    panel_to_png(dir, "keyboard_fn_ctrl_win", UI_PANEL_KEYBOARD, &v, 1.0f);
    v.kb_mods = 0;

    // bar and its button faces
    v.bar_show_aim = 1;
    v.bar_show_stereo = 1;
    v.session.stereo_on = 1;
    v.session.ray = 1;
    panel_to_png(dir, "bar_all_on", UI_PANEL_BAR, &v, 1.0f);
    v.bar_show_aim = 0;
    v.session.stereo_on = 0;
    v.session.ray = 0;
    v.session.pad_mode = 1;
    v.hover = (UiHover){ UI_HIT_BAR_BUTTON, BAR_SLOT_COG, 0 };
    panel_to_png(dir, "bar_states", UI_PANEL_BAR, &v, 1.0f);
    v.hover.kind = UI_HIT_NONE;
    panel_to_png(dir, "button_atlas", UI_PANEL_BUTTON_ATLAS, &v, 1.0f);

    // prompts
    panel_to_png(dir, "exit", UI_PANEL_EXIT, &v, 1.0f);
    v.hover = (UiHover){ UI_HIT_BUTTON, UI_BTN_EXIT_CONFIRM, 0 };
    panel_to_png(dir, "exit_hot", UI_PANEL_EXIT, &v, 1.0f);
    v.hover.kind = UI_HIT_NONE;
    panel_to_png(dir, "hint", UI_PANEL_HINT, &v, 1.0f);
    panel_to_png(dir, "kofi", UI_PANEL_KOFI, &v, 1.0f);

    // report
    {
        uint8_t* buf;
        UiCanvas cv = make(&buf, UI_PANEL_REPORT, 1.0f);
        UiReport r = { "", REPORT_ZONE_NOTE, REPORT_ZONE_NONE, 0 };
        ui_draw_report(&cv, &r, NULL);
        save(dir, "report_empty", &cv, 0);
        ui_cv_clear(&cv);
        UiReport r2 = { "The picture froze for a second when I opened the keyboard, then the ray stopped following my right hand until I put the controller down and picked it up again. It happened three times in ten minutes.",
                        REPORT_ZONE_NOTE, REPORT_ZONE_SEND, 1 };
        ui_draw_report(&cv, &r2, NULL);
        save(dir, "report_filled", &cv, 0);
        free(buf);
    }
    // toast
    {
        uint8_t* buf;
        UiCanvas cv = make(&buf, UI_PANEL_TOAST, 1.0f);
        ui_draw_toast(&cv, "3D on", NULL, 0);
        save(dir, "toast_one", &cv, 0);
        ui_cv_clear(&cv);
        ui_draw_toast(&cv, "Gamepad mode", ui_str(UIS_TOAST_BACK_MENU_GRIP), 0);
        save(dir, "toast_two", &cv, 0);
        ui_cv_clear(&cv);
        ui_draw_toast(&cv, "Report saved", "/home/steamos/Games/MoonlightXRNative/reports/moonlight-report-2026-10-10-123456.txt", 1);
        save(dir, "toast_path", &cv, 0);
        free(buf);
        cv = make(&buf, UI_PANEL_CLOCK, 1.0f);
        ui_draw_clock(&cv, "10:42   Battery 87%");
        save(dir, "clock", &cv, 0);
        free(buf);
    }
    // lists
    {
        static UiPcItem pcs[7];
        const char* names[7] = { "GAMING-PC", "Living room PC", "steamdeck", "A very long computer name that will not fit in the row at all, honestly",
                                 "server", "laptop", "htpc" };
        for (int i = 0; i < 7; i++) {
            snprintf(pcs[i].name, sizeof pcs[i].name, "%s", names[i]);
            pcs[i].state = i % 3 == 2 ? UI_PC_REFRESHING : (i % 3 == 1 ? UI_PC_OFFLINE : UI_PC_ONLINE);
            pcs[i].paired = i % 2 == 0;
            pcs[i].game_running = i == 0;
        }
        uint8_t* buf;
        UiCanvas cv = make(&buf, UI_PANEL_PC_LIST, 1.0f);
        UiPcList l = { pcs, 7, 0, 1, { UI_HIT_LIST_ITEM, 2, 0 } };
        ui_draw_pc_list(&cv, &l, NULL);
        save(dir, "pc_list", &cv, 0);
        ui_cv_clear(&cv);
        UiPcList l2 = { pcs, 7, 1, -1, { UI_HIT_NONE, 0, 0 } };
        ui_draw_pc_list(&cv, &l2, NULL);
        save(dir, "pc_list_page2", &cv, 0);
        ui_cv_clear(&cv);
        UiPcList l3 = { pcs, 0, 0, -1, { UI_HIT_NONE, 0, 0 } };
        ui_draw_pc_list(&cv, &l3, NULL);
        save(dir, "pc_list_empty", &cv, 0);
        ui_cv_clear(&cv);
        static UiAppItem apps[6];
        const char* an[6] = { "Desktop", "Steam Big Picture", "Cyberpunk 2077", "Halo: The Master Chief Collection", "Portal 2", "Low Res Screen" };
        for (int i = 0; i < 6; i++) { snprintf(apps[i].name, sizeof apps[i].name, "%s", an[i]); apps[i].running = i == 2; }
        UiAppList a = { "GAMING-PC", apps, 6, 0, 0, 0, 0, { UI_HIT_BUTTON, UI_BTN_LIST_BACK, 0 } };
        ui_draw_app_list(&cv, &a, NULL);
        save(dir, "app_list", &cv, 0);
        ui_cv_clear(&cv);
        UiAppList a2 = { "GAMING-PC", apps, 0, 0, -1, 1, 0, { UI_HIT_NONE, 0, 0 } };
        ui_draw_app_list(&cv, &a2, NULL);
        save(dir, "app_list_loading", &cv, 0);
        free(buf);
    }
    return written;
}

#ifdef PANEL_RENDER_MAIN
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "panels";
    int n = render_all_panels(dir);
    printf("%d panels written to %s\n", n, dir);
    ui_panels_shutdown();
    return n > 0 ? 0 : 1;
}
#endif
