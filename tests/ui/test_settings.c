// Settings: defaults identical to upstream, INI round trip, defaults written commented out,
// clamping on load, tolerant parsing, config directory handling.
#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "panels.h"
#include "settings.h"
#include "ui_strings.h"
#include "xr_cards.h"
#include "xr_lightgun.h"
#include "xr_padgyro.h"
#include "xr_shared.h"

static int geti(const Settings* s, const char* name) {
    const SettingDesc* d = settings_find(name);
    if (!d) { fprintf(stderr, "no setting %s\n", name); checksFailed++; return -999; }
    return settings_get(s, d);
}

static const char* gets(const Settings* s, const char* name) {
    const SettingDesc* d = settings_find(name);
    return d ? settings_get_str(s, d) : "<missing>";
}

static void test_defaults(void) {
    Settings s;
    settings_defaults(&s);
    // stream (PreferenceConfiguration DEFAULT_RESOLUTION 2560x1440, DEFAULT_FPS 90, auto bitrate, stereo)
    CHECK(strcmp(gets(&s, "stream.host"), "") == 0);
    CHECK(strcmp(gets(&s, "stream.app"), "") == 0);
    CHECK(strcmp(SETTINGS_RESOLUTIONS[s.resolution], "2560x1440") == 0);
    CHECK(settings_fps_value(&s) == 90);
    CHECK(geti(&s, "bitrate_kbps") == 0);
    CHECK(geti(&s, "codec") == 0 && geti(&s, "audio_config") == 0);
    CHECK(geti(&s, "virtual_surround") == 0);
    CHECK(geti(&s, "network_path") == 0);   // auto
    // VR Settings table of the fork README
    CHECK(geti(&s, "pointer") == 1);
    CHECK(geti(&s, "pointer_sleep") == 1);
    CHECK(geti(&s, "show_ray") == 1);
    CHECK(geti(&s, "controller_model") == 0);
    CHECK(geti(&s, "head_aim") == 0);
    CHECK(geti(&s, "head_aim_sensitivity") == 8);
    CHECK(geti(&s, "head_aim_deadzone") == 2);
    CHECK(geti(&s, "gamepad_toggle") == PAD_SHORTCUT_MENU_GRIP);
    CHECK(geti(&s, "rumble_mix") == PAD_RUMBLE_MIX_BOTH && geti(&s, "rumble_intensity") == 100);
    CHECK(geti(&s, "pad_grip") == PAD_GRIP_BUMPERS);   // native: the grips are bumpers, as ever
    CHECK(geti(&s, "click_sound") == 1);
    CHECK(geti(&s, "notices") == NOTICES_ALL);   // native: every toast, as before the setting
    CHECK(geti(&s, "gaze") == 0 && geti(&s, "hand_tracking") == 0);   // native defaults (M4): off on the Frame until checked
    CHECK(geti(&s, "passthrough") == 0);
    CHECK(settings_environment_id(&s) == ENV_ID_VOID);   // black void unless passthrough is on
    s.passthrough = 1;
    CHECK(settings_environment_id(&s) == ENV_ID_PASSTHROUGH);
    s.passthrough = 0;
    CHECK(geti(&s, "depth_source") == 0);               // ZipDepth
    CHECK(geti(&s, "env_res") == ENV_RES_STANDARD);
    CHECK(geti(&s, "picture_brightness") == 0 && geti(&s, "picture_contrast") == 100);
    CHECK(geti(&s, "picture_gamma") == 100 && geti(&s, "picture_saturation") == 100);
    // VR Debugging
    CHECK(geti(&s, "depth_rate") == DEPTH_RATE_DEFAULT && DEPTH_RATE_DEFAULT == 20);
    CHECK(geti(&s, "depth_debug") == 0 && geti(&s, "eye_swap") == 0);
    CHECK(geti(&s, "file_log") == FILE_LOG_BASIC);
    // the panel's rows
    CHECK(geti(&s, "head_locked") == 0);
    CHECK(geti(&s, "sharpening") == 0);      // native: off (upstream quality)
    CHECK(geti(&s, "supersampling") == 1);   // native: normal (upstream off)
    CHECK(geti(&s, "stats") == 0);
    CHECK(geti(&s, "glow") == 0 && geti(&s, "glow_level") == 50 && geti(&s, "screen_light") == 1);
    CHECK(geti(&s, "separation") == -1 && settings_separation_units(&s) == 6);   // ZipDepth's own
    CHECK(geti(&s, "convergence") == -1 && settings_convergence_percent(&s) == 50);
    s.depth_source = 1;
    CHECK(settings_separation_units(&s) == 5);   // MiDaS
    s.depth_source = 0;
    // where the picture hangs: 3 m away, 3 m wide, level
    CHECK(geti(&s, "distance_cm") == 300 && geti(&s, "width_cm") == 300);
    CHECK(geti(&s, "height_cm") == 0 && geti(&s, "tilt_deg") == 0 && geti(&s, "rotate_deg") == 0);
    CHECK(geti(&s, "curve") == 0);
    // rooms: XrShared's ROOM_ constants
    CHECK(geti(&s, "room_4_brightness") == ROOM_THEATER_BRIGHTNESS && geti(&s, "room_4_screen") == ROOM_THEATER_SCREEN);
    CHECK(geti(&s, "room_5_brightness") == ROOM_GRAND_CINEMA_BRIGHTNESS && geti(&s, "room_5_screen") == 100);
    CHECK(geti(&s, "room_6_brightness") == ROOM_SYNTHWAVE_BRIGHTNESS && geti(&s, "room_6_screen") == 100);
    CHECK(geti(&s, "room_4_glow") == 1 && geti(&s, "room_5_glow") == 0 && geti(&s, "room_6_glow") == 1);
    CHECK(geti(&s, "room_4_light") == ROOM_LIGHT_DEFAULT && geti(&s, "room_5_light") == 150 && geti(&s, "room_6_light") == 150);
    CHECK(settings_room_index(ENV_ID_HOME_THEATER) == 0 && settings_room_index(ENV_ID_SYNTHWAVE) == 2);
    CHECK(settings_room_index(ENV_ID_VOID) == -1 && settings_room_index(ENV_ID_PASSTHROUGH) == -1);
    CHECK(settings_room_resizable(ENV_ID_HOME_THEATER) && !settings_room_resizable(ENV_ID_GRAND_CINEMA));
    // derived stream values: Moonlight's bitrate curve
    CHECK(settings_bitrate_kbps(&s) == 80000);                       // 1440p90: owner's 80 Mbps (de55376)
    CHECK(settings_default_bitrate_kbps(2560, 1440, 60) == 80000);
    CHECK(settings_default_bitrate_kbps(2560, 1440, 120) != 80000);   // outside the owner's range: the curve
    // Native (lane refresh): above 90 at 1440p, the flat launcher's rule (twice the curve)
    CHECK(settings_default_bitrate_kbps(2560, 1440, 120) == 113000);
    CHECK(settings_default_bitrate_kbps(2560, 1440, 144) == 124000);
    CHECK(settings_default_bitrate_kbps(1920, 1080, 60) == 20000);
    CHECK(settings_default_bitrate_kbps(3840, 2160, 60) == 80000);
    CHECK(settings_default_bitrate_kbps(1280, 720, 30) == 5000);
    CHECK(settings_default_bitrate_kbps(640, 360, 30) == 1000);
    int w, h;
    settings_resolution(&s, &w, &h);
    CHECK(w == 2560 && h == 1440);
    s.bitrate_kbps = 12345;
    CHECK(settings_bitrate_kbps(&s) == 12345);
    // every default is inside its own range and the table has what the README lists
    int n;
    const SettingDesc* t = settings_table(&n);
    CHECK(n >= 60);
    Settings d2;
    settings_defaults(&d2);
    for (int i = 0; i < n; i++) {
        if (t[i].type == SET_STR) continue;
        int v = settings_get(&d2, &t[i]);
        CHECK(v >= t[i].min && v <= t[i].max);
        CHECK(settings_is_default(&d2, &t[i]));
        CHECK(t[i].doc && t[i].doc[0]);
        for (int j = i + 1; j < n; j++) CHECK(strcmp(t[i].key, t[j].key) != 0);   // unique keys
        if (t[i].type == SET_ENUM) {
            CHECK(t[i].nnames == t[i].max + 1);
            for (int a = 0; a < t[i].nnames; a++)
                for (int b = a + 1; b < t[i].nnames; b++) CHECK(strcmp(t[i].names[a], t[i].names[b]) != 0);
        }
    }
    CHECK(settings_find("nope") == NULL && settings_find("vr.nope") == NULL);
    CHECK(settings_find("VR.POINTER") != NULL);
    settings_clamp(&d2);
    Settings d3;
    settings_defaults(&d3);
    CHECK(settings_equal(&d2, &d3));
}

