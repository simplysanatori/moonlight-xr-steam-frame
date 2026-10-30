// See settings.h. GPLv3.
#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xr_shared.h"
#include "xr_cards.h"
#include "xr_padmouse.h"

// Where the tilt and rotate tracks clip to level, in radians (xr_renderer.h:
// COG_TILT_SNAP, COG_ROLL_SNAP)
#define COG_TILT_SNAP_RAD 0.0388f
#define COG_ROLL_SNAP_RAD 0.0873f

const char* const SETTINGS_RESOLUTIONS[] = { "640x360", "854x480", "1280x720", "1920x1080",
                                             "2560x1440", "3840x2160" };
const int SETTINGS_RESOLUTION_COUNT = 6;
// Native (lane refresh): 144, the Frame's fastest display rate, added at the end
const int SETTINGS_FPS[] = { 30, 60, 72, 90, 120, 144 };
const int SETTINGS_FPS_COUNT = 6;

static const char* const FPS_NAMES[] = { "30", "60", "72", "90", "120", "144" };
static const char* const CODEC_NAMES[] = { "auto", "av1", "hevc", "h264" };
static const char* const AUDIO_NAMES[] = { "stereo", "5.1", "7.1" };
static const char* const NETWORK_NAMES[] = { "auto", "home", "adapter", "usb" };
static const char* const PACING_NAMES[] = { "latency", "balanced", "cap-fps", "smoothness" };
static const char* const TOGGLE_NAMES[] = { "menu_grip", "sticks", "triggers_grips" };
static const char* const RUMBLEMIX_NAMES[] = { "both", "split" };
static const char* const PADGRIP_NAMES[] = { "bumpers", "none", "stick_clicks" };
static const char* const PADMOUSECHORD_NAMES[] = { "start_hold", "start_back", "off" };
static const char* const PADMOUSESCROLL_NAMES[] = { "right", "left", "none" };
static const char* const PADARB_NAMES[] = { "pointer", "also_gamepad", "sleep" };
static const char* const PADCARD_NAMES[] = { "auto", "on", "off" };
_Static_assert(sizeof(PADCARD_NAMES) / sizeof(PADCARD_NAMES[0]) == PADCARD_MODES, "pad_card names follow PADCARD_");
static const char* const ENV_NAMES[] = { "auto", "passthrough", "void", "home_theater",
                                         "grand_cinema", "synthwave" };
static const int ENV_IDS[] = { ENV_ID_NONE, ENV_ID_PASSTHROUGH, ENV_ID_VOID, ENV_ID_HOME_THEATER,
                               ENV_ID_GRAND_CINEMA, ENV_ID_SYNTHWAVE };
static const char* const DEPTH_NAMES[] = { "zipdepth", "midas", "off", "flat", "ramp", "blob",
                                           "eyetest", "shifttest", "zipdepth-int8" };
static const char* const ENVRES_NAMES[] = { "low", "standard", "high", "ultra" };
static const char* const NOTICES_NAMES[] = { "all", "important", "off" };
static const char* const STEADY_NAMES[] = { "off", "normal", "steady" };
static const char* const LOG_NAMES[] = { "off", "basic", "verbose" };
static const char* const KBLAYOUT_NAMES[] = { "compact", "full" };
static const char* const COGTAB_NAMES[] = { "screen", "display", "3d", "picture", "about" };
_Static_assert(sizeof(COGTAB_NAMES) / sizeof(COGTAB_NAMES[0]) == COG_TAB_COUNT, "cog_tab names follow COG_TAB_");
static const char* const KEYMAP_NAMES[] = { "qwerty", "azerty", "qwertz" };
static const char* const STATS_STYLE_NAMES[] = { "full", "hud", "lite" };
static const char* const SHARP_NAMES[] = { "off", "normal", "quality" };
// CONTROLLER_MODEL_*: 1 is what the old on/off switch's true meant
static const char* const MODEL_NAMES[] = { "off", "frame", "generic", "gun", "custom" };
_Static_assert(sizeof(MODEL_NAMES) / sizeof(MODEL_NAMES[0]) == CONTROLLER_MODEL_COUNT, "controller_model names follow CONTROLLER_MODEL_");

#define NAMES(a) (a), (int)(sizeof(a) / sizeof((a)[0]))
#define NONAMES NULL, 0
#define F(f) offsetof(Settings, f)

#define DB(sec, key, field, def, doc) { sec, key, SET_BOOL, F(field), 0, 1, def, NONAMES, NULL, 0, doc }
#define DI(sec, key, field, lo, hi, def, doc) { sec, key, SET_INT, F(field), lo, hi, def, NONAMES, NULL, 0, doc }
#define DE(sec, key, field, names, def, doc) { sec, key, SET_ENUM, F(field), 0, (int)(sizeof(names) / sizeof((names)[0])) - 1, def, NAMES(names), NULL, 0, doc }
#define DS(sec, key, field, sdef, doc) { sec, key, SET_STR, F(field), 0, 0, 0, NONAMES, sdef, sizeof(((Settings*)0)->field), doc }

