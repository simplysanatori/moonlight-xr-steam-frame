// CPU rasterisers for every panel of the headset UI. Each draws into an RGBA8
// premultiplied buffer of the pixel size the caller picked (a UiCanvas over
// it) and, if asked, reports the hit regions input code needs, in panel UV
// (0..1, origin top left, the same u/v xr_layout's tests take). No OpenXR, no
// Vulkan: the orchestrator uploads the buffers into quad swapchains.
//
// Sizes: every panel has a native size (ui_panel_native_size), the texture
// size upstream draws it at. Draw at any size with that aspect ratio: pixel
// quantities (radii, strokes, type sizes) scale with canvas width / native
// width; positions are the fractions from xr_shared.h / xr_layout.h, so hit
// regions do not depend on the size drawn at.
//
// What each panel maps to upstream is in docs/dev/reports/ui.md. GPLv3.
#ifndef UI_PANELS_H
#define UI_PANELS_H

#include <stdint.h>

#include "canvas.h"
#include "settings.h"
#include "text.h"
#include "ui_strings.h"
#include "xr_cards.h"
#include "xr_hostkeys.h"
#include "xr_keys.h"
#include "xr_layout.h"
#include "xr_shared.h"

typedef enum {
    UI_PANEL_COG = 0,      // settings panel (tab chosen in UiView.cog_tab, Room tab in a room)
    UI_PANEL_PICKER,       // environment picker
    UI_PANEL_KEYBOARD,     // keyboard, sheet and held modifiers in UiView
    UI_PANEL_BAR,          // the bar row under the picture, all buttons and the move pill
    UI_PANEL_BUTTON_ATLAS, // the 12 face cells of xr_layout's BTN_ cells in one strip
    UI_PANEL_EXIT,         // exit prompt (hover lit from UiView.hover)
    UI_PANEL_HINT,         // hand lock hint
    UI_PANEL_KOFI,         // Ko-fi sheet
    UI_PANEL_REPORT,       // report sheet (see UiReport)
    UI_PANEL_TOAST,        // notice (see ui_draw_toast)
    UI_PANEL_CLOCK,        // time and battery strip over the settings panel
    UI_PANEL_PC_LIST,      // native launcher: computers
    UI_PANEL_APP_LIST,     // native launcher: apps of one computer
    UI_PANEL_HOST,         // the Host sheet: quick actions, macros, Send keys (see UiView.host_*)
    UI_PANEL_FIRSTRUN,     // the first-run controls card (UiView.settings gamepad_toggle, pad.style)
    UI_PANEL_PADCARD,      // the gamepad card: a schematic of the pad in use, lit from UiView.pad
    UI_PANEL_COUNT
} UiPanelId;

void ui_panel_native_size(UiPanelId id, int* w, int* h);
const char* ui_panel_name(UiPanelId id);

// One RGBA8 atlas holding every panel at its native size, so the whole UI is
// one swapchain (the SteamVR cap is 16 a session). A quad shows a panel
// through its XrSwapchainSubImage.imageRect. Draw a panel into its slot with
// ui_cv_sub (canvas.h); upload only the rectangles that changed.
#define UI_ATLAS_W 2048
#define UI_ATLAS_H 4048
typedef struct { int x, y, w, h; } UiRectPx;
UiRectPx ui_atlas_rect(UiPanelId id);

// ---- hit regions --------------------------------------------------------------------------
typedef enum {
    UI_HIT_NONE = 0,
    UI_HIT_TAB,         // a = COG_TAB_*; the tab bar slot
    UI_HIT_TRACK_DOWN,  // a = row (face-specific, see docs/dev/reports/ui.md); the step-down button at the track's left end
    UI_HIT_TRACK_RUN,   // a = row; the run between the buttons, a press jumps the thumb (cogRunPlace(u))
    UI_HIT_TRACK_UP,    // a = row
    UI_HIT_CELL,        // a = row, b = cell index within the row
    UI_HIT_BUTTON,      // a = UiButton
    UI_HIT_KEY,         // a = key index (row-major), b = key code (see xr_shared.h KB_CODE_*)
    UI_HIT_ENV_TILE,    // a = ENV_CELL_*
    UI_HIT_BAR_BUTTON,  // a = BAR_SLOT_*
    UI_HIT_BAR_MOVE,    // the move pill
    UI_HIT_LIST_ITEM,   // a = index into the whole list, b = 0
    UI_HIT_FIELD,       // report sheet: a = REPORT_ZONE_*
    UI_HIT_HOST_CELL    // Host sheet: a = cell (xr_hostkeys.h), b = XR_HOST_CELL_ kind
} UiHitKind;