static const char* const README_KEYS[] = {
    // VR Settings
    "pointer", "pointer_sleep", "show_ray", "controller_model", "head_aim",
    "head_aim_sensitivity", "head_aim_deadzone", "gamepad_toggle", "pad_grip", "rumble_mix", "rumble_intensity", "click_sound", "gaze",
    "notices", "click_steadiness", "scroll_speed", "lightgun", "lightgun_reload", "lightgun_recoil", "pad_gyro", "pad_gyro_ratchet", "hand_tracking", "passthrough", "environment", "depth_source", "env_res",
    "picture_brightness", "picture_contrast", "picture_gamma", "picture_saturation",
    // VR Debugging
    "depth_rate", "depth_debug", "eye_swap", "file_log",
    // elsewhere
    "virtual_surround", "network_path",
    // stream
    "host", "app", "resolution", "fps", "bitrate_kbps", "codec", "audio_config", "frame_pacing",
    // the panel: Screen, Display, 3D, Room
    "distance_cm", "height_cm", "tilt_deg", "rotate_deg", "curve", "width_cm", "azimuth_deg",
    "head_locked", "sharpening", "supersampling", "stats", "stats_style", "glow", "glow_level", "screen_light",
    "separation", "convergence", "stereo_source", "sbs_full",
    "room_4_brightness", "room_4_glow", "room_4_light", "room_4_screen",
    "room_5_brightness", "room_5_glow", "room_5_light", "room_5_screen",
    "room_6_brightness", "room_6_glow", "room_6_light", "room_6_screen",
};

static void test_table_covers_readme(void) {
    for (size_t i = 0; i < sizeof README_KEYS / sizeof README_KEYS[0]; i++) {
        CHECK(settings_find(README_KEYS[i]) != NULL);
    }
}

static void make_nondefault(Settings* s, unsigned seed) {
    int n;
    const SettingDesc* t = settings_table(&n);
    srand(seed);
    for (int i = 0; i < n; i++) {
        const SettingDesc* d = &t[i];
        if (d->type == SET_STR) {
            char buf[40];
            snprintf(buf, sizeof buf, "value %d / \"q\" #h %u", i, seed);
            settings_set_str(s, d, buf);
        } else {
            int span = d->max - d->min + 1;
            int v = d->min + (span > 1 ? rand() % span : 0);
            settings_set(s, d, v);
        }
    }
    settings_clamp(s);
}

static void test_round_trip(void) {
    // every setting moved off its default, one at a time
    int n;
    const SettingDesc* t = settings_table(&n);
    for (int i = 0; i < n; i++) {
        Settings s, back;
        settings_defaults(&s);
        settings_defaults(&back);
        const SettingDesc* d = &t[i];
        if (d->type == SET_STR) {
            settings_set_str(&s, d, "other");
        } else {
            if (strncmp(d->key, "room_", 5) == 0 && strstr(d->key, "_screen") && !settings_room_resizable(atoi(d->key + 5))) {
                continue;   // a room that keeps its picture whole has no other size
            }
            // the first value after the default that survives the clamp (glow level moves by 5, bitrate starts at 500)
            int def = settings_get(&s, d);
            for (int v = def + 1; v <= d->max + 1; v++) {
                settings_set(&s, d, v > d->max ? d->min : v);
                settings_clamp(&s);
                if (!settings_is_default(&s, d)) break;
            }
        }
        settings_clamp(&s);
        CHECK(!settings_is_default(&s, d));
        char* text = settings_to_text(&s);
        CHECK(text != NULL);
        SettingsLoadInfo info;
        settings_from_text(&back, text, &info);
        CHECK(settings_equal(&s, &back));
        CHECK(info.applied == 1 && info.unknown == 0 && info.bad == 0 && info.clamped == 0);
        // exactly one active line, and it is this one
        int active = 0;
        for (const char* p = text; *p;) {
            const char* e = strchr(p, '\n');
            if (!e) e = p + strlen(p);
            if (e > p && *p != '#' && *p != '[') {
                active++;
                CHECK(strncmp(p, d->key, strlen(d->key)) == 0);
            }
            p = *e ? e + 1 : e;
        }
        CHECK(active == 1);
        free(text);
    }
    // random combinations through the text form
    for (unsigned seed = 1; seed <= 60; seed++) {
        Settings s, back;
        settings_defaults(&s);
        settings_defaults(&back);
        make_nondefault(&s, seed);
        char* text = settings_to_text(&s);
        settings_from_text(&back, text, NULL);
        CHECK(settings_equal(&s, &back));
        // saving what was loaded gives the same text
        char* again = settings_to_text(&back);
        CHECK(strcmp(text, again) == 0);
        free(text);
        free(again);
    }
}

static void test_defaults_commented_out(void) {
    Settings s;
    settings_defaults(&s);
    char* text = settings_to_text(&s);
    // every setting is present, and none is active
    int n;
    const SettingDesc* t = settings_table(&n);
    for (int i = 0; i < n; i++) {
        char needle[160];
        snprintf(needle, sizeof needle, "\n#%s = ", t[i].key);
        CHECK(strstr(text, needle) != NULL);
        snprintf(needle, sizeof needle, "\n%s = ", t[i].key);
        CHECK(strstr(text, needle) == NULL);
    }
    CHECK(strstr(text, "[stream]") && strstr(text, "[vr]") && strstr(text, "[debug]") && strstr(text, "[display]") &&
          strstr(text, "[3d]") && strstr(text, "[screen]") && strstr(text, "[rooms]"));
    // an old file whose commented lines hold last version's defaults: loading it gives the NEW defaults
    char* old = strdup(text);
    char* p = strstr(old, "#fps = 90");
    CHECK(p != NULL);
    if (p) memcpy(p, "#fps = 60", 9);
    p = strstr(old, "#head_aim_sensitivity = 8");
    if (p) memcpy(p, "#head_aim_sensitivity = 3", 25);
    Settings loaded;
    settings_defaults(&loaded);
    settings_from_text(&loaded, old, NULL);
    CHECK(settings_fps_value(&loaded) == 90 && loaded.head_aim_sensitivity == 8);
    // ...while an active line, even one equal to the default, is the user's
    Settings user;
    settings_defaults(&user);
    settings_from_text(&user, "[stream]\nfps = 60\n", NULL);
    CHECK(settings_fps_value(&user) == 60);
    free(old);
    free(text);
}

static void test_clamping(void) {
    Settings s;
    settings_defaults(&s);
    SettingsLoadInfo info;
    settings_from_text(&s,
        "[vr]\nhead_aim_sensitivity = 999\nhead_aim_deadzone = -5\npicture_gamma = 10\npicture_brightness = 51\n"
        "[debug]\ndepth_rate = 100\n"
        "[rooms]\nroom_5_screen = 40\nroom_4_screen = 10\nroom_6_brightness = 1000\n"
        "[display]\nglow_level = 53\n"
        "[stream]\nbitrate_kbps = 100\nresolution = 9\nfps = 4\n"
        "[3d]\nseparation = 99\nconvergence = 500\n"
        "[screen]\ndistance_cm = 1\nwidth_cm = 99999\n", &info);
    CHECK(s.head_aim_sensitivity == HEAD_AIM_SENSITIVITY_MAX);
    CHECK(s.head_aim_deadzone == HEAD_AIM_DEADZONE_MIN);
    CHECK(s.picture_gamma == PICTURE_GAMMA_MIN);
    CHECK(s.picture_brightness == PICTURE_BRIGHTNESS_MAX);
    CHECK(s.depth_rate == DEPTH_RATE_MAX);
    CHECK(s.room[1].screen == 100);                      // the Grand Cinema keeps its picture whole
    CHECK(s.room[0].screen == ROOM_SCREEN_MIN);          // the theater's lane starts at a quarter
    CHECK(s.room[2].brightness == ROOM_BRIGHTNESS_MAX);
    CHECK(s.glow_level == 55);                           // five percent steps
    CHECK(s.bitrate_kbps == 500);                        // 500..500000 where not 0
    CHECK(s.resolution == 4);                            // an enum index out of range is unreadable, the default stays
    CHECK(s.fps == 4);                                   // 4 is a valid index (120)
    CHECK(s.separation == COG_SEP_STEPS && s.convergence == 100);
    CHECK(s.screen_distance_cm == 20 && s.screen_width_cm == 800);
    CHECK(info.bad == 1);       // resolution = 9
    CHECK(info.clamped >= 10);
    // a struct filled with garbage comes out clamped
    Settings g;
    memset(&g, 0x7F, sizeof g);
    settings_clamp(&g);
    int n;
    const SettingDesc* t = settings_table(&n);
    for (int i = 0; i < n; i++) {
        if (t[i].type == SET_STR) { CHECK(strlen(settings_get_str(&g, &t[i])) < t[i].scap); continue; }
        int v = settings_get(&g, &t[i]);
        CHECK(v >= t[i].min && v <= t[i].max);
    }
    memset(&g, 0x80, sizeof g);   // large negatives
    settings_clamp(&g);
    for (int i = 0; i < n; i++) {
        if (t[i].type == SET_STR) continue;
        int v = settings_get(&g, &t[i]);
        CHECK(v >= t[i].min && v <= t[i].max);
    }
    // enum by name or by index, bool spellings
    Settings e;
    settings_defaults(&e);
    settings_from_text(&e, "[stream]\ncodec = HEVC\naudio_config = 2\nresolution = 1920x1080\nvirtual_surround = Yes\n", NULL);
    CHECK(e.codec == 2 && e.audio_config == 2 && strcmp(SETTINGS_RESOLUTIONS[e.resolution], "1920x1080") == 0);
    CHECK(e.virtual_surround == 1);
}