#define ROOMF(n, env) \
    DI("rooms", "room_" #env "_brightness", room[n].brightness, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX, 0, "Room tab: brightness in hundredths of the room as baked (room_brightness_" #env ")"), \
    DB("rooms", "room_" #env "_glow", room[n].glow, 0, "Room tab: glow on (room_glow_" #env ")"), \
    DI("rooms", "room_" #env "_light", room[n].light, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX, ROOM_LIGHT_DEFAULT, "Room tab: light level in hundredths (room_light_" #env ")"), \
    DI("rooms", "room_" #env "_screen", room[n].screen, ROOM_SCREEN_MIN, ROOM_SCREEN_MAX, 100, "Room tab: picture size, percent of the room's screen (room_screen_" #env ")")

static const SettingDesc TABLE[] = {
    // stream
    DS("stream", "host", host, "", "address of the host PC last used"),
    DS("stream", "app", app, "", "name of the host app last launched (empty: ask)"),
    DE("stream", "resolution", resolution, SETTINGS_RESOLUTIONS, 4, "list_resolution; 1440p on a headset, 4K is one entry away"),
    DE("stream", "fps", fps, FPS_NAMES, 3, "list_fps; the stream never goes above the display's rate (SteamVR's per-app setting: 72, 90, 120 or 144)"),
    DI("stream", "bitrate_kbps", bitrate_kbps, 0, MLXR_BITRATE_MAX_KBPS, 0, "seekbar_bitrate_kbps; 0 follows resolution and frame rate, else 500..500000 (the flat launcher's custom bitrate goes to 500 Mbps)"),
    DE("stream", "codec", codec, CODEC_NAMES, 0, "list_video_format: auto, av1, hevc, h264"),
    DE("stream", "audio_config", audio_config, AUDIO_NAMES, 0, "list_audio_config: stereo, 5.1, 7.1"),
    DB("stream", "virtual_surround", virtual_surround, 0, "checkbox_vr_virtual_surround: place 5.1 and 7.1 speakers around you in stereo"),
    DE("stream", "frame_pacing", frame_pacing, PACING_NAMES, 0, "frame_pacing"),
    DE("stream", "network_path", network_path, NETWORK_NAMES, 0, "native: which link reaches the PC: auto (probe all), home (Wi-Fi), adapter (Steam Frame Wireless Adapter) or usb (a USB cable between the headset and the PC); the --network flag and MLXR_NETWORK override it"),
    DI("stream", "doff_disconnect", doff_disconnect, 0, 3600, 60, "native: seconds with the headset off before the stream is disconnected, leaving the host's app running; 0 never. The sound mutes and the input is let go the moment it comes off, and putting it back on within the time carries on"),
    DI("stream", "idle_disconnect", idle_disconnect, 0, 1440, 0, "native: minutes without any input (controllers, hands, keyboard, mouse, pads) before the stream is disconnected, leaving the host's app running; 0 never"),
    // TODO(PLAN 6.4): auto_desktop (open the host's Desktop app without the app list) comes
    // back when something reads it. Dropped with vr_mode, check_updates, language and
    // doff_grace, which were written with descriptions and read by nothing; old files that
    // still carry them load fine (unknown keys are ignored and counted).
    // vr
    DB("vr", "pointer", pointer, 1, "checkbox_vr_pointer: controllers as a mouse (false leaves only the gamepad switch live)"),
    DB("vr", "pointer_sleep", pointer_sleep, 1, "checkbox_vr_pointer_sleep: pause the pointer when the controller is still"),
    DB("vr", "show_ray", show_ray, 1, "checkbox_vr_show_ray: show the controller ray"),
    DB("vr", "pointer_dot", pointer_dot, 1, "native: the dot where the ray lands (the host draws its own cursor into the video, which the Moonlight protocol cannot hide)"),
    DE("vr", "keyboard_layout", keyboard_layout, KBLAYOUT_NAMES, 0, "native: the headset keyboard, compact (Moonlight XR's) or full (tenkeyless); the key on each keyboard flips it"),
    DE("vr", "keyboard_keymap", keyboard_keymap, KEYMAP_NAMES, 0, "native: what the headset keyboard's keys say, qwerty, azerty (French) or qwertz (German); the keys send the US keyboard's positions, so the PC must be on the matching Windows layout"),
    DI("vr", "keyboard_hold_ms", keyboard_hold_ms, 200, 1500, 500, "native: milliseconds a Shift, Ctrl, Alt or Win key is held on the full keyboard to lock it down (a tap lights it for the next key)"),
    DE("vr", "controller_model", controller_model, MODEL_NAMES, 0, "checkbox_vr_controller_model: which controller is drawn in each hand; custom reads your own files from the models folder beside this file (true reads as frame, false as off)"),
    DB("vr", "head_aim", head_aim, 0, "checkbox_vr_head_aim: head aims the mouse (screen head locked, outside a room)"),
    DI("vr", "head_aim_sensitivity", head_aim_sensitivity, HEAD_AIM_SENSITIVITY_MIN, HEAD_AIM_SENSITIVITY_MAX, HEAD_AIM_SENSITIVITY_DEFAULT, "seekbar_vr_head_aim_sensitivity: pixels per degree"),
    DI("vr", "head_aim_deadzone", head_aim_deadzone, HEAD_AIM_DEADZONE_MIN, HEAD_AIM_DEADZONE_MAX, HEAD_AIM_DEADZONE_DEFAULT, "seekbar_vr_head_aim_deadzone: degrees per second"),
    DE("vr", "gamepad_toggle", gamepad_toggle, TOGGLE_NAMES, 0, "list_vr_gamepad_toggle: menu_grip, sticks, triggers_grips"),
    DB("vr", "flip_face_buttons", flip_face_buttons, 0, "checkbox_flip_face_buttons: swap A/B and X/Y on the gamepad"),
    DE("vr", "rumble_mix", rumble_mix, RUMBLEMIX_NAMES, 0, "native: the host's rumble on the controllers in gamepad mode: both (each hand gets the stronger motor) or split (low frequency motor left, high frequency motor right)"),
    DI("vr", "rumble_intensity", rumble_intensity, 0, 100, 100, "native: percent, the scale on the host's rumble on the controllers in gamepad mode"),
    DE("vr", "pad_grip", pad_grip, PADGRIP_NAMES, 0, "native: what the grips are in gamepad mode: bumpers (LB and RB, as the bumpers), none, or stick_clicks (LS and RS)"),
    DE("vr", "pad_mouse_chord", pad_mouse_chord, PADMOUSECHORD_NAMES, 0, "native: what turns a physical gamepad into the PC's mouse (stick moves the pointer, A/B/X click, LB/RB back/forward, Y the VR keyboard, d-pad arrows): start_hold (hold Start for a second), start_back (hold Start and Back) or off; the same chord gives the pad back"),
    DE("vr", "pad_mouse_scroll", pad_mouse_scroll, PADMOUSESCROLL_NAMES, 0, "native: the stick that scrolls while a gamepad is a mouse: right (the left stick points), left (the right stick points) or none"),
    DE("vr", "pad_arbitration", pad_arbitration, PADARB_NAMES, 0, "native: when a physical gamepad is used while the Frame controllers are the gamepad: pointer (the controllers' pad is taken off the PC and they point again; gamepad mode off and on brings it back), also_gamepad (the PC sees both pads) or sleep (as pointer, and the controllers' pointer rests while the gamepad is in use)"),
    DB("vr", "click_sound", click_sound, 1, "checkbox_vr_click_sound"),
    DE("vr", "notices", notices, NOTICES_NAMES, 0, "native: the toast notices (upstream #42): all, important (leaves out the display rate and 3D on/off; keeps mode changes and errors) or off"),
    DE("vr", "click_steadiness", click_steadiness, STEADY_NAMES, CLICK_STEADY_NORMAL, "native: Display tab Click steadiness, how still the cursor is held around a click: off (the aim as it is, for lightgun games), normal (the press sent from before the pull jolted the aim, and the double-click wait) or steady (and the cursor held from the first touch of the trigger); the MLXR_PRESS_STABILISE, MLXR_DOUBLE_CLICK_PX/_MS and MLXR_CLICK_HOLD_PX variables still override each part"),
    DI("vr", "scroll_speed", scroll_speed, SCROLL_SPEED_MIN, SCROLL_SPEED_MAX, SCROLL_SPEED_DEFAULT, "native: how fast a thumbstick scrolls, percent (100 is 7.5 notches a second at full tilt); the wheel is smooth, and the stick's sideways tilt scrolls sideways"),
    DB("vr", "gaze", gaze, 0, "checkbox_vr_gaze: look to point (native default off: with it on a controller takes the pointer back only on a press)"),
    DB("vr", "hand_tracking", hand_tracking, 0, "checkbox_vr_hand_tracking (native default off until checked on the Frame)"),
    DB("vr", "passthrough", passthrough, 0, "checkbox_vr_passthrough"),
    DE("vr", "environment", environment, ENV_NAMES, 0, "list_vr_environment: auto = black void, or passthrough with passthrough on"),
    DE("vr", "depth_source", depth_source, DEPTH_NAMES, 0, "list_vr_depth_source: Realtime 3D mode (zipdepth recommended, midas older, zipdepth-int8 lighter on the CPU, off streams flat; flat, ramp, blob, eyetest, shifttest are test patterns)"),
    DE("vr", "env_res", env_res, ENVRES_NAMES, 1, "list_vr_env_res: Environment Res, how sharp the rooms are drawn: low (half the headset's recommended eye size), standard, high or ultra (the runtime's largest); read when the rooms are built at the start of a stream"),
    DB("vr", "hand_hint_seen", hand_hint_seen, 0, "the hand lock hint was put away for good"),
    DB("vr", "first_run_shown", first_run_shown, 0, "native: the first-run controls card has been shown (it comes up once, on the first stream; set to false to see it again)"),
    DE("vr", "pad_card", pad_card, PADCARD_NAMES, PADCARD_AUTO, "native: the gamepad card, a schematic of the Bluetooth or USB gamepad in use that lights up as you press it, held in your lap: auto (up while a gamepad is in use, gone 4 seconds after the last input), on (up while a gamepad counts as in use) or off"),
    DI("vr", "picture_brightness", picture_brightness, PICTURE_BRIGHTNESS_MIN, PICTURE_BRIGHTNESS_MAX, PICTURE_BRIGHTNESS_DEFAULT, "seekbar_vr_picture_brightness: -50..50"),
    DI("vr", "picture_contrast", picture_contrast, PICTURE_CONTRAST_MIN, PICTURE_CONTRAST_MAX, PICTURE_CONTRAST_DEFAULT, "seekbar_vr_picture_contrast: percent, 50..150"),
    DI("vr", "picture_gamma", picture_gamma, PICTURE_GAMMA_MIN, PICTURE_GAMMA_MAX, PICTURE_GAMMA_DEFAULT, "seekbar_vr_picture_gamma: hundredths, 50..200"),
    DI("vr", "picture_saturation", picture_saturation, PICTURE_SATURATION_MIN, PICTURE_SATURATION_MAX, PICTURE_SATURATION_DEFAULT, "seekbar_vr_picture_saturation: percent, 0..200"),
    // debug
    DI("debug", "depth_rate", depth_rate, DEPTH_RATE_MIN, DEPTH_RATE_MAX, DEPTH_RATE_DEFAULT, "seekbar_vr_inference_cadence: the most depth maps made per second (the 3D picture follows the video; fewer saves the CPU)"),
    DB("debug", "depth_debug", depth_debug, 0, "checkbox_vr_depth_debug: show the depth map in place of the picture"),
    DB("debug", "eye_swap", eye_swap, 0, "checkbox_vr_eye_swap: swap the left and right eye of the 3D picture (for a wrong-way test pattern)"),
    DE("debug", "file_log", file_log, LOG_NAMES, 1, "list_vr_file_log: off, basic (warnings and errors), verbose"),
    // display tab
    DB("display", "head_locked", head_locked, 0, "Display tab: head locked screen (checkbox_vr_head_locked)"),
    DB("display", "head_follow", head_follow, 0, "native: Display tab, Head locked row's Follow cell: the screen stays in the world until the head turns past the leash, then eases after the gaze (head_locked wins when both are on)"),
    DI("display", "follow_leash_deg", follow_leash_deg, FOLLOW_LEASH_MIN_DEG, FOLLOW_LEASH_MAX_DEG, FOLLOW_LEASH_DEFAULT_DEG, "native: Follow mode's leash, degrees of head yaw or pitch away from the screen before it eases after the gaze"),
    DE("display", "sharpening", sharpening, SHARP_NAMES, 0, "Display tab: sharpen (list_vr_sharpening; native default off, the owner's choice, upstream quality)"),
    DE("display", "supersampling", supersampling, SHARP_NAMES, 1, "Display tab: supersample (list_vr_supersampling; native default normal, the owner's choice, upstream off; never used with the 3D on)"),
    DB("display", "stats", stats, 0, "Display tab: show performance stats (true, false, or hud / lite to switch them on in that style)"),
    DE("display", "stats_style", stats_style, STATS_STYLE_NAMES, 0, "native: what the stats show: full (every line the session writes), hud (latency, network, decode and rate in a few lines) or lite (speed, latency, decode, loss and fps on one line); with stats on, a perf-<stamp>.csv next to vrtiming.log gets a row every 5 s"),
    DB("display", "glow", glow, 0, "Display tab: glow (checkbox_vr_ambilight; native default off, the owner's choice, upstream on)"),
    DI("display", "glow_level", glow_level, 0, 100, 50, "Display tab: glow level, percent in 5 percent steps (seekbar_vr_ambilight_level)"),
    DB("display", "screen_light", screen_light, 1, "Display tab: screen light in rooms (checkbox_vr_room_light)"),
    DE("display", "cog_tab", cog_tab, COGTAB_NAMES, 0, "native: the tab the cog opens on, the one it was last left on: screen (room in a room), display, 3d, picture or about"),
    // 3d
    DI("3d", "separation", separation, -1, COG_SEP_STEPS, -1, "seekbar_vr_separation: tenths of a percent of frame width; -1 is the depth model's own (ZipDepth 6, MiDaS 5)"),
    DI("3d", "convergence", convergence, -1, 100, -1, "seekbar_vr_convergence: percent; -1 is the depth model's own (50)"),
    DB("3d", "stereo", stereo, 1, "native: the 3D switch (bar button, 3D tab) as it was last left; on to begin with, as upstream started every session"),
    // screen
    DI("screen", "distance_cm", screen_distance_cm, 20, 800, 300, "Screen tab: distance in centimetres"),
    DI("screen", "height_cm", screen_height_cm, -200, 200, 0, "Screen tab: height of the centre above eye level, centimetres"),
    DI("screen", "azimuth_deg", screen_azimuth_deg, -180, 180, 0, "direction the picture sits in, degrees to the left of straight ahead"),
    DI("screen", "tilt_deg", screen_tilt_deg, -40, 40, 0, "Screen tab: tilt, degrees"),
    DI("screen", "rotate_deg", screen_rotate_deg, -90, 90, 0, "Screen tab: rotate, degrees clockwise"),
    DI("screen", "curve", screen_curve, 0, 100, 0, "Screen tab: curve, percent (seekbar_vr_curvature)"),
    DI("screen", "width_cm", screen_width_cm, 80, 800, 300, "Screen tab: size, picture width in centimetres (seekbar_vr_screen_size)"),
    DB("screen", "per_app", per_app, 1, "native: Screen tab, Remember per app: each PC and app keeps its own placement, head lock and 3D state in a [screen.<hash>] section; an app with none starts from the settings in force"),
    DB("screen", "angular_lock", angular_lock, 1, "native: Screen tab, Angular-size lock: pushing or pulling the held screen with the thumbsticks resizes it to look the same size"),
    // rooms
    ROOMF(0, 4),
    ROOMF(1, 5),
    ROOMF(2, 6),
};
#define TABLE_COUNT ((int)(sizeof(TABLE) / sizeof(TABLE[0])))

const SettingDesc* settings_table(int* count) {
    if (count) *count = TABLE_COUNT;
    return TABLE;
}

static int streq_ci(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

const SettingDesc* settings_find(const char* name) {
    const char* dot = strchr(name, '.');
    for (int i = 0; i < TABLE_COUNT; i++) {
        if (dot) {
            size_t sl = (size_t)(dot - name);
            if (strlen(TABLE[i].section) == sl && strncasecmp(TABLE[i].section, name, sl) == 0 &&
                streq_ci(TABLE[i].key, dot + 1)) {
                return &TABLE[i];
            }
        } else if (streq_ci(TABLE[i].key, name)) {
            return &TABLE[i];
        }
    }
    return NULL;
}

// ---- room defaults -----------------------------------------------------------------

static const int ROOM_DEF_BRIGHTNESS[ROOM_COUNT] = { ROOM_THEATER_BRIGHTNESS,
                                                    ROOM_GRAND_CINEMA_BRIGHTNESS,
                                                    ROOM_SYNTHWAVE_BRIGHTNESS };
static const int ROOM_DEF_GLOW[ROOM_COUNT] = { ROOM_THEATER_GLOW, ROOM_GRAND_CINEMA_GLOW,
                                               ROOM_SYNTHWAVE_GLOW };
static const int ROOM_DEF_SCREEN[ROOM_COUNT] = { ROOM_THEATER_SCREEN, ROOM_GRAND_CINEMA_SCREEN,
                                                 ROOM_SYNTHWAVE_SCREEN };
static const int ROOM_RESIZABLE[ROOM_COUNT] = { ROOM_THEATER_RESIZABLE, ROOM_GRAND_CINEMA_RESIZABLE,
                                                ROOM_SYNTHWAVE_RESIZABLE };

int settings_room_index(int env_id) {
    return env_id == ENV_ID_HOME_THEATER ? 0 : env_id == ENV_ID_GRAND_CINEMA ? 1
         : env_id == ENV_ID_SYNTHWAVE ? 2 : -1;
}

int settings_room_resizable(int env_id) {
    int i = settings_room_index(env_id);
    return i >= 0 && ROOM_RESIZABLE[i];
}

// the default of a room field (the table's def column holds a placeholder for
// the ones that differ per room)
static int default_of(const SettingDesc* d) {
    if (d->type == SET_INT || d->type == SET_BOOL) {
        if (strncmp(d->key, "room_", 5) == 0) {
            int env = atoi(d->key + 5);
            int r = settings_room_index(env);
            if (r >= 0) {
                if (strstr(d->key, "_brightness")) return ROOM_DEF_BRIGHTNESS[r];
                if (strstr(d->key, "_glow")) return ROOM_DEF_GLOW[r];
                if (strstr(d->key, "_screen")) return ROOM_DEF_SCREEN[r];
            }
        }
    }
    return d->def;
}

// ---- field access ----------------------------------------------------------------------

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static void layout_clamp(AppLayout* l);

int settings_get(const Settings* s, const SettingDesc* d) {
    return *(const int*)((const char*)s + d->offset);
}

void settings_set(Settings* s, const SettingDesc* d, int value) {
    *(int*)((char*)s + d->offset) = clamp_int(value, d->min, d->max);
}

const char* settings_get_str(const Settings* s, const SettingDesc* d) {
    return (const char*)s + d->offset;
}

void settings_set_str(Settings* s, const SettingDesc* d, const char* v) {
    char* dst = (char*)s + d->offset;
    size_t n = 0;
    while (v && *v && isspace((unsigned char)*v)) v++;   // the file form cannot carry edge spaces
    for (; v && v[n] && n + 1 < d->scap; n++) {
        unsigned char c = (unsigned char)v[n];
        dst[n] = c < 0x20 ? ' ' : (char)c;   // one line, no control characters
    }
    while (n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = 0;
}

int settings_is_default(const Settings* s, const SettingDesc* d) {
    if (d->type == SET_STR) return strcmp(settings_get_str(s, d), d->sdef) == 0;
    return settings_get(s, d) == default_of(d);
}

void settings_defaults(Settings* s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < TABLE_COUNT; i++) {
        const SettingDesc* d = &TABLE[i];
        if (d->type == SET_STR) settings_set_str(s, d, d->sdef);
        else *(int*)((char*)s + d->offset) = default_of(d);
    }
}

void settings_clamp(Settings* s) {
    for (int i = 0; i < TABLE_COUNT; i++) {
        const SettingDesc* d = &TABLE[i];
        if (d->type == SET_STR) {
            ((char*)s + d->offset)[d->scap - 1] = 0;
        } else {
            settings_set(s, d, settings_get(s, d));
        }
    }
    // a bitrate is 0 (follow resolution and rate) or 500..MLXR_BITRATE_MAX_KBPS kbps
    if (s->bitrate_kbps > 0 && s->bitrate_kbps < 500) s->bitrate_kbps = 500;
    // glow level moves in five percent steps
    s->glow_level = (s->glow_level + 2) / 5 * 5;
    for (int r = 0; r < ROOM_COUNT; r++) {
        if (!ROOM_RESIZABLE[r]) s->room[r].screen = ROOM_SCREEN_MAX;
    }
    // locked to the head wins over following it
    if (s->head_locked) s->head_follow = 0;
    s->layout_count = clamp_int(s->layout_count, 0, APP_LAYOUT_MAX);
    for (int i = 0; i < s->layout_count; i++) layout_clamp(&s->layouts[i]);
}

int settings_equal(const Settings* a, const Settings* b) {
    for (int i = 0; i < TABLE_COUNT; i++) {
        const SettingDesc* d = &TABLE[i];
        if (d->type == SET_STR) {
            if (strcmp(settings_get_str(a, d), settings_get_str(b, d)) != 0) return 0;
        } else if (settings_get(a, d) != settings_get(b, d)) {
            return 0;
        }
    }
    if (a->layout_count != b->layout_count) return 0;
    return memcmp(a->layouts, b->layouts, sizeof(AppLayout) * (size_t)clamp_int(a->layout_count, 0, APP_LAYOUT_MAX)) == 0;
}

// ---- per PC and app layouts -----------------------------------------------------------------
// One [screen.<hash>] section each. The keys are the settings' own, found by
// their section and name so the range and kind of each is the table's.

typedef struct {
    const char* key;
    size_t offset;          // into AppLayout
    const char* setting;    // the Settings field it remembers, "section.key"
} LayoutKey;

#define LK(k, g) { #k, offsetof(AppLayout, k), g }
static const LayoutKey LAYOUT_KEYS[] = {
    LK(distance_cm, "screen.distance_cm"), LK(height_cm, "screen.height_cm"),
    LK(azimuth_deg, "screen.azimuth_deg"), LK(tilt_deg, "screen.tilt_deg"),
    LK(rotate_deg, "screen.rotate_deg"), LK(curve, "screen.curve"), LK(width_cm, "screen.width_cm"),
    LK(head_locked, "display.head_locked"), LK(head_follow, "display.head_follow"),
    LK(stereo, "3d.stereo"), LK(separation, "3d.separation"), LK(convergence, "3d.convergence"),
};
#define LAYOUT_KEY_COUNT ((int)(sizeof(LAYOUT_KEYS) / sizeof(LAYOUT_KEYS[0])))

static const SettingDesc* layout_desc(int i) {
    static const SettingDesc* cache[sizeof(LAYOUT_KEYS) / sizeof(LAYOUT_KEYS[0])];
    if (!cache[i]) cache[i] = settings_find(LAYOUT_KEYS[i].setting);
    return cache[i];
}

static int* layout_field(AppLayout* l, int i) { return (int*)((char*)l + LAYOUT_KEYS[i].offset); }
static const int* layout_cfield(const AppLayout* l, int i) { return (const int*)((const char*)l + LAYOUT_KEYS[i].offset); }

static void layout_clamp(AppLayout* l) {
    for (int i = 0; i < LAYOUT_KEY_COUNT; i++) {
        const SettingDesc* d = layout_desc(i);
        *layout_field(l, i) = clamp_int(*layout_field(l, i), d->min, d->max);
    }
    if (l->head_locked) l->head_follow = 0;
}

unsigned settings_app_hash(const char* host, const char* app) {
    if (!app || !*app) return 0;
    unsigned h = 2166136261u;   // FNV-1a over the names, lower case, a newline between
    for (const char* p = host ? host : ""; *p; p++) {
        h = (h ^ (unsigned)tolower((unsigned char)*p)) * 16777619u;
    }
    h = (h ^ (unsigned)'\n') * 16777619u;
    for (const char* p = app; *p; p++) {
        h = (h ^ (unsigned)tolower((unsigned char)*p)) * 16777619u;
    }
    return h ? h : 1u;
}

const AppLayout* settings_layout_find(const Settings* s, unsigned hash) {
    if (!hash) return NULL;
    for (int i = 0; i < s->layout_count && i < APP_LAYOUT_MAX; i++) {
        if (s->layouts[i].hash == hash) return &s->layouts[i];
    }
    return NULL;
}

void settings_layout_snapshot(const Settings* s, AppLayout* out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < LAYOUT_KEY_COUNT; i++) {
        *layout_field(out, i) = settings_get(s, layout_desc(i));
    }
    layout_clamp(out);
}

void settings_layout_apply(Settings* s, const AppLayout* l) {
    for (int i = 0; i < LAYOUT_KEY_COUNT; i++) {
        settings_set(s, layout_desc(i), *layout_cfield(l, i));
    }
    settings_clamp(s);
}

int settings_layout_same(const AppLayout* a, const AppLayout* b) {
    for (int i = 0; i < LAYOUT_KEY_COUNT; i++) {
        if (*layout_cfield(a, i) != *layout_cfield(b, i)) return 0;
    }
    return 1;
}

static void layout_remove_at(Settings* s, int at) {
    for (int i = at; i + 1 < s->layout_count; i++) s->layouts[i] = s->layouts[i + 1];
    s->layout_count--;
    memset(&s->layouts[s->layout_count], 0, sizeof(AppLayout));
}

// A slot at the end for a hash, the oldest dropped to make room
static AppLayout* layout_add(Settings* s, unsigned hash) {
    if (s->layout_count >= APP_LAYOUT_MAX) layout_remove_at(s, 0);
    AppLayout* l = &s->layouts[s->layout_count++];
    memset(l, 0, sizeof(*l));
    l->hash = hash;
    return l;
}

int settings_layout_store(Settings* s, unsigned hash, const AppLayout* l) {
    if (!hash) return 0;
    AppLayout copy = *l;
    copy.hash = hash;
    layout_clamp(&copy);
    for (int i = 0; i < s->layout_count && i < APP_LAYOUT_MAX; i++) {
        if (s->layouts[i].hash != hash) continue;
        if (settings_layout_same(&s->layouts[i], &copy)) return 0;
        layout_remove_at(s, i);
        break;
    }
    *layout_add(s, hash) = copy;
    return 1;
}

int settings_layout_forget(Settings* s, unsigned hash) {
    for (int i = 0; i < s->layout_count && i < APP_LAYOUT_MAX; i++) {
        if (s->layouts[i].hash == hash && hash) {
            layout_remove_at(s, i);
            return 1;
        }
    }
    return 0;
}

int settings_head_lock_cell(const Settings* s) {
    return s->head_locked ? 1 : (s->head_follow ? 2 : 0);
}

// "screen.<hex>", the section of one remembered layout. 0 when it is not one.
static unsigned layout_section_hash(const char* section) {
    if (strncasecmp(section, "screen.", 7) != 0 || !section[7]) return 0;
    char* end;
    unsigned long v = strtoul(section + 7, &end, 16);
    return *end || v == 0 || v > 0xFFFFFFFFul ? 0 : (unsigned)v;
}

// ---- text form -------------------------------------------------------------------------

typedef struct { char* p; size_t len, cap; } Buf;

static void buf_add(Buf* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = (int)sizeof tmp - 1;
    if (b->len + (size_t)n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + (size_t)n + 1) nc *= 2;
        char* np = realloc(b->p, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, tmp, (size_t)n);
    b->len += (size_t)n;
    b->p[b->len] = 0;
}

static void value_text(const Settings* s, const SettingDesc* d, char* out, size_t cap) {
    switch (d->type) {
    case SET_BOOL: snprintf(out, cap, "%s", settings_get(s, d) ? "true" : "false"); break;
    case SET_INT: snprintf(out, cap, "%d", settings_get(s, d)); break;
    case SET_ENUM: {
        int v = clamp_int(settings_get(s, d), 0, d->nnames - 1);
        snprintf(out, cap, "%s", d->names[v]);
        break;
    }
    case SET_STR: snprintf(out, cap, "%s", settings_get_str(s, d)); break;
    }
}

static void default_text(const SettingDesc* d, char* out, size_t cap) {
    Settings tmp;
    settings_defaults(&tmp);
    value_text(&tmp, d, out, cap);
}

static void range_note(const SettingDesc* d, char* out, size_t cap) {
    out[0] = 0;
    if (d->type == SET_INT) {
        snprintf(out, cap, " [%d..%d]", d->min, d->max);
    } else if (d->type == SET_ENUM) {
        size_t n = 0;
        n += (size_t)snprintf(out + n, cap - n, " [");
        for (int i = 0; i < d->nnames && n < cap; i++) {
            n += (size_t)snprintf(out + n, cap - n, "%s%s", i ? " | " : "", d->names[i]);
        }
        if (n < cap) snprintf(out + n, cap - n, "]");
    } else if (d->type == SET_BOOL) {
        snprintf(out, cap, " [true | false]");
    }
}

char* settings_to_text(const Settings* s) {
    Buf b = { 0 };
    buf_add(&b, "# Moonlight XR Native settings.\n"
                "#\n"
                "# Every setting is listed. A line that starts with '#' is at its default and\n"
                "# is ignored, so a default that changes in a later version reaches this file.\n"
                "# To change a setting remove the '#' and edit the value; set it back to the\n"
                "# default (or comment it out again) to follow the program's default.\n"
                "# Unknown keys are ignored and are dropped the next time the program saves.\n");
    const char* section = "";
    for (int i = 0; i < TABLE_COUNT; i++) {
        const SettingDesc* d = &TABLE[i];
        if (strcmp(section, d->section) != 0) {
            buf_add(&b, "\n[%s]\n", d->section);
            section = d->section;
        }
        char val[UI_STR_MAX + 8], def[UI_STR_MAX + 8], range[512];
        value_text(s, d, val, sizeof val);
        default_text(d, def, sizeof def);
        range_note(d, range, sizeof range);
        buf_add(&b, "# %s%s (default %s)\n", d->doc, range, def[0] ? def : "empty");
        buf_add(&b, "%s%s = %s\n", settings_is_default(s, d) ? "#" : "", d->key, val);
    }
    for (int n = 0; n < s->layout_count && n < APP_LAYOUT_MAX; n++) {
        const AppLayout* l = &s->layouts[n];
        buf_add(&b, "\n[screen.%08x]\n", l->hash);
        if (n == 0) {
            buf_add(&b, "# Remembered layouts, one section for each PC and app (Remember per app on the Screen\n"
                        "# tab). The name is a hash of the PC's and the app's names. The keys are the\n"
                        "# settings' own: screen placement, head lock and the 3D state. Delete a section\n"
                        "# to forget the app, or use Forget on the Screen tab while it streams.\n");
        }
        for (int i = 0; i < LAYOUT_KEY_COUNT; i++) {
            const SettingDesc* d = layout_desc(i);
            int v = *layout_cfield(l, i);
            if (d->type == SET_BOOL) buf_add(&b, "%s = %s\n", LAYOUT_KEYS[i].key, v ? "true" : "false");
            else buf_add(&b, "%s = %d\n", LAYOUT_KEYS[i].key, v);
        }
    }
    return b.p ? b.p : strdup("");
}

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = 0;
    return s;
}

static int parse_bool(const char* v, int* out) {
    static const char* const T[] = { "true", "on", "yes", "1", "y" };
    static const char* const F[] = { "false", "off", "no", "0", "n" };
    for (size_t i = 0; i < 5; i++) {
        if (streq_ci(v, T[i])) { *out = 1; return 1; }
        if (streq_ci(v, F[i])) { *out = 0; return 1; }
    }
    return 0;
}

static int apply_value(Settings* s, const SettingDesc* d, const char* v, SettingsLoadInfo* info) {
    if (d->type == SET_STR) {
        settings_set_str(s, d, v);
        return 1;
    }
    int x = 0;
    if (d->type == SET_BOOL && strcmp(d->key, "stats") == 0 && (streq_ci(v, "hud") || streq_ci(v, "lite") || streq_ci(v, "full"))) {
        // native: `stats = hud` is stats on, in the HUD style
        s->stats = 1;
        s->stats_style = streq_ci(v, "hud") ? 1 : streq_ci(v, "lite") ? 2 : 0;
        return 1;
    }
    if (d->type == SET_BOOL) {
        if (!parse_bool(v, &x)) return 0;
    } else if (d->type == SET_ENUM) {
        int found = -1;
        for (int i = 0; i < d->nnames; i++) {
            if (streq_ci(v, d->names[i])) { found = i; break; }
        }
        if (found < 0) {
            char* end;
            long n = strtol(v, &end, 10);
            int b = 0;
            if (strcmp(d->key, "controller_model") == 0 && parse_bool(v, &b)) {
                // Native: the key was a switch (true/false) before it was a kind
                found = b ? CONTROLLER_MODEL_FRAME : CONTROLLER_MODEL_OFF;
            } else {
                if (end == v || *end || n < 0 || n >= d->nnames) return 0;
                found = (int)n;
            }
        }
        x = found;
    } else {
        char* end;
        errno = 0;
        long n = strtol(v, &end, 10);
        if (end == v || *end) {
            // a trailing unit or fraction ("8 px", "1.5") is not a number we know
            return 0;
        }
        if (errno == ERANGE || n > 1000000000L || n < -1000000000L) n = n < 0 ? d->min : d->max;
        if (n < d->min || n > d->max) {
            if (info) info->clamped++;
        }
        x = (int)n;
    }
    settings_set(s, d, x);
    return 1;
}

void settings_from_text(Settings* s, const char* text, SettingsLoadInfo* info) {
    SettingsLoadInfo local = { 0 };
    if (!info) info = &local;
    memset(info, 0, sizeof(*info));
    char section[64] = "";
    AppLayout* layout = NULL;   // the slot a [screen.<hash>] section fills
    int in_layout = 0;
    if (!text) return;
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) text += 3;   // a byte order mark from an editor
    const char* p = text;
    while (p && *p) {
        const char* eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char line[1024];
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = eol ? eol + 1 : NULL;
        char* l = trim(line);
        if (!*l || *l == '#' || *l == ';') continue;
        if (*l == '[') {
            char* close = strchr(l, ']');
            if (!close) { info->bad++; continue; }
            *close = 0;
            snprintf(section, sizeof section, "%s", trim(l + 1));
            layout = NULL;
            in_layout = strncasecmp(section, "screen.", 7) == 0 && section[7];
            if (in_layout) {
                unsigned hash = layout_section_hash(section);
                if (hash) {
                    layout = (AppLayout*)settings_layout_find(s, hash);
                    if (!layout) {
                        Settings dflt;
                        settings_defaults(&dflt);
                        layout = layout_add(s, hash);
                        AppLayout base;
                        settings_layout_snapshot(&dflt, &base);
                        base.hash = hash;
                        *layout = base;
                    }
                }
            }
            continue;
        }
        char* eq = strchr(l, '=');
        if (!eq) { info->bad++; continue; }
        *eq = 0;
        char* key = trim(l);
        char* val = trim(eq + 1);
        size_t vl = strlen(val);
        if (vl >= 2 && val[0] == '"' && val[vl - 1] == '"') { val[vl - 1] = 0; val++; }
        if (in_layout) {
            // a remembered layout's key: never one of the settings in force
            int found = -1;
            for (int i = 0; i < LAYOUT_KEY_COUNT && found < 0; i++) {
                if (layout && streq_ci(LAYOUT_KEYS[i].key, key)) found = i;
            }
            if (found < 0) { info->unknown++; continue; }
            const SettingDesc* ld = layout_desc(found);
            int x = 0;
            if (ld->type == SET_BOOL) {
                if (!parse_bool(val, &x)) { info->bad++; continue; }
            } else {
                char* end;
                errno = 0;
                long n = strtol(val, &end, 10);
                if (end == val || *end) { info->bad++; continue; }
                if (errno == ERANGE || n > 1000000000L || n < -1000000000L) n = n < 0 ? ld->min : ld->max;
                if (n < ld->min || n > ld->max) info->clamped++;
                x = (int)n;
            }
            *layout_field(layout, found) = clamp_int(x, ld->min, ld->max);
            info->applied++;
            continue;
        }
        const SettingDesc* d = NULL;
        for (int i = 0; i < TABLE_COUNT && !d; i++) {
            if (streq_ci(TABLE[i].section, section) && streq_ci(TABLE[i].key, key)) d = &TABLE[i];
        }
        if (!d) d = settings_find(key);   // moved to another section: still the same setting
        if (!d) { info->unknown++; continue; }
        if (apply_value(s, d, val, info)) info->applied++;
        else info->bad++;
    }
    settings_clamp(s);
}

// ---- files ---------------------------------------------------------------------------------

const char* settings_default_path(char* buf, size_t cap) {
    const char* dir = getenv("MLXR_CONFIG_DIR");
    if (dir && *dir) {
        snprintf(buf, cap, "%s/config.ini", dir);
        return buf;
    }
    const char* home = getenv("HOME");
    snprintf(buf, cap, "%s/.config/moonlight-xr/config.ini", home && *home ? home : ".");
    return buf;
}

static int mkdir_p(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char* c = tmp + 1; *c; c++) {
        if (*c == '/') {
            *c = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -errno;
            *c = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -errno;
    return 0;
}

int settings_load(Settings* s, const char* path, SettingsLoadInfo* info) {
    char def[1024];
    if (!path) path = settings_default_path(def, sizeof def);
    FILE* f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    char* text = NULL;
    size_t len = 0, cap = 0;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (len + got + 1 > cap) {
            cap = (len + got + 1) * 2;
            char* nt = realloc(text, cap);
            if (!nt) { free(text); fclose(f); return -1; }
            text = nt;
        }
        memcpy(text + len, chunk, got);
        len += got;
    }
    int err = ferror(f);
    fclose(f);
    if (err) { free(text); return -1; }
    if (text) text[len] = 0;
    settings_from_text(s, text ? text : "", info);
    free(text);
    return 0;
}

int settings_save(const Settings* s, const char* path) {
    char def[1024];
    if (!path) path = settings_default_path(def, sizeof def);
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        int r = mkdir_p(dir);
        if (r) return r;
    }
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    char* text = settings_to_text(s);
    if (!text) return -ENOMEM;
    FILE* f = fopen(tmp, "wb");
    if (!f) { free(text); return -errno; }
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    free(text);
    if (!ok) { unlink(tmp); return -EIO; }
    if (rename(tmp, path) != 0) { int e = errno; unlink(tmp); return -e; }
    return 0;
}

// ---- derived ---------------------------------------------------------------------------------

void settings_resolution(const Settings* s, int* w, int* h) {
    int ww = 2560, hh = 1440;
    sscanf(SETTINGS_RESOLUTIONS[clamp_int(s->resolution, 0, SETTINGS_RESOLUTION_COUNT - 1)], "%dx%d", &ww, &hh);
    if (w) *w = ww;
    if (h) *h = hh;
}

int settings_fps_value(const Settings* s) {
    return SETTINGS_FPS[clamp_int(s->fps, 0, SETTINGS_FPS_COUNT - 1)];
}

// PreferenceConfiguration.getDefaultBitrate
int settings_default_bitrate_kbps(int width, int height, int fps) {
    // Owner's measurement on the Frame (main de55376): 1440p needs about 80 Mbps;
    // Moonlight's curve gives 40-49 Mbps there, which smeared fast motion.
    if (width == 2560 && height == 1440 && fps >= 60 && fps <= 90) return 80000;
    double frame_rate_factor = (fps <= 60 ? fps : (sqrt(fps / 60.0) * 60.0)) / 30.0;
    static const int pixel_vals[] = { 640 * 360, 854 * 480, 1280 * 720, 1920 * 1080, 2560 * 1440,
                                      3840 * 2160 };
    static const int factor_vals[] = { 1, 2, 5, 10, 20, 40 };
    int pixels = width * height;
    double res_factor;
    int count = 6;
    if (pixels <= pixel_vals[0]) {
        res_factor = factor_vals[0];
    } else if (pixels >= pixel_vals[count - 1]) {
        res_factor = factor_vals[count - 1];
    } else {
        int i = 1;
        while (pixels > pixel_vals[i]) i++;
        if (pixels == pixel_vals[i]) {
            res_factor = factor_vals[i];
        } else {
            res_factor = ((double)(pixels - pixel_vals[i - 1]) / (pixel_vals[i] - pixel_vals[i - 1])) *
                             (factor_vals[i] - factor_vals[i - 1]) + factor_vals[i - 1];
        }
    }
    // Native (lane refresh): above 90 fps at 1440p, the flat launcher's rule (twice Moonlight's
    // curve, to 500 kbps: 113000 at 120, 124000 at 144), so a faster stream never gets less than
    // the 90 fps figure
    if (width == 2560 && height == 1440 && fps > 90) {
        int kbps = (int)llround(4.0 * res_factor * frame_rate_factor) * 500;
        return kbps > 150000 ? 150000 : kbps;
    }
    return (int)llround(res_factor * frame_rate_factor) * 1000;
}

int settings_bitrate_kbps(const Settings* s) {
    if (s->bitrate_kbps > 0) return clamp_int(s->bitrate_kbps, 500, MLXR_BITRATE_MAX_KBPS);
    int w, h;
    settings_resolution(s, &w, &h);
    return settings_default_bitrate_kbps(w, h, settings_fps_value(s));
}

int settings_environment_id(const Settings* s) {
    int e = ENV_IDS[clamp_int(s->environment, 0, 5)];
    if (e == ENV_ID_NONE) return s->passthrough ? ENV_ID_PASSTHROUGH : ENV_ID_VOID;
    return e;
}

int settings_model_default_separation(const Settings* s) {
    return s->depth_source == 1 ? 5 : 6;   // MiDaS 5, ZipDepth 6 (MidasDepthSource)
}

int settings_separation_units(const Settings* s) {
    return s->separation >= 0 ? s->separation : settings_model_default_separation(s);
}

int settings_convergence_percent(const Settings* s) {
    return s->convergence >= 0 ? s->convergence : 50;
}

// ---- screen sliders -----------------------------------------------------------------------------

static float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

float settings_screen_slider(const Settings* s, int row) {
    switch (row) {
    case COG_SLIDER_DISTANCE: return clamp01(((float)s->screen_distance_cm / 100.0f - 0.2f) / (8.0f - 0.2f));
    case COG_SLIDER_HEIGHT: return clamp01(((float)s->screen_height_cm / 100.0f + 2.0f) / 4.0f);
    case COG_SLIDER_TILT: return clamp01(((float)s->screen_tilt_deg + 40.0f) / 80.0f);
    case COG_SLIDER_ROTATE: return clamp01(((float)s->screen_rotate_deg + 90.0f) / 180.0f);
    case COG_SLIDER_CURVE: return clamp01((float)s->screen_curve / 100.0f);
    case COG_SLIDER_SIZE: return clamp01(((float)s->screen_width_cm / 100.0f - 0.8f) / (8.0f - 0.8f));
    case COG_SLIDER_AIM_SENSITIVITY:
        return clamp01((float)(s->head_aim_sensitivity - HEAD_AIM_SENSITIVITY_MIN) /
                       (float)(HEAD_AIM_SENSITIVITY_MAX - HEAD_AIM_SENSITIVITY_MIN));
    case COG_SLIDER_AIM_DEADZONE:
        return clamp01((float)(s->head_aim_deadzone - HEAD_AIM_DEADZONE_MIN) /
                       (float)(HEAD_AIM_DEADZONE_MAX - HEAD_AIM_DEADZONE_MIN));
    default: return 0.0f;
    }
}

void settings_set_screen_slider(Settings* s, int row, float t) {
    t = clamp01(t);
    switch (row) {
    case COG_SLIDER_DISTANCE: s->screen_distance_cm = (int)lroundf((0.2f + t * 7.8f) * 100.0f); break;
    case COG_SLIDER_HEIGHT: s->screen_height_cm = (int)lroundf((-2.0f + t * 4.0f) * 100.0f); break;
    case COG_SLIDER_TILT: {
        float deg = -40.0f + t * 80.0f;
        if (fabsf(deg * 0.0174533f) < COG_TILT_SNAP_RAD) deg = 0.0f;
        s->screen_tilt_deg = (int)lroundf(deg);
        break;
    }
    case COG_SLIDER_ROTATE: {
        float deg = -90.0f + t * 180.0f;
        if (fabsf(deg * 0.0174533f) < COG_ROLL_SNAP_RAD) deg = 0.0f;
        s->screen_rotate_deg = (int)lroundf(deg);
        break;
    }
    case COG_SLIDER_CURVE: s->screen_curve = (int)lroundf(t * 100.0f); break;
    case COG_SLIDER_SIZE: s->screen_width_cm = (int)lroundf((0.8f + t * 7.2f) * 100.0f); break;
    case COG_SLIDER_AIM_SENSITIVITY:
        s->head_aim_sensitivity = HEAD_AIM_SENSITIVITY_MIN +
            (int)lroundf(t * (float)(HEAD_AIM_SENSITIVITY_MAX - HEAD_AIM_SENSITIVITY_MIN));
        break;
    case COG_SLIDER_AIM_DEADZONE:
        s->head_aim_deadzone = HEAD_AIM_DEADZONE_MIN +
            (int)lroundf(t * (float)(HEAD_AIM_DEADZONE_MAX - HEAD_AIM_DEADZONE_MIN));
        break;
    default: return;
    }
    settings_clamp(s);
}

void settings_reset_screen(Settings* s) {
    Settings d;
    settings_defaults(&d);
    s->screen_distance_cm = d.screen_distance_cm;
    s->screen_height_cm = d.screen_height_cm;
    s->screen_azimuth_deg = d.screen_azimuth_deg;
    s->screen_tilt_deg = d.screen_tilt_deg;
    s->screen_rotate_deg = d.screen_rotate_deg;
    s->screen_curve = d.screen_curve;
    s->screen_width_cm = d.screen_width_cm;
}

void settings_reset_3d(Settings* s) {
    s->separation = -1;
    s->convergence = -1;
}

void settings_reset_picture(Settings* s) {
    s->picture_brightness = PICTURE_BRIGHTNESS_DEFAULT;
    s->picture_contrast = PICTURE_CONTRAST_DEFAULT;
    s->picture_gamma = PICTURE_GAMMA_DEFAULT;
    s->picture_saturation = PICTURE_SATURATION_DEFAULT;
}

int* settings_picture_ptr(Settings* s, int row) {
    switch (row) {
    case PICTURE_BRIGHTNESS: return &s->picture_brightness;
    case PICTURE_CONTRAST: return &s->picture_contrast;
    case PICTURE_GAMMA: return &s->picture_gamma;
    case PICTURE_SATURATION: return &s->picture_saturation;
    default: return NULL;
    }
}

int settings_picture_value(const Settings* s, int row) {
    int* p = settings_picture_ptr((Settings*)s, row);
    return p ? *p : 0;
}

// ---- display tab ------------------------------------------------------------------------------

void session_from_settings(const Settings* s, SessionToggles* t) {
    t->head_aim = s->head_aim;
    t->pad_mode = 0;               // every session starts with the controllers as the pointer
    t->ray = s->show_ray;
    t->stereo_on = s->depth_source != 2 && s->stereo;   // "off" streams flat; native: the switch is remembered
}

int settings_option_cell(const Settings* s, const SessionToggles* t, int option) {
    switch (option) {
    case COG_OPTION_SHARPEN: return clamp_int(s->sharpening, 0, 2);
    case COG_OPTION_SUPERSAMPLE: return clamp_int(s->supersampling, 0, 2);
    case COG_OPTION_STATS: return s->stats;
    case COG_OPTION_HEAD_LOCK: return settings_head_lock_cell(s);
    case COG_OPTION_HEAD_AIM: return t->head_aim;
    case COG_OPTION_GAMEPAD: return t->pad_mode;
    case COG_OPTION_POINTER_SLEEP: return s->pointer_sleep;
    case COG_OPTION_RAY: return settings_pointer_cell(t->ray, s->pointer_dot);
    case COG_OPTION_CONTROLLERS: return s->controller_model;
    case COG_OPTION_CLICK_SOUND: return s->click_sound;
    case COG_OPTION_AMBILIGHT: return s->glow;
    case COG_OPTION_ROOM_LIGHT: return s->screen_light;
    case COG_OPTION_NOTICES: return clamp_int(s->notices, 0, 2);
    case COG_OPTION_CLICK_STEADY: return clamp_int(s->click_steadiness, 0, 2);
    default: return -1;
    }
}

int settings_pointer_cell(int ray, int dot) {
    return ray ? 2 : (dot ? 1 : 0);
}

void settings_pointer_from_cell(int cell, int* ray, int* dot) {
    *ray = cell >= 2;
    *dot = cell >= 1;
}
