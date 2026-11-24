// One settings struct for the whole client: the stream (host, app, resolution,
// frame rate, bitrate, codec, audio), every VR setting of the upstream
// "VR Settings" and "VR Debugging" tables, the in-headset panel's Display, 3D,
// Picture and Screen values, and each room's own Room tab values. Defaults
// are upstream's (PreferenceConfiguration.java, XrShared constants, the fork
// README's tables).
//
// Persisted as an INI at ~/.config/moonlight-xr/config.ini (directory
// overridden by $MLXR_CONFIG_DIR). Every setting is written; one at its
// default is written commented out ("#key = value"), so a default that
// changes in a later build reaches an old install, and only values somebody
// changed are active lines. Loading is tolerant: unknown keys, sections and
// malformed lines are ignored (and counted), values are range-clamped.
// GPLv3.
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <stddef.h>

#define UI_STR_MAX 128

// The highest bitrate a setting may ask for: the flat launcher's custom bitrate
// goes to 500 Mbps, so config.ini follows (it capped at 150 Mbps before). The
// default curve for a size and rate still tops out at 150000 (its 4K figure).
#define MLXR_BITRATE_MAX_KBPS 500000

typedef struct {
    int brightness;   // hundredths of the room as baked
    int glow;         // 0/1
    int light;        // hundredths of the screen light's gain
    int screen;       // whole percent of the room's screen anchor
} RoomSettings;

// Environment ids upstream stores (PreferenceConfiguration.VR_ENV_*)
#define ENV_ID_NONE -1
#define ENV_ID_PASSTHROUGH 0
#define ENV_ID_VOID 1
#define ENV_ID_HOME_THEATER 4
#define ENV_ID_GRAND_CINEMA 5
#define ENV_ID_SYNTHWAVE 6
// Native (1.3.3, issue #6): depth_source's default, ZipDepth Light (zipdepth-int8, index 8). 0 is ZipDepth Full.
#define DEPTH_SOURCE_FULL 0
#define DEPTH_SOURCE_LIGHT 8
#define DEPTH_SOURCE_DEFAULT DEPTH_SOURCE_LIGHT
// Native (1.3.4): [display] foveated, the Foveated rendering row. Only a choice somebody made is stored:
// auto (the default, written commented out) is what this release does when nobody chose, so a later
// release can change it for those who never touched the row. Auto is Off in 1.3.4 (owner's choice: it looked
// sharper and cost no measurable GPU at 3440x1440@144); FOVEATED_AUTO_IS_ON says what auto is.
#define FOVEATED_AUTO 0
#define FOVEATED_ON 1
#define FOVEATED_OFF 2
#define FOVEATED_AUTO_IS_ON 0
#define ROOM_COUNT 3   // the baked rooms, indexed 0 theater, 1 grand cinema, 2 synthwave

// What the Screen tab remembers per PC and app: where the screen hangs, how the
// head lock stands, and the 3D state. A hash of the PC and app names picks the
// slot; the file keeps one [screen.<hash>] section for each. Whole ints only,
// so a copy of the settings compares byte for byte.
#define APP_LAYOUT_MAX 32
typedef struct {
    unsigned hash;        // settings_app_hash of the PC and app; never 0 in a slot in use
    int distance_cm, height_cm, azimuth_deg, tilt_deg, rotate_deg, curve, width_cm;
    int head_locked, head_follow;
    int stereo, separation, convergence;
    int zoom, zoom_x, zoom_y;   // PLAN 4b.6: the part of the picture shown (zoom percent, pan centre percent)
    // Native (lane aspect-ratio): the section's size_cm as read (0 none), and once settings_screen_picture has run,
    // the size and real width it came in with (written back as they were while the size does not change)
    int size_cm, file_width_cm;
} AppLayout;