// Keys removed because nothing read them: an old config.ini that still has
// them loads, the rest of the file applies, and they are not written again
// The config.ini bitrate follows the flat launcher's 500 Mbps ceiling (it stopped at 150)
static void test_bitrate_ceiling(void) {
    Settings s;
    settings_defaults(&s);
    settings_from_text(&s, "[stream]\nbitrate_kbps = 300000\n", NULL);
    CHECK(s.bitrate_kbps == 300000 && settings_bitrate_kbps(&s) == 300000);
    settings_from_text(&s, "[stream]\nbitrate_kbps = 500000\n", NULL);
    CHECK(settings_bitrate_kbps(&s) == 500000);
    SettingsLoadInfo info;
    settings_from_text(&s, "[stream]\nbitrate_kbps = 900000\n", &info);
    CHECK(s.bitrate_kbps == 500000 && info.clamped == 1);
    settings_from_text(&s, "[stream]\nbitrate_kbps = 100\n", NULL);
    CHECK(s.bitrate_kbps == 500);
    // the default curve is untouched
    CHECK(settings_default_bitrate_kbps(2560, 1440, 144) == 124000);
    CHECK(MLXR_BITRATE_MAX_KBPS == 500000);
}

static void test_removed_keys(void) {
    // Native (lane wave6-i18n): language is back as [ui] language (the headset UI's); the old
    // [stream] one still reads as unknown
    static const char* const GONE[] = { "auto_desktop", "check_updates", "vr_mode", "doff_grace" };
    for (size_t i = 0; i < sizeof GONE / sizeof GONE[0]; i++) {
        CHECK(settings_find(GONE[i]) == NULL);
    }
    CHECK(settings_find("stream.language") == NULL && settings_find("ui.language") != NULL);
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    settings_from_text(&s,
        "[stream]\nauto_desktop = true\ncheck_updates = false\nlanguage = fr\nfps = 72\n"
        "[vr]\nvr_mode = false\ndoff_grace = false\nshow_ray = false\n", &info);
    CHECK(info.unknown == 5 && info.bad == 0);
    CHECK(info.applied == 2 && settings_fps_value(&s) == 72 && s.show_ray == 0);
    char* text = settings_to_text(&s);
    CHECK(text != NULL);
    for (size_t i = 0; text && i < sizeof GONE / sizeof GONE[0]; i++) {
        CHECK(strstr(text, GONE[i]) == NULL);
    }
    free(text);
}

static void test_notices(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    settings_from_text(&s, "[vr]\nnotices = important\n", &info);
    CHECK(s.notices == NOTICES_IMPORTANT && info.applied == 1 && info.bad == 0);
    settings_from_text(&s, "[vr]\nnotices = Off\n", &info);
    CHECK(s.notices == NOTICES_OFF);
    settings_from_text(&s, "[vr]\nnotices = 0\n", &info);
    CHECK(s.notices == NOTICES_ALL);
    settings_from_text(&s, "[vr]\nnotices = loud\n", &info);   // not a value: the last good one stays
    CHECK(s.notices == NOTICES_ALL && info.bad == 1);
    s.notices = NOTICES_IMPORTANT;
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nnotices = important\n") != NULL);
    free(text);
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#notices = all\n") != NULL);
    free(text);
    // the Display tab's row reads it
    SessionToggles t;
    session_from_settings(&s, &t);
    s.notices = NOTICES_OFF;
    CHECK(settings_option_cell(&s, &t, COG_OPTION_NOTICES) == NOTICES_OFF);
}

// Show controllers is a kind: off, frame, generic, gun, custom. Files from before it was a switch
// say true and false (or 0 and 1) and keep their meaning: the Frame's controllers, or none
static void test_controller_model(void) {
    static const struct { const char* text; int want; } OK[] = {
        { "off", CONTROLLER_MODEL_OFF }, { "frame", CONTROLLER_MODEL_FRAME }, { "generic", CONTROLLER_MODEL_GENERIC },
        { "gun", CONTROLLER_MODEL_GUN }, { "custom", CONTROLLER_MODEL_CUSTOM }, { "Gun", CONTROLLER_MODEL_GUN },
        { "0", CONTROLLER_MODEL_OFF }, { "1", CONTROLLER_MODEL_FRAME }, { "2", CONTROLLER_MODEL_GENERIC },
        { "3", CONTROLLER_MODEL_GUN }, { "4", CONTROLLER_MODEL_CUSTOM },
        { "true", CONTROLLER_MODEL_FRAME }, { "false", CONTROLLER_MODEL_OFF }, { "on", CONTROLLER_MODEL_FRAME },
    };
    for (size_t i = 0; i < sizeof OK / sizeof OK[0]; i++) {
        Settings s;
        SettingsLoadInfo info;
        char text[64];
        settings_defaults(&s);
        s.controller_model = CONTROLLER_MODEL_GUN;   // so a value that does not apply is seen
        snprintf(text, sizeof text, "[vr]\ncontroller_model = %s\n", OK[i].text);
        settings_from_text(&s, text, &info);
        CHECK(s.controller_model == OK[i].want && info.applied == 1 && info.bad == 0);
    }
    // Not a kind: the last good one stays, and it is counted
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    s.controller_model = CONTROLLER_MODEL_GENERIC;
    settings_from_text(&s, "[vr]\ncontroller_model = 5\n", &info);
    CHECK(s.controller_model == CONTROLLER_MODEL_GENERIC && info.bad == 1);
    settings_from_text(&s, "[vr]\ncontroller_model = knife\n", &info);
    CHECK(s.controller_model == CONTROLLER_MODEL_GENERIC && info.bad == 1);
    // Written by name, the default commented out
    s.controller_model = CONTROLLER_MODEL_CUSTOM;
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\ncontroller_model = custom\n") != NULL);
    free(text);
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#controller_model = off\n") != NULL);
    free(text);
    // The Display tab's row: a cell for each kind but custom, which marks none of them
    SessionToggles t;
    session_from_settings(&s, &t);
    for (int k = CONTROLLER_MODEL_OFF; k < CONTROLLER_MODEL_COUNT; k++) {
        s.controller_model = k;
        CHECK(settings_option_cell(&s, &t, COG_OPTION_CONTROLLERS) == k);
    }
    CHECK(COG_CONTROLLERS_CELLS == CONTROLLER_MODEL_CUSTOM);
}

// The cog opens on the tab it was last left on: [display] cog_tab, the tab
// names in COG_TAB_ order, screen to start with
static void test_cog_tab(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.cog_tab == COG_TAB_SCREEN);
    settings_from_text(&s, "[display]\ncog_tab = display\n", &info);
    CHECK(s.cog_tab == COG_TAB_DISPLAY && info.applied == 1 && info.bad == 0);
    settings_from_text(&s, "[display]\ncog_tab = 3d\n", &info);
    CHECK(s.cog_tab == COG_TAB_3D);
    settings_from_text(&s, "[display]\ncog_tab = picture\n", &info);
    CHECK(s.cog_tab == COG_TAB_PICTURE);
    settings_from_text(&s, "[display]\ncog_tab = about\n", &info);
    CHECK(s.cog_tab == COG_TAB_ABOUT);
    settings_from_text(&s, "[display]\ncog_tab = 1\n", &info);   // an index works too
    CHECK(s.cog_tab == COG_TAB_DISPLAY);
    settings_from_text(&s, "[display]\ncog_tab = sound\n", &info);   // not a tab: the last good one stays
    CHECK(s.cog_tab == COG_TAB_DISPLAY && info.bad == 1);
    settings_from_text(&s, "[display]\ncog_tab = 9\n", &info);
    CHECK(s.cog_tab == COG_TAB_DISPLAY && info.bad == 1);
    s.cog_tab = COG_TAB_3D;
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\ncog_tab = 3d\n") != NULL);
    free(text);
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#cog_tab = screen\n") != NULL);
    free(text);
}

