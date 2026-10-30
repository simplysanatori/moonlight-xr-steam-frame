// The strings table: every word the panels draw, English only for v1, keyed
// by the upstream resource name (app/src/main/res/values/strings.xml) so a
// translation can be added later as a second table of the same ids. New
// native-only strings (PC and app lists) are marked "native" in strings.c.
// GPLv3.
#ifndef UI_STRINGS_H
#define UI_STRINGS_H

#define UI_STRINGS(X) \
    /* settings panel tabs and rows (vr_panel_*) */ \
    X(TAB_SCREEN, "vr_panel_tab_screen", "Screen") \
    X(TAB_DISPLAY, "vr_panel_tab_display", "Display") \
    X(TAB_3D, "vr_panel_3d", "3D") \
    X(TAB_PICTURE, "vr_panel_tab_picture", "Picture") \
    X(TAB_ABOUT, "vr_panel_tab_about", "About") \
    X(TAB_ROOM, "vr_panel_tab_room", "Room") \
    X(ROW_DISTANCE, "vr_panel_distance", "Distance") \
    X(ROW_HEIGHT, "vr_panel_height", "Height") \
    X(ROW_TILT, "vr_panel_tilt", "Tilt") \
    X(ROW_ROTATE, "vr_panel_rotate", "Rotate") \
    X(ROW_CURVE, "vr_panel_curve", "Curve") \
    X(ROW_SIZE, "vr_panel_size", "Size") \
    X(ROW_AIM_SENSITIVITY, "vr_panel_aim_sensitivity", "Aim sensitivity") \
    X(ROW_AIM_DEAD_ZONE, "vr_panel_aim_dead_zone", "Aim dead zone") \
    X(NEEDS_HEAD_LOCK, "vr_panel_needs_head_lock", "needs Head locked") \
    X(NOT_IN_ROOM, "vr_panel_not_in_room", "not in a room") \
    X(RESET, "vr_panel_reset", "Reset") \
    X(ROW_BRIGHTNESS, "vr_panel_brightness", "Brightness") \
    X(ROW_GLOW, "vr_panel_glow", "Glow") \
    X(ROW_SCREEN_LIGHT, "vr_panel_screen_light", "Screen light") \
    X(ROW_LIGHT_LEVEL, "vr_panel_light_level", "Light level") \
    X(OFF, "vr_panel_off", "Off") \
    X(ON, "vr_panel_on", "On") \
    X(FIXED_SIZE, "vr_panel_fixed_size", "This room's screen is a fixed size") \
    X(ROW_SHARPEN, "vr_panel_sharpen", "Sharpen") \
    X(ROW_SUPERSAMPLE, "vr_panel_supersample", "Supersample") \
    X(ROW_STATS, "vr_panel_stats", "Stats") \
    X(ROW_HEAD_LOCKED, "vr_panel_head_locked", "Head locked") \
    X(ROW_HEAD_AIM, "vr_panel_head_aim", "Head aim") \
    X(ROW_MODE, "vr_panel_controllers", "Mode") \
    X(ROW_POINTER_SLEEP, "vr_panel_pointer_sleep", "Pointer sleep") \
    X(ROW_RAY, "vr_panel_ray", "Ray") \
    X(ROW_SHOW_CONTROLLERS, "vr_panel_controller_model", "Show controllers") \
    X(ROW_CLICK_SOUND, "vr_panel_click_sound", "Click sound") \
    X(NORMAL, "vr_panel_normal", "Normal") \
    X(QUALITY, "vr_panel_quality", "Quality") \
    X(POINTER, "vr_panel_pointer", "Pointer") \
    X(GAMEPAD, "vr_panel_gamepad", "Gamepad") \
    X(ROW_GLOW_LEVEL, "vr_panel_glow_level", "Glow level") \
    X(ROW_NOTICES, "vr_panel_notices", "Notices") \
    X(ROW_CLICK_STEADY, "vr_panel_click_steadiness", "Click steadiness") \
    X(STEADY, "vr_panel_steady", "Steady") \
    X(NOTICE_CELL_ALL, "vr_panel_notices_all", "All") \
    X(NOTICE_CELL_IMPORTANT, "vr_panel_notices_important", "Important") \
    X(SHORTCUT_MENU_GRIP, "vr_panel_shortcut_menu_grip", "Shortcut: left View + grip") \
    X(SHORTCUT_STICKS, "vr_panel_shortcut_sticks", "Shortcut: both thumbsticks") \
    X(SHORTCUT_TRIGGERS_GRIPS, "vr_panel_shortcut_triggers_grips", "Shortcut: triggers + grips") \
    X(ROW_PRESET, "vr_panel_preset", "Preset")     X(PRESET_DESK, "native_preset_desk", "Desk")     X(PRESET_COUCH, "native_preset_couch", "Couch")     X(PRESET_CINEMA, "native_preset_cinema", "Cinema")     X(BRING_HERE, "native_bring_here", "Bring here")     X(ROW_PER_APP, "native_per_app", "Remember per app")     X(FORGET, "native_forget", "Forget")     X(ROW_ANGULAR_LOCK, "native_angular_lock", "Angular-size lock")     X(FOLLOW, "native_follow", "Follow") \
    X(COMFORT, "vr_panel_comfort", "Comfort") \
    X(BALANCED, "vr_panel_balanced", "Balanced") \
    X(STRONG, "vr_panel_strong", "Strong") \
    X(ROW_DEPTH, "vr_panel_depth", "Depth") \
    X(ROW_CONVERGENCE, "vr_panel_convergence", "Convergence") \
    X(HARDER_ON_EYES, "vr_panel_harder_on_eyes", "harder on the eyes") \
    X(THREE_D_OFF_IN_SETTINGS, "vr_panel_3d_off_in_settings", "3D is off in settings") \
    X(ROW_CONTRAST, "vr_panel_contrast", "Contrast") \
    X(ROW_GAMMA, "vr_panel_gamma", "Gamma") \
    X(ROW_SATURATION, "vr_panel_saturation", "Saturation") \
    X(ROW_BACKGROUND, "native_background", "Background")     X(ROW_GLOW_EDGE, "native_glow_edge", "Glow edge")     X(FEATHER_SOFT, "native_feather_soft", "Soft")     X(FEATHER_WIDE, "native_feather_wide", "Wide") \
    X(VERSION, "vr_panel_version", "Version %s, commit %s") \
    X(VERSION_NO_COMMIT, "vr_panel_version_no_commit", "Version %s") \
    X(LOG_FILE, "vr_panel_log_file", "Log file: %s") \
    X(LOG_FILE_OFF, "vr_panel_log_file_off", "Log file: off") \
    X(ABOUT_BUILT_BY, "vr_panel_built_by", "Built by simplysanatori") \
    X(ABOUT_BASED_ON, "vr_panel_based_on", "Based on Moonlight XR by Sean Gilleece and Moonlight") \
    X(REPORT_SENDS, "vr_panel_report_sends", "Saves a note on the headset for a bug report") \
    X(ROOMS, "vr_panel_rooms", "Rooms") \
    /* environments */ \
    X(ENV_PASSTHROUGH, "vr_environment_passthrough", "Passthrough") \
    X(ENV_VOID, "vr_environment_void", "Black void") \
    X(ENV_HOME_THEATER, "vr_environment_home_theater", "Home Theater") \
    X(ENV_GRAND_CINEMA, "vr_environment_grand_cinema", "Grand Cinema") \
    X(ENV_SYNTHWAVE, "vr_environment_synthwave", "Synthwave") \
    /* keyboard */ \
    X(KEY_SHIFT, "vr_key_shift", "Shift") \
    X(KEY_SPACE, "vr_key_space", "space") \
    X(KEY_ENTER, "vr_key_enter", "Enter") \
    X(KEY_HIDE, "vr_key_hide", "Hide") \
    X(KEY_CTRL, "vr_key_ctrl", "Ctrl") \
    X(KEY_ALT, "vr_key_alt", "Alt") \
    X(KEY_WIN, "vr_key_win", "Win") \
    X(NATIVE_KEY_FULL, "native_key_full", "Full") \
    X(NATIVE_KEY_COMPACT, "native_key_compact", "Compact") \
    /* exit prompt, hints, sheets */ \
    X(EXIT_QUESTION, "vr_exit_question", "Exit the stream?") \
    X(EXIT, "vr_exit", "Exit") \
    X(EXIT_QUIT_APP, "vr_exit_quit_app", "Quit app") \
    X(EXIT_DISCONNECT, "vr_exit_disconnect", "Disconnect") \
    X(EXIT_DISCONNECT_HINT, "vr_exit_disconnect_hint", "Disconnect leaves the app running on the PC") \
    X(CANCEL, "cancel", "Cancel") \
    X(OK, "ok", "OK") \
    X(HAND_HINT_TITLE, "vr_hand_hint_title", "Hand tracking is on.") \
    X(HAND_HINT_BODY, "vr_hand_hint_body", "Pinch your thumb and index finger three times quickly to lock your hands out, so a stray pinch does not click; the same triple pinch unlocks them.") \
    X(HAND_HINT_NEVER, "vr_hand_hint_never", "Don't show this again") \
    X(KOFI_TITLE, "vr_kofi_title", "Support Moonlight XR on Ko-fi") \
    X(KOFI_SCAN, "vr_kofi_scan", "Scan it with your phone.") \
    X(KOFI_FREE, "vr_kofi_free", "Optional: Moonlight XR is free and stays free.") \
    X(KOFI_CLOSE, "vr_kofi_close", "Close") \
    /* the Host sheet (PLAN 4b.1) */ \
    X(HOST_TITLE, "native_host_title", "Host") \
    X(HOST_SEND_KEYS, "native_host_send_keys", "Send keys…") \
    X(HOST_SEND_KEYS_HINT, "native_host_send_keys_hint", "type a combo") \
    X(HOST_SEND_KEYS_SHORT, "native_host_send_keys_short", "Send keys") \
    X(HOST_CLOSE, "native_host_close", "Close") \
    X(HOST_CONFIRM_Q, "native_host_confirm_q", "Send %s to the PC?") \
    X(HOST_CONFIRM_SEND, "native_host_confirm_send", "Send") \
    X(HOST_NO_MACROS, "native_host_no_macros", "Your own buttons go in macros.ini") \
    X(HOST_PCVR, "native_host_pcvr", "PC VR") \
    X(HOST_PCVR_Q, "native_host_pcvr_q", "Switch to PC VR? This stream ends and Steam Link takes over.") \
    X(HOST_PCVR_GO, "native_host_pcvr_go", "Switch") \
    X(HOST_HINT_ESC, "native_host_hint_esc", "Escape") \
    X(HOST_HINT_WIN, "native_host_hint_win", "Start menu") \
    X(HOST_HINT_DESKTOP, "native_host_hint_desktop", "Show desktop") \
    X(HOST_HINT_FULLSCREEN, "native_host_hint_fullscreen", "Full screen") \
    X(HOST_HINT_SWITCHER, "native_host_hint_switcher", "App switcher") \
    X(HOST_HINT_STEAM, "native_host_hint_steam", "Steam overlay") \
    X(HOST_HINT_GAMEBAR, "native_host_hint_gamebar", "Game bar") \
    X(HOST_HINT_MOVE, "native_host_hint_move", "Move window") \
    X(HOST_HINT_PASTE, "native_host_hint_paste", "Paste") \
    X(HOST_HINT_TASKMGR, "native_host_hint_taskmgr", "Task manager") \
    X(HOST_HINT_CLOSE_WINDOW, "native_host_hint_close_window", "Close window") \
    X(REPORT_TITLE, "title_bug_report", "Report a problem") \
    X(REPORT_NOTE_LABEL, "bug_report_message_hint", "What happened?") \
    X(REPORT_NOTE_HINT, "vr_report_note_hint", "What were you doing when it went wrong?") \
    X(REPORT_GOES, "vr_report_sends", "Save keeps your note on the Frame in ~/Games/MoonlightXRNative/reports. Save and zip also makes the masked zip there, to attach to an issue on GitHub. Nothing is sent from the headset.") \
    X(REPORT_SEND, "bug_report_send_direct", "Save") \
    X(NATIVE_REPORT_ZIP, "native_report_zip", "Save and zip") \
    X(NATIVE_REPORT_ZIP_DONE, "native_report_zip_done", "Report zip saved") \
    X(NATIVE_REPORT_ZIP_FAIL, "native_report_zip_fail", "The report zip could not be made") \
    X(NATIVE_REPORT_ZIP_BUSY, "native_report_zip_busy", "The report zip is still being made") \
    X(NATIVE_SLOW_CONN, "native_slow_conn", "Slow connection") \
    /* toasts */ \
    X(TOAST_RATE, "vr_toast_rate", "Display %d Hz") \
    X(TOAST_HANDS_LOCKED, "vr_toast_hands_locked", "Hands locked") \
    X(TOAST_HANDS_UNLOCKED, "vr_toast_hands_unlocked", "Hands unlocked") \
    X(TOAST_3D_OFF, "vr_toast_3d_off", "3D off") \
    X(TOAST_3D_ON, "vr_toast_3d_on", "3D on") \
    X(TOAST_HEAD_AIM_ON, "vr_toast_head_aim_on", "Head aim on") \
    X(TOAST_HEAD_AIM_ON_MORE, "vr_toast_head_aim_on_more", "Turning your head moves the mouse") \
    X(TOAST_HEAD_AIM_OFF, "vr_toast_head_aim_off", "Head aim off") \
    X(TOAST_GAMEPAD_MODE, "vr_toast_gamepad_mode", "Gamepad mode") \
    X(TOAST_BACK_MENU_GRIP, "vr_toast_gamepad_back_menu_grip", "Hold the left View button and left grip to go back to the pointer") \
    X(TOAST_BACK_STICKS, "vr_toast_gamepad_back_sticks", "Press both thumbsticks to go back to the pointer") \
    X(TOAST_BACK_TRIGGERS_GRIPS, "vr_toast_gamepad_back_triggers_grips", "Squeeze both triggers and both grips to go back to the pointer") \
    X(TOAST_POINTER_MODE, "vr_toast_pointer_mode", "Pointer mode") \
    X(BATTERY, "vr_battery", "Battery %d%%") \
    /* PC list and app list (upstream: pcview_*, applist_*) */ \
    X(PC_ONLINE, "pcview_menu_header_online", "Online") \
    X(PC_OFFLINE, "pcview_menu_header_offline", "Offline") \
    X(PC_REFRESHING, "pcview_menu_header_unknown", "Refreshing") \
    X(PC_PAIR, "pcview_menu_pair_pc", "Pair with PC") \
    X(PC_VIEW_APPS, "pcview_menu_app_list", "View All Apps") \
    X(PC_ADD, "title_add_pc", "Add PC Manually") \
    X(APP_RESUME, "applist_menu_resume", "Resume Session") \
    X(APP_QUIT, "applist_menu_quit", "Quit Session") \
    X(APP_LIST_TITLE, "applist_refresh_title", "App List") \
    X(APP_REFRESHING, "applist_refresh_msg", "Refreshing apps…") \
    X(APP_ERROR, "applist_refresh_error_msg", "Failed to get app list") \
    X(APP_CONNECTING, "applist_connect_msg", "Connecting to PC…") \
    /* native only: no upstream key, written for this port */ \
    X(NATIVE_PC_LIST_TITLE, "native_pc_list_title", "Computers") \
    X(NATIVE_PC_NOT_PAIRED, "native_pc_not_paired", "Not paired") \
    X(NATIVE_PC_PAIRED, "native_pc_paired", "Paired") \
    X(NATIVE_PC_RUNNING, "native_pc_running", "Running") \
    X(NATIVE_PC_NONE, "native_pc_none", "No computers found yet. Make sure Sunshine is running on your PC.") \
    X(NATIVE_APP_NONE, "native_app_none", "This PC has no apps.") \
    X(NATIVE_BACK, "native_back", "Back") \
    X(NATIVE_PAGE_UP, "native_page_up", "Previous page") \
    X(NATIVE_PAGE_DOWN, "native_page_down", "Next page") \
    X(NATIVE_PAGE_OF, "native_page_of", "%d / %d") \
    X(NATIVE_APP_RUNNING, "native_app_running", "Running") \
    X(NATIVE_LATER, "native_later", "comes in a later update") \
    X(NATIVE_3D_LATER, "native_3d_later", "3D comes with the depth model in a later update") \
    X(NATIVE_3D_NO_MODEL, "native_3d_no_model", "3D needs the depth model, which is not installed") \
    X(NATIVE_3D_FAILED, "native_3d_failed", "3D is off: the depth model did not load") \
    X(NATIVE_3D_LOADING, "native_3d_loading", "3D: loading the depth model") \
    X(NATIVE_TILE_LATER, "native_tile_later", "Later") \
    X(NATIVE_DOT, "native_dot", "Dot") \
    X(NATIVE_RAY, "native_ray", "Ray") \
    X(MODEL_FRAME, "native_model_frame", "Frame") \
    X(MODEL_GENERIC, "native_model_generic", "Generic") \
    X(MODEL_GUN, "native_model_gun", "Gun") \
    X(ROW_3D_SOURCE, "native_3d_source", "3D source") \
    X(SOURCE_DEPTH, "native_3d_source_depth", "Depth") \
    X(SOURCE_SBS, "native_3d_source_sbs", "SBS") \
    X(SOURCE_TB, "native_3d_source_tb", "Top-bottom") \
    X(SBS_NO_DEPTH, "native_3d_sbs_no_depth", "the PC draws the depth") \
    X(NATIVE_EXIT_APP, "native_exit_app", "Quit Moonlight XR for Frame?") \
    X(NATIVE_PAIR_LATER, "native_pair_later", "Pairing from the headset comes later: pair with mlxr-cli") \
    X(NATIVE_NOT_PAIRED, "native_not_paired", "%s is not paired: pair it with mlxr-cli") \
    X(NATIVE_OFFLINE_WAKE, "native_offline_wake", "%s is offline: sending Wake-on-LAN") \
    X(NATIVE_STARTING, "native_starting", "Starting %s") \
    X(NATIVE_STREAM_FAILED, "native_stream_failed", "The stream did not start") \
    X(NATIVE_STREAM_ENDED, "native_stream_ended", "The stream ended") \
    X(NATIVE_QUITTING, "native_quitting", "Ending the session on %s") \
    X(NATIVE_DISCONNECTED, "native_disconnected", "Disconnected: Start+Select+L1+R1") \
    X(NATIVE_DISCONNECTED_KEYS, "native_disconnected_keys", "Disconnected: Ctrl+Alt+Shift+Q") \
    X(NATIVE_DISCONNECTED_BAR, "native_disconnected_bar", "Disconnected: the app is still running on the PC") \
    X(NATIVE_RECONNECTING, "native_reconnecting", "Connection lost, reconnecting (%d/%d)…") \
    X(NATIVE_DISCONNECTED_DOFF, "native_disconnected_doff", "Disconnected while the headset was off: the app is still running on the PC") \
    X(NATIVE_DISCONNECTED_IDLE, "native_disconnected_idle", "Disconnected after no input for a while: the app is still running on the PC") \
    X(NATIVE_PCVR, "native_pcvr", "Switching to PC VR: the app is still running on the PC") \
    X(NATIVE_PCVR_NEEDS_LAUNCHER, "native_pcvr_needs_launcher", "PC VR needs the Moonlight XR launcher: start the stream from it")

typedef enum {
#define X(id, key, en) UIS_##id,
    UI_STRINGS(X)
#undef X
    UIS_COUNT
} UiStr;

// English text for an id ("" for one out of range). Never NULL.
const char* ui_str(UiStr id);
// The upstream resource name an id maps to
const char* ui_str_key(UiStr id);

#endif