typedef struct Settings {
    // [stream]
    char host[UI_STR_MAX];
    char app[UI_STR_MAX];
    int aspect;            // native (lane aspect-ratio): SETTINGS_ASPECT_*, the list the resolution is picked from
    int resolution;        // index into SETTINGS_RESOLUTIONS (default 4: 2560x1440); SETTINGS_RESOLUTION_CUSTOM: custom_*
    int custom_width;      // native (lane aspect-ratio): the Custom size, 256..8192 each way
    int custom_height;
    int fps;               // index into SETTINGS_FPS (default 3: 90)
    int bitrate_kbps;      // 0 = follow resolution and frame rate (upstream's default curve)
    int codec;             // 0 auto, 1 AV1, 2 HEVC, 3 H.264
    int audio_config;      // 0 stereo, 1 5.1, 2 7.1
    int virtual_surround;  // HRTF virtual surround for headsets
    int frame_pacing;      // 0 latency, 1 balanced, 2 cap fps, 3 smoothness
    int network_path;      // 0 auto, 1 home (Wi-Fi), 2 adapter (Steam Frame Wireless Adapter), 3 usb (USB cable)
    int vpn_only_away;     // native (lane vpn-fallback): 1 = a VPN address of the PC is used only when it does not answer directly
    int doff_disconnect;   // native: seconds with the headset off before the stream disconnects (the host's app stays); 0 never
    int idle_disconnect;   // native: minutes without input before the same; 0 never
    int bitrate_wifi;      // native (lane wave4b-abr): start bitrate in kbps over Wi-Fi; 0 = the launcher's bitrate
    int bitrate_adapter;   // ... over the Steam Frame Wireless Adapter
    int bitrate_wired;     // ... over a wired network
    int bitrate_usb;       // ... over the USB cable
    int rfi_idr_ms;        // native (lane wave1-burst): at most one IDR frame request of the client's own per this many ms; 0 no cap
    int failover;          // native (lane wave1-failover): 0 off, 1 on: leave a link that keeps losing video for another one that measured clearly better
    int failover_burst_ms; // ... and how many ms of loss within 10 s make it leave (500..9000, 3000)

    // [vr] VR Settings table
    int pointer;           // Controllers as a mouse
    int pointer_sleep;
    int show_ray;
    int pointer_dot;       // native: the dot where the ray lands (the host draws its own cursor)
    int keyboard_layout;   // native: 0 compact (Moonlight XR's laser keyboard), 1 full (tenkeyless)
    int keyboard_keymap;   // native: 0 qwerty, 1 azerty, 2 qwertz: what the headset keyboard's keys say
    int keyboard_hold_ms;  // native: how long a modifier key is held to lock it on the full keyboard
    int controller_model;  // CONTROLLER_MODEL_*: 0 off, 1 the Frame's, 2 gun, 3 custom
    int head_aim;
    int head_aim_sensitivity;
    int head_aim_deadzone;
    int gamepad_toggle;    // PAD_SHORTCUT_* : 0 menu+grip, 1 sticks, 2 triggers+grips
    int flip_face_buttons; // swap A/B and X/Y on gamepad mode's pad
    int rumble_mix;        // PAD_RUMBLE_MIX_* : 0 both controllers get the stronger motor, 1 low motor left and high motor right
    int rumble_intensity;  // percent, 0..100: the scale on the host's rumble on the controllers
    int pad_grip;          // PAD_GRIP_* : what the grips are on gamepad mode's pad: 0 bumpers, 1 nothing, 2 stick clicks
    int pad_mouse_chord;   // native: PAD_MOUSE_CHORD_* : what turns a physical pad into the host's mouse: 0 hold Start, 1 hold Start and Back, 2 never
    int pad_mouse_scroll;  // native: PAD_MOUSE_SCROLL_* : the stick that scrolls while a pad is a mouse: 0 right, 1 left, 2 none
    int pad_arbitration;   // native: PAD_ARB_* : when a physical pad is used: 0 the Frame pad is retired and the controllers stay pointers, 1 both, 2 as 0 and the pointer sleeps
    int click_sound;
    int notices;           // native: NOTICES_ALL, IMPORTANT, OFF (the toast's informational notices)
    int click_steadiness;  // native: CLICK_STEADY_OFF, NORMAL, STEADY (how still the cursor is held around a click)
    int lightgun;          // native: LIGHTGUN_AUTO, ON, OFF (the lightgun pointer policy; auto follows the Gun model)
    int lightgun_reload;   // native: with the policy on, a trigger pulled off the picture is a right click
    int lightgun_recoil;   // native: with the policy on, a pulse in the controller on every shot
    int pad_gyro;          // native: PAD_GYRO_OFF, CONTROLLER, HEAD (what turns the gamepad's gyro)
    int pad_gyro_ratchet;  // native: PAD_GYRO_RATCHET_ALWAYS, STICK_TOUCH (when the gyro is sent)
    // Native (lane wave4-headinput): head and pointer input
    int head_look;         // native: PLAN 4.4, the head's turn as relative mouse motion in pointer mode (head locked or a room)
    int head_look_px_deg;  // native: head-look's pixels a degree (HEAD_AIM_SENSITIVITY_MIN..MAX)
    int opentrack;         // native: PLAN 4.5, the head pose to opentrack on the PC over UDP while streaming
    char opentrack_host[UI_STR_MAX]; // native: where it goes, "" the PC of the current stream
    int opentrack_port;    // native: its UDP port (OPENTRACK_PORT_MIN..MAX, default 4242)
    int pointer_source;    // native: PLAN 6.5, POINTER_SOURCE_CONTROLLER, HEAD, GAZE (what points)
    int head_dwell_ms;     // native: head Point mode's dwell click, 0 off, else DWELL_MS_MIN..MAX
    int assist_dot_size;   // native (lane a11y-2): the head and eye pointer's dot, percent (ACCESS_DOT_*)
    int assist_dwell_style;   // native (lane radial-dwell): DWELL_STYLE_SIMPLE or RADIAL (xr_radial.h)
    char eye_calibration[UI_STR_MAX];   // native (lane eye-calibration): xr_eyecal.h eyeCalFormat, "" none
    int assist_headset_button;   // native (lane a11y-2): the headset's side button clicks (evdev, grabbed)
    char assist_click_path[UI_STR_MAX];   // native (lane a11y-2): "<profile> <path>" of an OpenXR button that clicks, "" off
    int mouse_mode;        // native: PLAN 4b.10, MOUSE_MODE_ABSOLUTE, RELATIVE (the ray's point, or its movement)
    int back_as_guide;     // native: PLAN 4b.10, gamepad mode's Back (View) button is Guide
    int scroll_speed;      // native: the thumbstick's wheel, percent of 7.5 notches a second (SCROLL_SPEED_MIN..MAX)
    int gaze;
    int hand_tracking;
    int passthrough;
    // Native (1.3.4): I Understand on the passthrough sheets. Without it, passthrough draws no controller models
    // (Show controllers is greyed, "off in passthrough") and no glow; the void and the rooms are not affected.
    int passthrough_controllers_ok;
    int passthrough_glow_ok;
    int environment;       // 0 auto (void, or passthrough when passthrough is on), 1 passthrough, 2 void, 3 theater, 4 grand cinema, 5 synthwave
    int depth_source;      // 0 ZipDepth (Full), 1 MiDaS, 2 off (flat), 3..7 test patterns, 8 ZipDepth int8 (Light, native; the default, DEPTH_SOURCE_DEFAULT)
    int env_res;           // ENV_RES_LOW..ULTRA
    int hand_hint_seen;
    int first_run_shown;   // native: the first-run controls card has been shown (once per install)
    int pad_card;          // native: PADCARD_*: the gamepad card in the lap: 0 auto (up while a pad is in use), 1 on, 2 off
    int picture_brightness, picture_contrast, picture_gamma, picture_saturation;
    int bg_dim;            // native: Picture tab Background, percent of the room, cameras or void taken away (BG_DIM_)
    char void_color[32];   // native: the void's colour, "#rrggbb" (config only; black unless set)

    // [debug] VR Debugging table
    int depth_rate;
    int depth_threads;     // native (PLAN 5.5): the depth model's threads, DEPTH_THREADS_MIN..MAX
    int depth_debug;
    int eye_swap;
    int file_log;          // 0 off, 1 warnings and errors, 2 verbose

    // [display] the Display tab's persisted rows
    int head_locked;
    int sharpening;        // 0 off, 1 normal, 2 quality
    int supersampling;
    int picture_filter;    // native (lane text-clarity): the curved or 3D picture's footprint filter
    int chroma_from_luma;  // native (lane text-clarity): chroma sharpened from the luma in the warp
    int stats;
    int stats_style;       // native: what the Stats overlay shows: 0 full lines, 1 HUD, 2 Lite one-liner (xr_hud.h STATS_STYLE_)
    int glow;              // ambilight
    int glow_level;        // percent, five percent steps
    int glow_feather;      // native: Picture tab Glow edge, 0 off, 1 soft, 2 wide (GLOW_FEATHER_)
    int glow_behind;       // native (issue #7): the Glow edge row past those (GLOW_BEHIND_), with glow_feather off
    int glow_spread;       // native (issue #7): Backlight's reach, percent of its most (GLOW_SPREAD_)
    int glow_bezel;        // native (issue #7): and its black border, percent of the picture's longer side (GLOW_BEZEL_)
    int foveated;          // native (1.3.4): Foveated rendering, FOVEATED_AUTO (never chosen), ON or OFF
    int screen_light;      // room light
    int cog_tab;           // native: the cog's tab as it was last left (COG_TAB_*), where it opens next time

    // [3d]
    int separation;        // tenths of a percent of frame width; -1 = the depth model's own
    int convergence;       // percent; -1 = the depth model's own
    int stereo;            // native: the 3D switch as it was last left (0 off, 1 on)
    int eye_balance;       // native: percent -100..100, how the disparity is shared by the eyes (0 even; config only)
    int stereo_source;     // native (PLAN 4.3): STEREO_SOURCE_* (depth, sbs, top-bottom)
    int sbs_full;          // native (PLAN 4.3): the SBS or top-bottom frame holds a full size picture per eye
    int edge_sharpness;    // native (PLAN 5.1): the Edge setting, EDGE_SHARPNESS_MIN..MAX; 0 is upstream's edges
    int motion_reset;      // native (PLAN 5.4): percent luma change that restarts a depth texel's average; 0 off
    int depth_scale;       // PLAN 5.6, upstream vrDepthScale: percent gain on the depth's local detail (100 as made)
    int depth_global;      // native (PLAN 5.6): percent gain on the depth's overall shape around the convergence (100 as made)

    // [screen] where the picture hangs (what the Screen tab moves)
    int screen_distance_cm;
    int screen_height_cm;
    int screen_azimuth_deg;   // direction the picture sits in, degrees left of straight ahead
    int screen_tilt_deg;
    int screen_rotate_deg;    // clockwise positive as the viewer sees it
    int screen_curve;         // percent
    int screen_width_cm;
    // The bar's Bring here: the placement it goes back to from Desk, in the
    // placement's own units; back_width_cm 0 for none
    int screen_back_distance_cm, screen_back_height_cm, screen_back_azimuth_deg;
    int screen_back_tilt_deg, screen_back_rotate_deg, screen_back_curve, screen_back_width_cm;
    // Native (lane aspect-ratio): config.ini's width_cm keys are the picture's real width, as every version reads
    // them; size_cm and back_size_cm keep the Size (a 16:9 picture's width at the same height) beside them, and
    // size_shape the picture they were written for. In memory, once settings_screen_picture has run, width_cm and
    // back_width_cm (and each layout's) hold the Size, and size_cm what it came in as.
    int screen_size_cm, screen_back_size_cm;
    char screen_size_shape[24];
    int screen_file_width_cm, screen_back_file_width_cm;   // the real widths read, for writing back unchanged
    int screen_picture_w, screen_picture_h;   // the picture the sizes show at; 0 before settings_screen_picture
    int screen_loaded_w, screen_loaded_h;     // the picture they were read at
    int per_app;              // native: remember the layout per PC and app
    int angular_lock;         // native: pushing or pulling the held screen keeps its size in the view

    // [display] the Head locked row's third cell
    int head_follow;          // native: Follow (head_locked is then 0)
    int follow_leash_deg;     // native: how far the head turns before a followed screen comes after it
    int zoom;                 // native (PLAN 4b.6): zoom percent, 100..400; the part of the picture that fills the screen
    int zoom_x, zoom_y;       // native (PLAN 4b.6): where that part is centred, percent across and down the picture

    // [ui] native (lane wave6-i18n): the headset UI's language, read once at start. 0 auto (the
    // locale: LC_ALL, LC_MESSAGES, LANG), else 1 + the ui_strings.h language (1 English)
    int language;

    // [screen.<hash>] the layouts remembered per PC and app, the oldest first
    AppLayout layouts[APP_LAYOUT_MAX];
    int layout_count;

    // [rooms] per room
    RoomSettings room[ROOM_COUNT];
} Settings;