// Native (PLAN 4.3): the 3D source and full-size SBS, through the file and
// back, with the names, an index, a bad value and the panel's view
static void test_stereo_source(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.stereo_source == STEREO_SOURCE_DEPTH && s.sbs_full == 0);
    CHECK(settings_find("3d.stereo_source") != NULL && settings_find("3d.sbs_full") != NULL);
    settings_from_text(&s, "[3d]\nstereo_source = sbs\nsbs_full = true\n", &info);
    CHECK(s.stereo_source == STEREO_SOURCE_SBS && s.sbs_full == 1 && info.applied == 2 && info.bad == 0);
    settings_from_text(&s, "[3d]\nstereo_source = Top-Bottom\n", &info);
    CHECK(s.stereo_source == STEREO_SOURCE_TB);
    settings_from_text(&s, "[3d]\nstereo_source = 1\n", &info);   // an index works too
    CHECK(s.stereo_source == STEREO_SOURCE_SBS);
    settings_from_text(&s, "[3d]\nstereo_source = anaglyph\n", &info);   // not a source: the last good one stays
    CHECK(s.stereo_source == STEREO_SOURCE_SBS && info.bad == 1);
    s.stereo_source = STEREO_SOURCE_TB;
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nstereo_source = top-bottom\n") != NULL);
    CHECK(text != NULL && strstr(text, "\nsbs_full = true\n") != NULL);
    Settings back;
    settings_defaults(&back);
    if (text != NULL) settings_from_text(&back, text, &info);
    CHECK(back.stereo_source == STEREO_SOURCE_TB && back.sbs_full == 1 && settings_equal(&s, &back));
    free(text);
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#stereo_source = depth\n") != NULL);
    CHECK(text != NULL && strstr(text, "\n#sbs_full = false\n") != NULL);
    free(text);
    // the panel's view: the PC's pair needs no depth model, so the 3D rows
    // and the bar's button are there with the model off
    s.depth_source = 2;
    UiView v;
    ui_view_init(&v, &s);
    CHECK(!v.stereo_ok && !v.bar_show_stereo && v.stereo_source == STEREO_SOURCE_DEPTH);
    s.stereo_source = STEREO_SOURCE_SBS;
    ui_view_init(&v, &s);
    CHECK(v.stereo_ok && v.bar_show_stereo && v.stereo_source == STEREO_SOURCE_SBS);
}

// Native (lane wave2-bind-wire): Click steadiness and the stick's wheel speed
static void test_click_and_scroll(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.click_steadiness == CLICK_STEADY_NORMAL && s.scroll_speed == SCROLL_SPEED_DEFAULT);
    settings_from_text(&s, "[vr]\nclick_steadiness = steady\nscroll_speed = 250\n", &info);
    CHECK(s.click_steadiness == CLICK_STEADY_STEADY && s.scroll_speed == 250 && info.bad == 0);
    settings_from_text(&s, "[vr]\nclick_steadiness = Off\n", &info);
    CHECK(s.click_steadiness == CLICK_STEADY_OFF);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nclick_steadiness = off\n") != NULL);
    CHECK(text != NULL && strstr(text, "\nscroll_speed = 250\n") != NULL);
    free(text);
    SessionToggles t;
    session_from_settings(&s, &t);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_CLICK_STEADY) == CLICK_STEADY_OFF);
    s.click_steadiness = CLICK_STEADY_STEADY;
    CHECK(settings_option_cell(&s, &t, COG_OPTION_CLICK_STEADY) == CLICK_STEADY_STEADY);
}

// Native (lane wave3-cards): the first-run controls card's once-only mark and the pad card's mode
static void test_cards_settings(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.first_run_shown == 0 && s.pad_card == PADCARD_AUTO);
    memset(&info, 0, sizeof info);
    settings_from_text(&s, "[vr]\nfirst_run_shown = yes\npad_card = OFF\n", &info);
    CHECK(s.first_run_shown == 1 && s.pad_card == PADCARD_OFF && info.bad == 0);
    settings_from_text(&s, "[vr]\npad_card = 1\n", &info);
    CHECK(s.pad_card == PADCARD_ON);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nfirst_run_shown = true\n") != NULL && strstr(text, "\npad_card = on\n") != NULL);
    free(text);
    // a file that says false brings the card back on the next stream
    settings_from_text(&s, "[vr]\nfirst_run_shown = false\n", &info);
    CHECK(s.first_run_shown == 0);
    memset(&info, 0, sizeof info);
    settings_from_text(&s, "[vr]\npad_card = sometimes\n", &info);
    CHECK(s.pad_card == PADCARD_ON && info.bad == 1);
}

// Native (lane wave2-lightgun-gyro): the lightgun policy and the gamepad's gyro
static void test_lightgun_and_gyro(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    // defaults: the lightgun follows the Gun model with recoil and no reload; no gyro, always on
    CHECK(s.lightgun == LIGHTGUN_AUTO && s.lightgun_reload == 0 && s.lightgun_recoil == 1);
    CHECK(s.pad_gyro == PAD_GYRO_OFF && s.pad_gyro_ratchet == PAD_GYRO_RATCHET_ALWAYS);
    settings_from_text(&s, "[vr]\nlightgun = On\nlightgun_reload = true\nlightgun_recoil = false\n"
                           "pad_gyro = head\npad_gyro_ratchet = stick_touch\n", &info);
    CHECK(s.lightgun == LIGHTGUN_ON && s.lightgun_reload == 1 && s.lightgun_recoil == 0 && info.bad == 0);
    CHECK(s.pad_gyro == PAD_GYRO_HEAD && s.pad_gyro_ratchet == PAD_GYRO_RATCHET_STICK_TOUCH);
    settings_from_text(&s, "[vr]\nlightgun = off\npad_gyro = controller\npad_gyro_ratchet = always\n", &info);
    CHECK(s.lightgun == LIGHTGUN_OFF && s.pad_gyro == PAD_GYRO_CONTROLLER && s.pad_gyro_ratchet == PAD_GYRO_RATCHET_ALWAYS);
    // indices read too, and a word that is not a value leaves the last good one
    settings_from_text(&s, "[vr]\nlightgun = 1\npad_gyro = 2\n", &info);
    CHECK(s.lightgun == LIGHTGUN_ON && s.pad_gyro == PAD_GYRO_HEAD);
    settings_from_text(&s, "[vr]\nlightgun = sometimes\npad_gyro = wrist\n", &info);
    CHECK(s.lightgun == LIGHTGUN_ON && s.pad_gyro == PAD_GYRO_HEAD && info.bad == 2);
    // they are written by name, and not at all while they are at their defaults
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nlightgun = on\n") != NULL);
    CHECK(text != NULL && strstr(text, "\npad_gyro = head\n") != NULL);
    CHECK(text != NULL && strstr(text, "\n#pad_gyro_ratchet = always\n") != NULL);
    free(text);
    // the Frame pad advertises nothing by default: the opt-in is off in a fresh file
    settings_defaults(&s);
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#pad_gyro = off\n") != NULL);
    CHECK(text != NULL && strstr(text, "\n#lightgun = auto\n") != NULL);
    free(text);
}

static void test_network_path(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    settings_from_text(&s, "[stream]\nnetwork_path = adapter\n", &info);
    CHECK(s.network_path == 2);
    settings_from_text(&s, "[stream]\nnetwork_path = Home\n", &info);
    CHECK(s.network_path == 1);
    settings_from_text(&s, "[stream]\nnetwork_path = USB\n", &info);   // Native (lane wave4c-usb)
    CHECK(s.network_path == 3);
    settings_from_text(&s, "[stream]\nnetwork_path = home\n", &info);
    CHECK(s.network_path == 1);
    settings_from_text(&s, "[stream]\nnetwork_path = ethernet\n", &info);   // not a value: the last good one stays
    CHECK(s.network_path == 1 && info.bad == 1);
    settings_defaults(&s);
    CHECK(s.network_path == 0);
    s.network_path = 2;
    char* text = settings_to_text(&s);
    CHECK(text && strstr(text, "network_path = adapter"));
    free(text);
    s.network_path = 3;
    text = settings_to_text(&s);
    CHECK(text && strstr(text, "network_path = usb"));
    free(text);
}