typedef enum {
    UI_BTN_RESET = 1,        // Screen, 3D and Picture tabs
    UI_BTN_KOFI,             // About tab
    UI_BTN_REPORT,           // About tab
    UI_BTN_EXIT_CONFIRM,     // exit prompt (EXIT_ZONE_EXIT)
    UI_BTN_EXIT_CANCEL,      // exit prompt (EXIT_ZONE_CANCEL)
    UI_BTN_EXIT_DISCONNECT,  // exit prompt during a stream (EXIT_ZONE_DISCONNECT)
    UI_BTN_HINT_OK,          // hint (HINT_ZONE_OK)
    UI_BTN_HINT_NEVER,       // hint (HINT_ZONE_NEVER)
    UI_BTN_KOFI_CLOSE,       // Ko-fi sheet, "Not now" (KOFI_ZONE_CLOSE)
    UI_BTN_REPORT_CANCEL,    // report sheet (REPORT_ZONE_CANCEL)
    UI_BTN_REPORT_SEND,      // report sheet (REPORT_ZONE_SEND)
    UI_BTN_LIST_ADD,         // PC list: add a PC by address
    UI_BTN_LIST_PAGE_UP,
    UI_BTN_LIST_PAGE_DOWN,
    UI_BTN_LIST_BACK,        // app list: back to the PC list
    UI_BTN_KB_LAYOUT,        // Compact keyboard: the chip that flips to the Full one (needs UiView.kb_chip)
    UI_BTN_HOST_CLOSE,       // Host sheet (XR_HOST_ZONE_CLOSE)
    UI_BTN_HOST_CONFIRM,     // Host sheet's confirmation: send (XR_HOST_ZONE_CONFIRM_SEND)
    UI_BTN_HOST_CANCEL,      // Host sheet's confirmation: cancel (XR_HOST_ZONE_CONFIRM_CANCEL)
    UI_BTN_HOST_PCVR,        // Host sheet: Switch to PC VR (XR_HOST_ZONE_PCVR)
    UI_BTN_KOFI_OPEN,        // Ko-fi sheet, "Open page" (KOFI_ZONE_OPEN)
    UI_BTN_HOST_PAGE         // Host sheet: the header button between its pages (XR_HOST_ZONE_PAGE)
} UiButton;

typedef struct {
    int kind;                 // UiHitKind
    int a, b;
    float u0, v0, u1, v1;     // panel UV, [u0,u1] x [v0,v1]
    int enabled;              // 0 for a greyed row (draw dim; input should ignore it)
} UiHit;

#define UI_MAX_HITS 192
typedef struct {
    UiHit hit[UI_MAX_HITS];
    int count;
} UiHits;

void ui_hits_clear(UiHits* h);
// Index of the last hit containing (u,v), or -1 (later entries sit on top)
int ui_hits_at(const UiHits* h, float u, float v);
// Index of the first hit of this kind/a/b (b<0 matches any b), or -1
int ui_hits_find(const UiHits* h, int kind, int a, int b);

// ---- the view a panel is drawn from -----------------------------------------------------------
typedef struct {
    int kind, a, b;           // UiHit fields; kind UI_HIT_NONE means nothing hovered
} UiHover;