typedef enum { SET_BOOL, SET_INT, SET_ENUM, SET_STR } SettingType;

typedef struct {
    const char* section;
    const char* key;
    SettingType type;
    size_t offset;          // into Settings
    int min, max, def;      // ints; enums use 0..nnames-1
    const char* const* names;   // enum names as written in the file
    int nnames;
    const char* sdef;       // string default
    size_t scap;            // string capacity
    const char* doc;        // one line for the file's comment, with upstream's name
} SettingDesc;

// [stream] resolution's names: the six 16:9 sizes of earlier versions first, in their old order (a file that
// says a number means one of these six), then the other shapes' sizes, then "custom" (custom_width x
// custom_height). Every preset is a real monitor size, named by its height.
extern const char* const SETTINGS_RESOLUTIONS[];   // "640x360" .. "7680x2160", "custom"
extern const int SETTINGS_RESOLUTION_COUNT;        // presets and custom
extern const int SETTINGS_RESOLUTION_CUSTOM;       // the index of "custom"

// Native (lane aspect-ratio): [stream] aspect, the Aspect ratio row of the flat launcher's video settings
enum {
    SETTINGS_ASPECT_4_3,     // Standard
    SETTINGS_ASPECT_16_9,    // Widescreen (the default)
    SETTINGS_ASPECT_16_10,
    SETTINGS_ASPECT_21_9,    // Ultrawide
    SETTINGS_ASPECT_32_9,    // Super Ultrawide
    SETTINGS_ASPECT_COUNT
};
typedef struct {
    int aspect;              // SETTINGS_ASPECT_*
    int width, height;
    const char* label;       // the name by its height, "1440p" ("4K" for 3840x2160)
} SettingsResolution;
// SETTINGS_RESOLUTIONS' presets (custom has none), same order and index
extern const SettingsResolution SETTINGS_RESOLUTION_PRESETS[];