static void test_session_limits(void) {
    // Native (lane wave3-session): the headset-off and idle disconnects
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.doff_disconnect == 60 && s.idle_disconnect == 0);   // a minute, and no idle limit
    CHECK(settings_find("stream.doff_disconnect") != NULL && settings_find("idle_disconnect") != NULL);
    settings_from_text(&s, "[stream]\ndoff_disconnect = 0\nidle_disconnect = 30\n", &info);
    CHECK(s.doff_disconnect == 0 && s.idle_disconnect == 30 && info.applied == 2 && info.bad == 0);
    settings_from_text(&s, "[stream]\ndoff_disconnect = 999999\nidle_disconnect = -4\n", &info);   // held to the range
    CHECK(s.doff_disconnect == 3600 && s.idle_disconnect == 0);
    settings_from_text(&s, "[stream]\ndoff_disconnect = soon\n", &info);   // not a number: the last good one stays
    CHECK(s.doff_disconnect == 3600 && info.bad == 1);
    settings_defaults(&s);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#doff_disconnect = 60\n") != NULL && strstr(text, "\n#idle_disconnect = 0\n") != NULL);
    free(text);
    s.doff_disconnect = 15;
    s.idle_disconnect = 45;
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\ndoff_disconnect = 15\n") != NULL && strstr(text, "\nidle_disconnect = 45\n") != NULL);
    free(text);
}

static void test_path_bitrates(void) {
    // Native (lane wave4b-abr): one start bitrate per network path, 0 = the launcher's
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.bitrate_wifi == 0 && s.bitrate_adapter == 0 && s.bitrate_wired == 0 && s.bitrate_usb == 0);
    CHECK(settings_find("stream.bitrate_wifi") != NULL && settings_find("bitrate_adapter") != NULL);
    CHECK(settings_find("bitrate_wired") != NULL && settings_find("bitrate_usb") != NULL);
    settings_from_text(&s, "[stream]\nbitrate_wifi = 60000\nbitrate_adapter = 150000\nbitrate_wired = 250000\nbitrate_usb = 500000\n", &info);
    CHECK(s.bitrate_wifi == 60000 && s.bitrate_adapter == 150000 && s.bitrate_wired == 250000 && s.bitrate_usb == 500000);
    CHECK(info.applied == 4 && info.bad == 0);
    int ov[4];
    settings_abr_overrides(&s, ov);
    CHECK(ov[0] == 60000 && ov[1] == 150000 && ov[2] == 250000 && ov[3] == 500000);   // xr_abr.h's order
    settings_from_text(&s, "[stream]\nbitrate_wifi = 99999999\nbitrate_usb = -3\nbitrate_adapter = 120\n", &info);   // held to the range
    CHECK(s.bitrate_wifi == MLXR_BITRATE_MAX_KBPS && s.bitrate_usb == 0 && s.bitrate_adapter == 500);
    settings_from_text(&s, "[stream]\nbitrate_wired = fast\n", &info);   // not a number: the last good one stays
    CHECK(s.bitrate_wired == 250000 && info.bad == 1);
    settings_defaults(&s);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#bitrate_wifi = 0\n") != NULL && strstr(text, "\n#bitrate_usb = 0\n") != NULL);
    free(text);
    s.bitrate_wifi = 60000;
    s.bitrate_wired = 300000;
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nbitrate_wifi = 60000\n") != NULL && strstr(text, "\nbitrate_wired = 300000\n") != NULL);
    free(text);
}

// Native (lane wave1-burst): [stream] rfi_idr_ms, the cap on the client's own IDR frame requests
static void test_rfi_idr_ms(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.rfi_idr_ms == 500);
    CHECK(settings_find("stream.rfi_idr_ms") != NULL && settings_find("rfi_idr_ms") != NULL);
    settings_from_text(&s, "[stream]\nrfi_idr_ms = 0\n", &info);   // 0: no cap
    CHECK(s.rfi_idr_ms == 0 && info.applied == 1 && info.bad == 0);
    settings_from_text(&s, "[stream]\nrfi_idr_ms = 90000\n", &info);   // held to the range
    CHECK(s.rfi_idr_ms == 5000);
    settings_from_text(&s, "[stream]\nrfi_idr_ms = -1\n", &info);
    CHECK(s.rfi_idr_ms == 0);
    settings_defaults(&s);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#rfi_idr_ms = 500\n") != NULL);
    free(text);
    s.rfi_idr_ms = 1000;
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nrfi_idr_ms = 1000\n") != NULL);
    free(text);
}

// Native (lane wave1-failover): [stream] failover (off by default) and failover_burst_ms
static void test_failover(void) {
    Settings s;
    SettingsLoadInfo info;
    settings_defaults(&s);
    CHECK(s.failover == 0 && s.failover_burst_ms == 3000);
    CHECK(settings_find("stream.failover") != NULL && settings_find("failover_burst_ms") != NULL);
    settings_from_text(&s, "[stream]\nfailover = on\nfailover_burst_ms = 5000\n", &info);
    CHECK(s.failover == 1 && s.failover_burst_ms == 5000 && info.applied == 2 && info.bad == 0);
    settings_from_text(&s, "[stream]\nfailover = OFF\n", &info);
    CHECK(s.failover == 0);
    settings_from_text(&s, "[stream]\nfailover = maybe\n", &info);   // not a value: the last good one stays
    CHECK(s.failover == 0 && info.bad == 1);
    settings_from_text(&s, "[stream]\nfailover_burst_ms = 50\n", &info);   // held to 500..9000
    CHECK(s.failover_burst_ms == 500);
    settings_from_text(&s, "[stream]\nfailover_burst_ms = 60000\n", &info);
    CHECK(s.failover_burst_ms == 9000);
    settings_defaults(&s);
    char* text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\n#failover = off\n") != NULL && strstr(text, "\n#failover_burst_ms = 3000\n") != NULL);
    free(text);
    s.failover = 1;
    s.failover_burst_ms = 2000;
    text = settings_to_text(&s);
    CHECK(text != NULL && strstr(text, "\nfailover = on\n") != NULL && strstr(text, "\nfailover_burst_ms = 2000\n") != NULL);
    free(text);
}

static void test_tolerant(void) {
    Settings s;
    settings_defaults(&s);
    SettingsLoadInfo info;
    const char* junk =
        "\xEF\xBB\xBF# a BOM and then a file written by hand\r\n"
        "[Stream]\r\n"
        "HOST =  192.168.1.10  \r\n"
        "app=\"Desktop\"\r\n"
        "; a semicolon comment\r\n"
        "[nonsense]\r\n"
        "foo = 1\r\n"
        "[vr]\r\n"
        "this line has no equals sign\r\n"
        "bar = 2\r\n"
        "pointer_sleep = maybe\r\n"          // not a bool
        "head_aim_sensitivity = 7.5\r\n"     // not an int
        "head_aim = true\r\n"
        "= no key\r\n"
        "[unterminated\r\n"
        "hand_tracking = 1\r\n"
        "gamepad_toggle = sticks\r\n"
        "pad_grip = stick_clicks\r\n"
        "rumble_mix = split\r\n"
        "rumble_intensity = 250\r\n"
        "head_locked = true\r\n"             // belongs to [display], in the wrong section: still read
        "\x01\x02\x03 binary = 7\r\n";
    settings_from_text(&s, junk, &info);
    CHECK(strcmp(s.host, "192.168.1.10") == 0);
    CHECK(strcmp(s.app, "Desktop") == 0);
    CHECK(s.head_aim == 1 && s.hand_tracking == 1 && s.gamepad_toggle == PAD_SHORTCUT_STICKS);
    CHECK(s.head_locked == 1);
    CHECK(s.pad_grip == PAD_GRIP_STICK_CLICKS);
    CHECK(s.rumble_mix == PAD_RUMBLE_MIX_SPLIT && s.rumble_intensity == 100);   // 250 clamps to 100
    CHECK(s.pointer_sleep == 1 && s.head_aim_sensitivity == 8);   // unreadable values leave the default
    CHECK(info.unknown >= 3);
    CHECK(info.bad >= 4);
    // nothing but noise
    Settings n2;
    settings_defaults(&n2);
    Settings ref;
    settings_defaults(&ref);
    settings_from_text(&n2, "", NULL);
    CHECK(settings_equal(&n2, &ref));
    settings_from_text(&n2, NULL, NULL);
    CHECK(settings_equal(&n2, &ref));
    char noise[2000];
    srand(7);
    for (int i = 0; i < (int)sizeof noise - 1; i++) noise[i] = (char)(rand() % 255 + 1);
    noise[sizeof noise - 1] = 0;
    settings_from_text(&n2, noise, &info);
    int cnt;
    const SettingDesc* t = settings_table(&cnt);
    for (int i = 0; i < cnt; i++) {   // whatever happened, the struct is valid
        if (t[i].type == SET_STR) CHECK(strlen(settings_get_str(&n2, &t[i])) < t[i].scap);
        else CHECK(settings_get(&n2, &t[i]) >= t[i].min && settings_get(&n2, &t[i]) <= t[i].max);
    }
    // a very long line, a long string value, a value with control characters
    char* longline = malloc(5000);
    memset(longline, 'a', 4999);
    longline[4999] = 0;
    char* text = malloc(6000);
    snprintf(text, 6000, "[stream]\nhost = %s\napp = tab\there\nfps = 72\n", longline);
    Settings l;
    settings_defaults(&l);
    settings_from_text(&l, text, NULL);
    CHECK(strlen(l.host) < UI_STR_MAX);
    CHECK(strchr(l.app, '\t') == NULL);
    free(longline);
    free(text);
    // later lines win; strings keep '#' inside values
    Settings d;
    settings_defaults(&d);
    settings_from_text(&d, "[stream]\nfps = 30\nfps = 120\nhost = my#pc\n", NULL);
    CHECK(settings_fps_value(&d) == 120 && strcmp(d.host, "my#pc") == 0);
    // Native (lane refresh): 144 is a choice, nothing above it
    settings_from_text(&d, "[stream]\nfps = 144\n", NULL);
    CHECK(settings_fps_value(&d) == 144);
}

