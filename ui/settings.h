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
#define ROOM_COUNT 3   // the baked rooms, indexed 0 theater, 1 grand cinema, 2 synthwave

typedef struct Settings {
    // [stream]
    char host[UI_STR_MAX];
    char app[UI_STR_MAX];
    int resolution;        // index into SETTINGS_RESOLUTIONS (default 4: 2560x1440)
    int fps;               // index into SETTINGS_FPS (default 3: 90)
    int bitrate_kbps;      // 0 = follow resolution and frame rate (upstream's default curve)
    int codec;             // 0 auto, 1 AV1, 2 HEVC, 3 H.264
    int audio_config;      // 0 stereo, 1 5.1, 2 7.1
    int virtual_surround;  // HRTF virtual surround for headsets
    int frame_pacing;      // 0 latency, 1 balanced, 2 cap fps, 3 smoothness
    int auto_desktop;      // open the host's app named Desktop without the app list
    int check_updates;
    char language[16];     // "default" or a language code (English only for now)

    // [vr] VR Settings table
    int vr_mode;           // Stream in VR
    int pointer;           // Controllers as a mouse
    int pointer_sleep;
    int show_ray;
    int pointer_dot;       // native: the dot where the ray lands (the host draws its own cursor)
    int controller_model;
    int head_aim;
    int head_aim_sensitivity;
    int head_aim_deadzone;
    int gamepad_toggle;    // PAD_SHORTCUT_* : 0 menu+grip, 1 sticks, 2 triggers+grips
    int flip_face_buttons; // swap A/B and X/Y on gamepad mode's pad
    int click_sound;
    int gaze;
    int hand_tracking;
    int passthrough;
    int environment;       // 0 auto (void, or passthrough when passthrough is on), 1 passthrough, 2 void, 3 theater, 4 grand cinema, 5 synthwave
    int depth_source;      // 0 ZipDepth, 1 MiDaS, 2 off (flat), 3.. test patterns
    int env_res;           // ENV_RES_LOW..ULTRA
    int doff_grace;
    int hand_hint_seen;
    int picture_brightness, picture_contrast, picture_gamma, picture_saturation;

    // [debug] VR Debugging table
    int depth_rate;
    int depth_debug;
    int eye_swap;
    int file_log;          // 0 off, 1 warnings and errors, 2 verbose

    // [display] the Display tab's persisted rows
    int head_locked;
    int sharpening;        // 0 off, 1 normal, 2 quality
    int supersampling;
    int stats;
    int glow;              // ambilight
    int glow_level;        // percent, five percent steps
    int screen_light;      // room light

    // [3d]
    int separation;        // tenths of a percent of frame width; -1 = the depth model's own
    int convergence;       // percent; -1 = the depth model's own

    // [screen] where the picture hangs (what the Screen tab moves)
    int screen_distance_cm;
    int screen_height_cm;
    int screen_azimuth_deg;   // direction the picture sits in, degrees left of straight ahead
    int screen_tilt_deg;
    int screen_rotate_deg;    // clockwise positive as the viewer sees it
    int screen_curve;         // percent
    int screen_width_cm;

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

extern const char* const SETTINGS_RESOLUTIONS[];   // "640x360" .. "3840x2160"
extern const int SETTINGS_RESOLUTION_COUNT;
extern const int SETTINGS_FPS[];                   // 30 60 72 90 120
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
void settings_reset_3d(Settings* s);
void settings_reset_picture(Settings* s);
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