// The preset of exactly this size, or -1
int settings_resolution_find(int width, int height);
// The aspect a size is closest to (by width / height)
int settings_aspect_nearest(int width, int height);
// The presets of one aspect in their order (smallest first): their indexes into out, the count back
int settings_aspect_presets(int aspect, int* out, int cap);
// A size as the setting holds it: a preset (and its aspect) when one matches, else custom with this size
// (the aspect stays as it is: Custom is the last entry of the aspect's list)
void settings_set_resolution_size(Settings* s, int width, int height);
// A new aspect: the preset of that aspect nearest the current height (the lower one of two as near), passing
// over sizes that cannot stream (settings_resolution_limit, h264 as there) while one can. The aspect in force
// already changes nothing (a Custom size stays). The flat launcher's Aspect ratio row does the same.
void settings_set_aspect(Settings* s, int aspect, int h264);

// What the Frame and the PC can stream. Sizes past these are greyed out in the lists (a Custom size may still
// ask for them).
// H.264 streams at most 4096 wide (hosts' encoders and the level limits); wider needs HEVC (or AV1).
#define MLXR_H264_MAX_WIDTH 4096
// The Frame's decoder: VIDIOC_ENUM_FRAMESIZES on its qcom-iris-decoder said 128x128 to 8192x8192 for
// H.264, HEVC and VP9 (on the Frame, 2026-11-13; it has no AV1). The Vulkan import (video/vk_import.c) still
// checks each frame against the GPU's own maxExtent.
// TODO(headset): probe real HEVC streams 5120 and 7680 wide (5120x1440, 5120x2160, 7680x2160) on the Frame and
// grey out what fails; the decoder's macroblock rate may be lower than its frame size limit at 90 Hz and above.
#define MLXR_DECODE_MAX_WIDTH 8192
#define MLXR_DECODE_MAX_HEIGHT 8192
enum {
    SETTINGS_SIZE_OK,
    SETTINGS_SIZE_H264_WIDTH,   // over MLXR_H264_MAX_WIDTH with H.264 chosen
    SETTINGS_SIZE_DECODER,      // over the Frame's decoder
};
// Whether a size can stream: SETTINGS_SIZE_*. h264 is 1 when the codec setting is H.264 (Automatic and HEVC
// let the PC use HEVC past 4096).
int settings_resolution_limit(int width, int height, int h264);