static void test_files(void) {
    char tmpl[] = "/tmp/mlxr-ui-test-XXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (!dir) return;
    char path[512], sub[512];
    // MLXR_CONFIG_DIR picks the directory
    snprintf(sub, sizeof sub, "%s/a/b", dir);
    setenv("MLXR_CONFIG_DIR", sub, 1);
    char buf[600];
    CHECK(strcmp(settings_default_path(buf, sizeof buf), strcat(strcpy(path, sub), "/config.ini")) == 0);
    Settings s, back;
    settings_defaults(&s);
    settings_defaults(&back);
    SettingsLoadInfo info;
    CHECK(settings_load(&back, NULL, &info) == 1);    // no file yet: defaults stay
    CHECK(settings_equal(&s, &back));
    make_nondefault(&s, 99);
    CHECK(settings_save(&s, NULL) == 0);              // creates a/b
    struct stat st;
    CHECK(stat(path, &st) == 0 && st.st_size > 2000);
    char tmpfile[600];
    snprintf(tmpfile, sizeof tmpfile, "%s.tmp", path);
    CHECK(stat(tmpfile, &st) != 0);                   // renamed into place, nothing left behind
    CHECK(settings_load(&back, NULL, &info) == 0);
    CHECK(settings_equal(&s, &back));
    // saving over an existing file
    s.stats = !s.stats;
    CHECK(settings_save(&s, path) == 0);
    settings_defaults(&back);
    CHECK(settings_load(&back, path, NULL) == 0 && settings_equal(&s, &back));
    // HOME is the fallback
    unsetenv("MLXR_CONFIG_DIR");
    setenv("HOME", "/home/someone", 1);
    CHECK(strcmp(settings_default_path(buf, sizeof buf), "/home/someone/.config/moonlight-xr/config.ini") == 0);
    // unwritable
    CHECK(settings_save(&s, "/proc/nope/config.ini") < 0);
    CHECK(settings_load(&back, "/nonexistent/dir/config.ini", NULL) == 1);
    // cleanup
    unlink(path);
    snprintf(tmpfile, sizeof tmpfile, "%s/a/b", dir);
    rmdir(tmpfile);
    snprintf(tmpfile, sizeof tmpfile, "%s/a", dir);
    rmdir(tmpfile);
    rmdir(dir);
}

static void test_sliders(void) {
    Settings s;
    settings_defaults(&s);
    for (int row = 0; row < COG_SCREEN_TRACK_ROWS; row++) {
        for (int i = 0; i <= 10; i++) {
            float t = (float)i / 10.0f;
            settings_set_screen_slider(&s, row, t);
            float back = settings_screen_slider(&s, row);
            CHECK(back >= 0.0f && back <= 1.0f);
            // within a stored unit of where it was put, except across the level snap
            if (!((row == COG_SLIDER_TILT || row == COG_SLIDER_ROTATE) && i == 5)) CHECK_NEAR(back, t, 0.02);
        }
    }
    // level snaps: a touch off the middle of tilt or rotate is exactly level
    settings_set_screen_slider(&s, COG_SLIDER_TILT, 0.5f + 0.01f);
    CHECK(s.screen_tilt_deg == 0);
    settings_set_screen_slider(&s, COG_SLIDER_ROTATE, 0.5f - 0.02f);
    CHECK(s.screen_rotate_deg == 0);
    settings_set_screen_slider(&s, COG_SLIDER_TILT, 0.5f + 0.1f);
    CHECK(s.screen_tilt_deg == 8);
    // ends map to the upstream ranges
    settings_set_screen_slider(&s, COG_SLIDER_DISTANCE, 0.0f);
    CHECK(s.screen_distance_cm == 20);
    settings_set_screen_slider(&s, COG_SLIDER_DISTANCE, 1.0f);
    CHECK(s.screen_distance_cm == 800);
    settings_set_screen_slider(&s, COG_SLIDER_SIZE, 0.0f);
    CHECK(s.screen_width_cm == 80);
    settings_set_screen_slider(&s, COG_SLIDER_HEIGHT, 1.0f);
    CHECK(s.screen_height_cm == 200);
    settings_set_screen_slider(&s, 99, 0.5f);   // not a row: nothing happens
    settings_reset_screen(&s);
    CHECK(s.screen_distance_cm == 300 && s.screen_width_cm == 300 && s.screen_tilt_deg == 0);
    s.separation = 9;
    s.convergence = 70;
    settings_reset_3d(&s);
    CHECK(s.separation == -1 && s.convergence == -1);
    s.picture_gamma = 150;
    settings_reset_picture(&s);
    CHECK(s.picture_gamma == 100 && settings_picture_value(&s, PICTURE_GAMMA) == 100);
    CHECK(settings_picture_ptr(&s, 7) == NULL);

    // the Display tab's cells from the settings and the session
    settings_defaults(&s);
    SessionToggles t;
    session_from_settings(&s, &t);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_SHARPEN) == 0);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_SUPERSAMPLE) == 1);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_STATS) == 0);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_GAMEPAD) == 0);   // every session starts as the pointer
    CHECK(settings_option_cell(&s, &t, COG_OPTION_RAY) == 2);   // native: ray with its dot
    {
        // native: the ray row's three cells, none / dot / ray
        int ray = -1, dot = -1;
        settings_pointer_from_cell(0, &ray, &dot);
        CHECK(ray == 0 && dot == 0);
        settings_pointer_from_cell(1, &ray, &dot);
        CHECK(ray == 0 && dot == 1);
        settings_pointer_from_cell(2, &ray, &dot);
        CHECK(ray == 1 && dot == 1);
        for (int c = 0; c < 3; c++) {
            settings_pointer_from_cell(c, &ray, &dot);
            CHECK(settings_pointer_cell(ray, dot) == c);
        }
        CHECK(settings_pointer_cell(1, 0) == 2);
        t.ray = 0;
        CHECK(settings_option_cell(&s, &t, COG_OPTION_RAY) == 1);
        s.pointer_dot = 0;
        CHECK(settings_option_cell(&s, &t, COG_OPTION_RAY) == 0);
        s.pointer_dot = 1;
        t.ray = 1;
    }
    CHECK(settings_option_cell(&s, &t, COG_OPTION_AMBILIGHT) == 0 && settings_option_cell(&s, &t, COG_OPTION_ROOM_LIGHT) == 1);
    CHECK(settings_option_cell(&s, &t, 99) == -1);
    t.pad_mode = 1;
    CHECK(settings_option_cell(&s, &t, COG_OPTION_GAMEPAD) == 1);
    CHECK(t.stereo_on == 1);   // the 3D on by default, as upstream
    s.depth_source = 2;
    session_from_settings(&s, &t);
    CHECK(t.stereo_on == 0);
}

// The Frame's left menu button is its View button: no hint or toast says "menu"
// for the gamepad-mode chord
static void test_shortcut_wording(void) {
    const char* hint = ui_str(UIS_SHORTCUT_MENU_GRIP);
    const char* back = ui_str(UIS_TOAST_BACK_MENU_GRIP);
    CHECK(strcmp(hint, "Shortcut: left View + grip") == 0);
    CHECK(strstr(back, "left View button") != NULL);
    CHECK(strstr(hint, "menu") == NULL && strstr(back, "menu") == NULL);
}

// The two input rows say what they do: Mode is pointer or gamepad, Show
// controllers is whether the models are drawn (the old "Controllers" and
// "Controller model" sat side by side and read as the same thing)
static void test_input_row_wording(void) {
    CHECK(strcmp(ui_str(UIS_ROW_MODE), "Mode") == 0);
    CHECK(strcmp(ui_str(UIS_ROW_SHOW_CONTROLLERS), "Show controllers") == 0);
    CHECK(strcmp(ui_str(UIS_POINTER), "Pointer") == 0 && strcmp(ui_str(UIS_GAMEPAD), "Gamepad") == 0);
    CHECK(strcmp(ui_str(UIS_ROW_MODE), ui_str(UIS_ROW_SHOW_CONTROLLERS)) != 0);
    CHECK(strstr(ui_str(UIS_ROW_MODE), "Controller") == NULL);
}