typedef struct {
    const Settings* settings;
    SessionToggles session;      // head aim / pad / ray / 3D for the session (session_from_settings)
    int cog_tab;                 // COG_TAB_*
    int room_env;                // ENV_ID_* of the room up (4, 5, 6), or -1 for no room
    int curve_ok;                // 1 if the curve row can act (a layer or mesh that curves)
    int stereo_ok;               // 1 if the 3D rows are live (a depth source is on)
    int env_selected;            // ENV_CELL_* ringed on the picker, or -1
    int bar_show_aim;            // head aim button on the bar (screen head locked, outside a room)
    int bar_show_stereo;         // 3D button on the bar (3D mode not off)
    int bar_show_here;           // native (lane bar-glare): Bring here on the bar (screen in the world, outside a room)
    int stereo_source;           // native (PLAN 4.3): the 3D source in force, STEREO_SOURCE_*
    UiHover hover;
    int kb_sheet;                // KB_STATE_*
    int kb_mods;                 // KB_MOD_* lit
    // Native: the Full keyboard (xr_kbfull.h). kb_layout is KB_LAYOUT_*; the
    // KBF_ masks are the modifiers lit for the next key and those locked;
    // kb_caps is Caps Lock. kb_chip draws and hit-tests the Compact
    // keyboard's "Full" chip (off by default so the Compact panel is
    // exactly the upstream one).
    int kb_layout;
    int kb_map;                  // KB_MAP_*: what the keys say (xr_kbfull.h)
    int kb_oneshot, kb_locked;
    int kb_caps;
    int kb_chip;
    // Native: "Send keys" is waiting for a combo (kb_chord), with the KBF_
    // modifiers lit so far, which the space bar says
    int kb_chord;
    int kb_chord_mods;
    // Native: the Host sheet (UI_PANEL_HOST). host_macros is how many of the
    // user's macros there are (names in host_macro, at most XR_HOST_MACRO_SLOTS
    // shown); host_confirm is the cell waiting for a yes, or -1 for none
    // (XR_HOST_CONFIRM_PCVR: the PC VR switch is asking)
    int host_macros;
    char host_macro[XR_HOST_MACRO_SLOTS][XR_MACRO_NAME];
    int host_confirm;
    char host_confirm_label[XR_HOST_CMD_NAME];
    // Native (lane wave4-apollo): the Host sheet's PC page. host_page is XR_HOST_PAGE_*; host_info what
    // the host offers (the commands' names are its cells); host_confirm_cmd says the question is about
    // a command (Run) and not keys (Send). kb_compose: "Send text to PC" has the keyboard, and
    // kb_compose_text is the end of what is typed, for the space bar
    int host_page;
    XrHostInfo host_info;
    int host_confirm_cmd;
    int kb_compose;
    char kb_compose_text[64];
    const char* version;         // About tab ("0.1.0"), NULL for none
    const char* commit;          // may be NULL
    const char* log_path;        // shown shortened; NULL means the log is off
    const char* kofi_url;        // default https://ko-fi.com/moonlightxr
    // Native (M4): what the native client cannot act on yet. Display tab rows
    // (bit per COG_OPTION_*, bit COG_DISPLAY_SLIDER_ROW for the glow level)
    // drawn greyed with later_reason (NULL: "comes in a later update"); the 3D
    // tab's reason when stereo_ok is 0 (NULL: upstream's "3D is off in
    // settings"); picker tiles at or past env_live_cells drawn dimmed, "Later".
    unsigned display_later;
    const char* later_reason;
    const char* stereo_reason;
    int env_live_cells;
    // The exit prompt's question (NULL: upstream's "Exit the stream?"); the
    // native launcher asks "Quit Moonlight XR for Frame?" while no stream is up
    const char* exit_question;
    // Native: 1 draws the prompt as Quit app / Disconnect / Cancel (a stream is
    // up), 0 as Exit / Cancel
    int exit_disconnect;
    // Native (lane wave3-cards): the pad in use as the pad card draws it, and what the first-run
    // card words its gamepad lines for (pad.style; Xbox until a pad has been used)
    PadLive pad;
} UiView;

// Fills the view's defaults from settings (session toggles included)
void ui_view_init(UiView* v, const Settings* s);

// Draws a panel that needs only the view. hits may be NULL. The canvas should
// be cleared (ui_cv_clear) first; the panels paint onto transparent.
void ui_draw_panel(UiPanelId id, UiCanvas* cv, const UiView* v, UiHits* hits);