// Native (lane aspect-ratio): the Size, kept so a version before 1.3.3 (which knows only width_cm, the picture's
// real width) and this one can take turns with config.ini, either way round, and the screen stays as it was.
// settings_screen_picture, once after the load, with the picture the stream shows (one eye's of a full-size pair):
// each width_cm becomes the Size. size_cm is taken when it still agrees with width_cm (within 1 cm) at the picture
// size_shape names, which is how a new aspect keeps the screen's height; else (an older version saved the file,
// dropping size_cm, or something changed width_cm) the Size is width_cm at this picture, so the screen is as wide
// as the older version showed it. Each Size is then held so its real width is one width_cm can say, 80..800 cm
// (at most floor(800 / k) for a picture k times a 16:9 one's width), here, in settings_clamp and when the picture
// changes, so a launch shows the screen the file says. The number of widths worked out that way back (0 when
// every size_cm held).
// After that, settings_to_text writes width_cm as the real width at the picture of the time (held to 80..800 cm,
// what older versions read; a width read is written back as it was while its Size has not changed), size_cm and
// size_shape. Until it runs, the widths go in and out of the text as they are.
int settings_screen_picture(Settings* s, int pictureWidth, int pictureHeight);
// The picture changed shape after settings_screen_picture: the Size stays (held at the new shape), the real
// width written follows it
void settings_screen_picture_now(Settings* s, int pictureWidth, int pictureHeight);
extern const int SETTINGS_FPS[];                   // 30 60 72 90 120 144
extern const int SETTINGS_FPS_COUNT;