// Native (lane wave3-hud): the Stats overlay's style, and `stats = hud` as stats on in that style
static void test_stats_style(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(s.stats == 0 && s.stats_style == 0);
    CHECK(geti(&s, "stats_style") == 0);
    settings_from_text(&s, "[display]\nstats = hud\n", NULL);
    CHECK(s.stats == 1 && s.stats_style == 1);
    settings_from_text(&s, "[display]\nstats = lite\n", NULL);
    CHECK(s.stats == 1 && s.stats_style == 2);
    settings_from_text(&s, "[display]\nstats = full\n", NULL);
    CHECK(s.stats == 1 && s.stats_style == 0);
    // the plain forms are as they were
    settings_from_text(&s, "[display]\nstats = false\nstats_style = hud\n", NULL);
    CHECK(s.stats == 0 && s.stats_style == 1);
    settings_from_text(&s, "[display]\nstats = true\n", NULL);
    CHECK(s.stats == 1 && s.stats_style == 1);   // the style stays as it was set
    settings_from_text(&s, "[display]\nstats_style = lite\n", NULL);
    CHECK(s.stats_style == 2);
    SettingsLoadInfo info;
    settings_from_text(&s, "[display]\nstats_style = nonsense\n", &info);
    CHECK(info.bad == 1 && s.stats_style == 2);
    // `hud` means nothing to the other on/off keys
    settings_from_text(&s, "[display]\nglow = hud\n", &info);
    CHECK(info.bad == 1);
    // the file keeps both and reads them back
    s.stats = 1;
    s.stats_style = 1;
    char* text = settings_to_text(&s);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text, NULL);
    CHECK(back.stats == 1 && back.stats_style == 1);
    CHECK(strstr(text, "\nstats_style = hud\n") != NULL);
    free(text);
    // the cog row shows on or off for every style
    SessionToggles t;
    session_from_settings(&s, &t);
    CHECK(settings_option_cell(&s, &t, COG_OPTION_STATS) == 1);
}

// The Head locked row's third cell, the leash, the per app switch and the
// angular-size lock
static void test_follow_and_lock_settings(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(geti(&s, "head_follow") == 0 && geti(&s, "follow_leash_deg") == 25);
    CHECK(geti(&s, "per_app") == 1 && geti(&s, "angular_lock") == 1);
    CHECK(FOLLOW_LEASH_DEFAULT_DEG == 25);
    CHECK(settings_head_lock_cell(&s) == 0);
    CHECK(settings_option_cell(&s, &(SessionToggles){ 0 }, COG_OPTION_HEAD_LOCK) == 0);
    s.head_follow = 1;
    CHECK(settings_option_cell(&s, &(SessionToggles){ 0 }, COG_OPTION_HEAD_LOCK) == 2);
    s.head_locked = 1;
    // locked wins, and the clamp drops the follow
    settings_clamp(&s);
    CHECK(s.head_follow == 0 && settings_head_lock_cell(&s) == 1);
    // the leash is held to its lane
    settings_set(&s, settings_find("follow_leash_deg"), 5);
    CHECK(s.follow_leash_deg == FOLLOW_LEASH_MIN_DEG);
    settings_set(&s, settings_find("follow_leash_deg"), 500);
    CHECK(s.follow_leash_deg == FOLLOW_LEASH_MAX_DEG);
    CHECK(COG_HEAD_LOCK_CELLS == 3);
}

static AppLayout layout_of(int distance, int width, int stereo) {
    Settings s;
    settings_defaults(&s);
    s.screen_distance_cm = distance;
    s.screen_width_cm = width;
    s.stereo = stereo;
    AppLayout l;
    settings_layout_snapshot(&s, &l);
    return l;
}

// The names that key a layout, and the layouts kept in the file
static void test_layouts(void) {
    // the hash: stable, case blind, told apart by either name, none without an app
    unsigned a = settings_app_hash("Desk PC", "Desktop");
    CHECK(a != 0);
    CHECK(a == settings_app_hash("desk pc", "DESKTOP"));
    CHECK(a != settings_app_hash("Desk PC", "Steam Big Picture"));
    CHECK(a != settings_app_hash("Other PC", "Desktop"));
    CHECK(settings_app_hash("Desk PC", "") == 0 && settings_app_hash("Desk PC", NULL) == 0);
    CHECK(settings_app_hash(NULL, "Desktop") != 0);
    // "ab"+"c" is not "a"+"bc"
    CHECK(settings_app_hash("ab", "c") != settings_app_hash("a", "bc"));

    Settings s;
    settings_defaults(&s);
    CHECK(s.layout_count == 0 && settings_layout_find(&s, a) == NULL);
    AppLayout l1 = layout_of(120, 160, 0);
    CHECK(settings_layout_store(&s, a, &l1) == 1);
    CHECK(s.layout_count == 1 && settings_layout_find(&s, a) != NULL);
    CHECK(settings_layout_find(&s, a)->hash == a);
    // the same again changes nothing, a different one replaces it
    CHECK(settings_layout_store(&s, a, &l1) == 0);
    AppLayout l2 = layout_of(400, 500, 1);
    CHECK(settings_layout_store(&s, a, &l2) == 1 && s.layout_count == 1);
    CHECK(settings_layout_find(&s, a)->distance_cm == 400 && settings_layout_find(&s, a)->stereo == 1);
    CHECK(settings_layout_store(&s, 0, &l2) == 0);

    // it is a section of the file with the settings' own keys, and comes back
    unsigned b = settings_app_hash("Desk PC", "Cyberpunk 2077");
    CHECK(settings_layout_store(&s, b, &l1) == 1);
    char* text = settings_to_text(&s);
    char want[64];
    snprintf(want, sizeof want, "[screen.%08x]", a);
    CHECK(strstr(text, want) != NULL);
    CHECK(strstr(text, "distance_cm = 400") != NULL);
    // every active line outside the sections is still the defaults' commented ones
    Settings back;
    settings_defaults(&back);
    SettingsLoadInfo info;
    settings_from_text(&back, text, &info);
    CHECK(info.unknown == 0 && info.bad == 0);
    CHECK(settings_equal(&s, &back));
    CHECK(back.layout_count == 2);
    CHECK(settings_layout_find(&back, b)->width_cm == 160);
    // and none of it reached the settings in force
    Settings d;
    settings_defaults(&d);
    CHECK(back.screen_distance_cm == d.screen_distance_cm && back.stereo == d.stereo);
    // saving it again is the same file
    char* again = settings_to_text(&back);
    CHECK(strcmp(text, again) == 0);
    free(again);
    free(text);

    // a section with some keys: the rest are the defaults'; a bad hash, an
    // unknown key and a bad value are counted and change nothing in force
    Settings t;
    settings_defaults(&t);
    settings_from_text(&t,
        "[screen.00000abc]\n"
        "distance_cm = 333\n"
        "bogus = 1\n"
        "width_cm = wide\n"
        "head_locked = yes\n"
        "[screen.nothex]\n"
        "distance_cm = 111\n"
        "[screen.00000def]\n"
        "distance_cm = 99999\n"
        "[screen]\n"
        "tilt_deg = 7\n", &info);
    CHECK(t.layout_count == 2);
    const AppLayout* abc = settings_layout_find(&t, 0xabc);
    CHECK(abc != NULL && abc->distance_cm == 333 && abc->width_cm == d.screen_width_cm && abc->head_locked == 1);
    CHECK(info.unknown >= 2 && info.bad == 1 && info.clamped == 1);
    CHECK(settings_layout_find(&t, 0xdef)->distance_cm == 800);
    CHECK(t.screen_distance_cm == d.screen_distance_cm && t.screen_tilt_deg == 7);

    // apply puts one in force, snapshot reads it back
    Settings u;
    settings_defaults(&u);
    settings_layout_apply(&u, abc);
    CHECK(u.screen_distance_cm == 333 && u.head_locked == 1 && u.head_follow == 0);
    AppLayout snap;
    settings_layout_snapshot(&u, &snap);
    CHECK(settings_layout_same(&snap, abc));
    CHECK(!settings_layout_same(&snap, &l1));

    // forgetting
    CHECK(settings_layout_forget(&t, 0xabc) == 1 && t.layout_count == 1);
    CHECK(settings_layout_forget(&t, 0xabc) == 0 && settings_layout_forget(&t, 0) == 0);
    CHECK(settings_layout_find(&t, 0xabc) == NULL && settings_layout_find(&t, 0xdef) != NULL);

    // the file holds APP_LAYOUT_MAX and drops the oldest
    Settings full;
    settings_defaults(&full);
    for (unsigned i = 1; i <= APP_LAYOUT_MAX + 5; i++) {
        AppLayout l = layout_of(100 + (int)i, 200, 0);
        CHECK(settings_layout_store(&full, 0x1000 + i, &l) == 1);
    }
    CHECK(full.layout_count == APP_LAYOUT_MAX);
    CHECK(settings_layout_find(&full, 0x1001) == NULL && settings_layout_find(&full, 0x1005) == NULL);
    CHECK(settings_layout_find(&full, 0x1006) != NULL);
    CHECK(settings_layout_find(&full, 0x1000 + APP_LAYOUT_MAX + 5) != NULL);
    // a replaced one moves to the newest end
    AppLayout l3 = layout_of(555, 200, 0);
    CHECK(settings_layout_store(&full, 0x1006, &l3) == 1);
    CHECK(full.layouts[APP_LAYOUT_MAX - 1].hash == 0x1006);
    char* big = settings_to_text(&full);
    Settings bigBack;
    settings_defaults(&bigBack);
    settings_from_text(&bigBack, big, &info);
    CHECK(settings_equal(&full, &bigBack) && info.unknown == 0 && info.bad == 0);
    free(big);

    // out of range values in a layout are held to the setting's range, on load and on store
    AppLayout wild = layout_of(100, 200, 0);
    wild.distance_cm = 100000;
    wild.separation = -50;
    wild.head_locked = 1;
    wild.head_follow = 1;
    Settings w;
    settings_defaults(&w);
    CHECK(settings_layout_store(&w, 7, &wild) == 1);
    CHECK(settings_layout_find(&w, 7)->distance_cm == 800 && settings_layout_find(&w, 7)->separation == -1);
    CHECK(settings_layout_find(&w, 7)->head_follow == 0);
}