// ---- panels with their own data -------------------------------------------------------------------
typedef struct {
    const char* note;
    int focus;                   // REPORT_ZONE_NOTE / 0
    int hover;                   // REPORT_ZONE_*
    int can_send;
} UiReport;
void ui_draw_report(UiCanvas* cv, const UiReport* r, UiHits* hits);

// A line, and a quieter one under it when more is not empty; a path is cut
// from the middle when it does not fit, anything else from the end.
void ui_draw_toast(UiCanvas* cv, const char* text, const char* more, int more_is_path);
void ui_draw_clock(UiCanvas* cv, const char* line);
// Native (lane wave3-hud): the clock strip with the headset battery (percent 0..100, -1 for none; charging draws
// a bolt on it) and the slow connection pill
typedef struct {
    const char* time;
    int battery;
    int charging;
    int slow;
} UiClock;
void ui_draw_clock_strip(UiCanvas* cv, const UiClock* c);

typedef enum { UI_PC_OFFLINE = 0, UI_PC_ONLINE = 1, UI_PC_REFRESHING = 2 } UiPcState;
typedef struct {
    char name[64];
    int state;                   // UiPcState
    int paired;
    int game_running;            // a session is running on it
} UiPcItem;
#define UI_LIST_ROWS 5
typedef struct {
    const UiPcItem* items;
    int count;
    int page;                    // 0-based page of UI_LIST_ROWS
    int selected;                // item index with the focus ring, or -1
    UiHover hover;
} UiPcList;
void ui_draw_pc_list(UiCanvas* cv, const UiPcList* l, UiHits* hits);

typedef struct {
    char name[96];
    int running;                 // this app is the running session
} UiAppItem;
typedef struct {
    const char* pc_name;         // title
    const UiAppItem* items;
    int count;
    int page;
    int selected;
    int loading;                 // show "Refreshing apps…"
    int failed;                  // show "Failed to get app list"
    UiHover hover;
} UiAppList;
void ui_draw_app_list(UiCanvas* cv, const UiAppList* l, UiHits* hits);

// What a pad of this style (PADSTYLE_*) calls its Start button: Menu, Options, + or Start
const char* ui_pad_start_name(int style);
// And its Back button: View, Create, - or Back
const char* ui_pad_back_name(int style);

// ---- helpers for the orchestrator -----------------------------------------------------------------------
// Keyboard layout, shared by every sheet: how many keys, each key's rectangle
// in panel UV, and the code and label on a sheet (NULL label: a blank, no key).
int ui_kb_key_count(void);
void ui_kb_key_rect(int index, float rect[4]);
int ui_kb_key_code(int sheet, int index);
const char* ui_kb_key_label(int sheet, int index);   // English label or the character
// The same on a key map (KB_MAP_*: QWERTY, AZERTY, QWERTZ); the two above are QWERTY's
int ui_kb_map_key_code(int map, int sheet, int index);
const char* ui_kb_map_key_label(int map, int sheet, int index);
// The Compact keyboard's chip (in the half key of room beside the home row)
void ui_kb_layout_chip_rect(float rect[4]);

// The Display tab's rows, the 3D presets and so on are described by xr_shared.h's
// COG_* tables; these answer the questions a panel needs of the settings.
int ui_cog_face(const UiView* v);   // COG_TAB_* , or COG_FACE_ROOM when the Room tab is up
#define COG_FACE_ROOM COG_TAB_COUNT
// Rows of a face, whatever kind (xr_ui.c cogTabRowCount)
int ui_cog_row_count(int face);
// 1 for a track row, 0 for a row of cells
int ui_cog_row_is_track(int face, int row);
int ui_cog_row_cells(int face, int row);
int ui_cog_row_live(const UiView* v, int face, int row);

// Preset separations (units of the track) for the 3D tab, in cell order
void ui_presets_for(const Settings* s, int presets[COG_PRESET_CELLS]);

// Where assets (room thumbnails, Ko-fi QR, settings icon) are read from;
// defaults to $MLXR_ASSETS_DIR, then the compiled-in assets directory. Frees
// anything cached.
void ui_panels_set_assets_dir(const char* dir);
void ui_panels_shutdown(void);

#endif