const SettingDesc* settings_table(int* count);
// "section.key" or bare "key"
const SettingDesc* settings_find(const char* name);

void settings_defaults(Settings* s);
// Clamps every field into its range (and the rooms' screen sizes to 100 where
// a room is not resizable)
void settings_clamp(Settings* s);
int settings_equal(const Settings* a, const Settings* b);

int settings_get(const Settings* s, const SettingDesc* d);                 // int-valued kinds
void settings_set(Settings* s, const SettingDesc* d, int value);           // clamps
const char* settings_get_str(const Settings* s, const SettingDesc* d);
void settings_set_str(Settings* s, const SettingDesc* d, const char* v);
int settings_is_default(const Settings* s, const SettingDesc* d);

typedef struct {
    int applied;    // settings set from active lines
    int unknown;    // keys or sections no setting has
    int bad;        // lines or values that could not be read
    int clamped;    // numeric values pulled into range
} SettingsLoadInfo;

// Text form. settings_to_text returns a malloc'd NUL terminated string.
char* settings_to_text(const Settings* s);
// Applies active lines on top of what is in *s (so start from the defaults).
void settings_from_text(Settings* s, const char* text, SettingsLoadInfo* info);

// Paths and files. The directory is $MLXR_CONFIG_DIR, else $HOME/.config/moonlight-xr.
// path==NULL uses settings_default_path. load: 0 ok, 1 no such file (defaults
// kept), -1 read error. save: 0 ok, else -errno; written through a temporary
// file and renamed, directories created.
const char* settings_default_path(char* buf, size_t cap);
int settings_load(Settings* s, const char* path, SettingsLoadInfo* info);
int settings_save(const Settings* s, const char* path);

// ---- derived values ------------------------------------------------------------
void settings_resolution(const Settings* s, int* w, int* h);
int settings_fps_value(const Settings* s);
// Upstream's default curve for a resolution and rate (Moonlight Qt's table)
int settings_default_bitrate_kbps(int w, int h, int fps);
int settings_bitrate_kbps(const Settings* s);                 // 0 means default curve
// Native (lane wave4b-abr): the four per-path start bitrates in xr/core/xr_abr.h's order (Wi-Fi, adapter,
// wired, USB), 0 = none
void settings_abr_overrides(const Settings* s, int out[4]);
int settings_environment_id(const Settings* s);               // ENV_ID_*; 0 auto resolves with passthrough
int settings_room_index(int env_id);                          // 0..2, or -1 if not a room
int settings_room_resizable(int env_id);
int settings_separation_units(const Settings* s);             // resolved against the depth model
int settings_convergence_percent(const Settings* s);
int settings_model_default_separation(const Settings* s);