// Native (lane wave3-visual): Background dim, the void's colour, Glow edge and
// 3D Eye Balance
static void test_surroundings(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(geti(&s, "background_dim") == 0 && s.bg_dim == BG_DIM_DEFAULT);
    CHECK(strcmp(gets(&s, "void_color"), "#000000") == 0);
    CHECK(geti(&s, "glow_feather") == GLOW_FEATHER_OFF && s.glow_feather == 0);
    CHECK(geti(&s, "eye_balance") == 0 && s.eye_balance == EYE_BALANCE_DEFAULT);
    // where each lives in the file
    CHECK(strcmp(settings_find("background_dim")->section, "vr") == 0);
    CHECK(strcmp(settings_find("void_color")->section, "vr") == 0);
    CHECK(strcmp(settings_find("glow_feather")->section, "display") == 0);
    CHECK(strcmp(settings_find("eye_balance")->section, "3d") == 0);
    // their ranges
    CHECK(settings_find("background_dim")->min == 0 && settings_find("background_dim")->max == 100);
    CHECK(settings_find("eye_balance")->min == -100 && settings_find("eye_balance")->max == 100);
    CHECK(settings_find("glow_feather")->nnames == GLOW_FEATHER_COUNT);

    // out of range values are pulled in, on load as through the setter
    settings_from_text(&s, "[vr]\nbackground_dim = 250\n[3d]\neye_balance = -400\n", NULL);
    CHECK(s.bg_dim == 100 && s.eye_balance == -100);
    settings_from_text(&s, "[vr]\nbackground_dim = -3\n[3d]\neye_balance = 33\n", NULL);
    CHECK(s.bg_dim == 0 && s.eye_balance == 33);

    // the names, their indexes and the wrong ones
    settings_defaults(&s);
    settings_from_text(&s, "[display]\nglow_feather = wide\n", NULL);
    CHECK(s.glow_feather == GLOW_FEATHER_WIDE);
    settings_from_text(&s, "[display]\nglow_feather = soft\n", NULL);
    CHECK(s.glow_feather == GLOW_FEATHER_SOFT);
    settings_from_text(&s, "[display]\nglow_feather = 0\n", NULL);
    CHECK(s.glow_feather == GLOW_FEATHER_OFF);
    SettingsLoadInfo info;
    settings_from_text(&s, "[display]\nglow_feather = huge\n", &info);
    CHECK(info.bad == 1 && s.glow_feather == GLOW_FEATHER_OFF);

    // through the file and back, as typed
    settings_defaults(&s);
    s.bg_dim = 35;
    s.eye_balance = -60;
    s.glow_feather = GLOW_FEATHER_SOFT;
    settings_set_str(&s, settings_find("void_color"), "#1a2b3c");
    char* text = settings_to_text(&s);
    CHECK(strstr(text, "\nbackground_dim = 35\n") != NULL);
    CHECK(strstr(text, "\neye_balance = -60\n") != NULL);
    CHECK(strstr(text, "\nglow_feather = soft\n") != NULL);
    CHECK(strstr(text, "\nvoid_color = #1a2b3c\n") != NULL);
    Settings back;
    settings_defaults(&back);
    settings_from_text(&back, text, &info);
    free(text);
    CHECK(settings_equal(&s, &back) && info.bad == 0 && info.unknown == 0);
    CHECK(strcmp(gets(&back, "void_color"), "#1a2b3c") == 0);

    // the colour: with or without the hash, short or long, any case
    float rgb[3] = { 0.5f, 0.5f, 0.5f };
    CHECK(settings_parse_hex_rgb("#ff8000", rgb) == 1);
    CHECK(rgb[0] == 1.0f && rgb[1] > 0.50f && rgb[1] < 0.51f && rgb[2] == 0.0f);
    CHECK(settings_parse_hex_rgb("00FF00", rgb) == 1 && rgb[0] == 0.0f && rgb[1] == 1.0f && rgb[2] == 0.0f);
    CHECK(settings_parse_hex_rgb("  #f00  ", rgb) == 1 && rgb[0] == 1.0f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    CHECK(settings_parse_hex_rgb("abc", rgb) == 1 && rgb[0] > 0.66f && rgb[0] < 0.67f && rgb[2] > 0.79f && rgb[2] < 0.81f);
    // not colours: nothing touched
    rgb[0] = 0.25f; rgb[1] = 0.5f; rgb[2] = 0.75f;
    CHECK(settings_parse_hex_rgb("", rgb) == 0);
    CHECK(settings_parse_hex_rgb("#", rgb) == 0);
    CHECK(settings_parse_hex_rgb("#12345", rgb) == 0);
    CHECK(settings_parse_hex_rgb("#1234567", rgb) == 0);
    CHECK(settings_parse_hex_rgb("#12345678", rgb) == 0);
    CHECK(settings_parse_hex_rgb("#gg0000", rgb) == 0);
    CHECK(settings_parse_hex_rgb("red", rgb) == 0);
    CHECK(settings_parse_hex_rgb(NULL, rgb) == 0);
    CHECK(rgb[0] == 0.25f && rgb[1] == 0.5f && rgb[2] == 0.75f);
    // a bad colour in the file is black, and the default is too
    settings_set_str(&s, settings_find("void_color"), "mauve");
    CHECK(settings_void_rgb(&s, rgb) == 0 && rgb[0] == 0.0f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    settings_defaults(&s);
    rgb[0] = 1.0f;
    CHECK(settings_void_rgb(&s, rgb) == 1 && rgb[0] == 0.0f && rgb[1] == 0.0f && rgb[2] == 0.0f);

    // the Picture tab's Reset takes the two rows with it, and leaves the colour
    s.bg_dim = 80;
    s.glow_feather = GLOW_FEATHER_WIDE;
    s.picture_gamma = 150;
    settings_set_str(&s, settings_find("void_color"), "#102030");
    settings_reset_picture(&s);
    CHECK(s.bg_dim == BG_DIM_DEFAULT && s.glow_feather == GLOW_FEATHER_OFF && s.picture_gamma == 100);
    CHECK(strcmp(gets(&s, "void_color"), "#102030") == 0);
    s.eye_balance = 40;
    settings_reset_3d(&s);
    CHECK(s.eye_balance == 40);   // the 3D tab's reset is the depth pair's
}

int main(void) {
    test_surroundings();
    test_follow_and_lock_settings();
    test_layouts();
    test_defaults();
    test_stats_style();
    test_shortcut_wording();
    test_input_row_wording();
    test_table_covers_readme();
    test_round_trip();
    test_defaults_commented_out();
    test_clamping();
    test_network_path();
    test_session_limits();
    test_path_bitrates();
    test_rfi_idr_ms();
    test_failover();
    test_tolerant();
    test_removed_keys();
    test_bitrate_ceiling();
    test_notices();
    test_click_and_scroll();
    test_cards_settings();
    test_lightgun_and_gyro();
    test_controller_model();
    test_cog_tab();
    test_stereo_source();
    test_files();
    test_sliders();
    return checksDone("ui_settings");
}