// Screen tab tracks: thumb place 0..1 from the persisted pose, and back.
// row is COG_SLIDER_* (0..7). set applies upstream's snaps (level tilt and
// rotate within a few degrees) and the stored units.
float settings_screen_slider(const Settings* s, int row);
void settings_set_screen_slider(Settings* s, int row, float t);
void settings_reset_screen(Settings* s);

// Per PC and app layouts. The hash is of the PC's name and the app's name, 0
// when there is no app (a file played locally), which is never remembered.
unsigned settings_app_hash(const char* host, const char* app);
const AppLayout* settings_layout_find(const Settings* s, unsigned hash);
// The layout the settings hold now, and a stored one put back as the settings
void settings_layout_snapshot(const Settings* s, AppLayout* out);
void settings_layout_apply(Settings* s, const AppLayout* l);
// Whether two layouts hold the same values, whatever their slots
int settings_layout_same(const AppLayout* a, const AppLayout* b);
// Stores a layout under a hash: added, or replaced where the hash has one,
// the oldest dropped when the file is full. Returns 1 when something changed.
int settings_layout_store(Settings* s, unsigned hash, const AppLayout* l);
// Drops the layout under a hash. Returns 1 when there was one.
int settings_layout_forget(Settings* s, unsigned hash);
// Native (lane wave6-i18n): the ui_strings.h language the [ui] language setting picks (auto: the
// process's locale); 0 is English
int settings_ui_language(const Settings* s);
// Native (1.3.4): whether foveated rendering is on: [display] foveated, auto as FOVEATED_AUTO_IS_ON
// Native (issue #7): the Glow edge row's value (GLOW_EDGE_), kept in two keys: glow_feather while it is soft or wide,
// else glow_behind. Setting it writes both, so a version without glow_behind reads Off for the new ones.
int settings_glow_edge(const Settings* s);
void settings_set_glow_edge(Settings* s, int edge);
int settings_foveated_on(const Settings* s);
// Native (1.3.4): whether Valve's fdm_injection layer is to be turned off for this session (it is read
// when the OpenXR instance is made). fdmEnv is $MLXR_FDM_LAYER, which wins when set: "0" off, else on.
// why (may be NULL) gets where the answer came from, for the log.
int settings_fdm_layer_off(const Settings* s, const char* fdmEnv, const char** why);
// The cell the Head locked row shows: 0 off, 1 on, 2 follow
int settings_head_lock_cell(const Settings* s);
void settings_reset_3d(Settings* s);
void settings_reset_picture(Settings* s);
// The void's colour as 0..1 red, green and blue as typed (sRGB). Reads "#rrggbb",
// "rrggbb", "#rgb" or "rgb"; anything else gives black and returns 0.
int settings_void_rgb(const Settings* s, float rgb[3]);
// The same parse on its own: 0 and rgb untouched when text is not a colour
int settings_parse_hex_rgb(const char* text, float rgb[3]);
int* settings_picture_ptr(Settings* s, int row);              // PICTURE_* row
int settings_picture_value(const Settings* s, int row);

// Display tab: the cell in force on each option row (COG_OPTION_*), 0-based,
// for the rows the Settings struct owns. session carries the bar-button rows
// that last only for the session (head aim, controllers pad/pointer, ray).
typedef struct {
    int head_aim;      // 0/1
    int pad_mode;      // 0 pointer, 1 gamepad
    int ray;           // 0/1
    int stereo_on;     // 0/1
} SessionToggles;
void session_from_settings(const Settings* s, SessionToggles* t);
int settings_option_cell(const Settings* s, const SessionToggles* t, int option);
// Native: the Display tab's ray row, three cells: 0 neither ray nor dot, 1 the
// dot alone, 2 the ray (with the dot). A ray without a dot shows as 2.
int settings_pointer_cell(int ray, int dot);
void settings_pointer_from_cell(int cell, int* ray, int* dot);

#endif
