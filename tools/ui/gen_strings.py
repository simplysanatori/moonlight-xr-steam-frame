#!/usr/bin/env python3
"""Translations for the headset UI: ui/ui_strings_i18n.inc from upstream's strings.xml files.

Moonlight XR (github.com/Gilleece/moonlight-android-xr, GPLv3) ships its Android strings in
app/src/main/res/values/strings.xml and a translation per language in values-<lang>/strings.xml.
The headset UI's strings (ui/ui_strings.h) keep upstream's resource names, so this script takes
each language's text for the keys both share and writes a C table, one array per language indexed
by the UIS_* ids, that ui/ui_strings.c compiles in. The generated file is committed, so a build
needs no upstream checkout. A string with no translation (one written for this port, or one whose
English has changed its meaning here) is left out and falls back to English at run time.

    tools/ui/gen_strings.py --res <upstream>/app/src/main/res --upstream-rev <sha>
        regenerates ui/ui_strings_i18n.inc and the list in docs/dev/i18n.md
    tools/ui/gen_strings.py --verify
        no upstream needed: every key of ui/ui_strings.h is in MAP below and the committed
        table was made from this MAP (the tests run this)

Standard library only. GPLv3.
"""
import argparse
import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

# ---- the mapping table ----------------------------------------------------------------------------
# Every key of ui/ui_strings.h, in its order, and where its translations come from:
#   SAME            upstream has the key, with the same meaning: its translations are used
#   "<key>"         upstream has the same text under another key (an alias): that key's translations
#   NATIVE          written for this port, nothing upstream to take: English in every language
#   DRIFT           upstream has the key but the fork's English says something else now: English
# A key added to ui_strings.h needs a line here (tests/ui/test_i18n_gen.py fails without one).
# A NATIVE or DRIFT line needs no regeneration; a SAME or alias line does (see docs/dev/i18n.md).
SAME = "="
NATIVE = None
DRIFT = "drift"

MAP = [
    # settings panel tabs and rows
    ("vr_panel_tab_screen", SAME),
    ("vr_panel_tab_display", SAME),
    ("vr_panel_3d", SAME),
    ("vr_panel_tab_picture", SAME),
    ("vr_panel_tab_about", SAME),
    ("vr_panel_tab_room", SAME),
    ("vr_panel_distance", SAME),
    ("vr_panel_height", SAME),
    ("vr_panel_tilt", SAME),
    ("vr_panel_rotate", SAME),
    ("vr_panel_curve", SAME),
    ("vr_panel_size", SAME),
    ("vr_panel_aim_sensitivity", SAME),
    ("vr_panel_aim_dead_zone", SAME),
    ("vr_panel_needs_head_lock", SAME),
    ("vr_panel_not_in_room", SAME),
    ("vr_panel_reset", SAME),
    ("vr_panel_brightness", SAME),
    ("vr_panel_glow", SAME),
    ("vr_panel_screen_light", SAME),
    ("vr_panel_light_level", SAME),
    ("vr_panel_off", SAME),
    ("vr_panel_on", SAME),
    ("vr_panel_fixed_size", SAME),
    ("vr_panel_sharpen", SAME),
    ("vr_panel_supersample", SAME),
    ("vr_panel_stats", SAME),
    ("vr_panel_head_locked", SAME),
    ("vr_panel_head_aim", SAME),
    # the fork calls the row "Mode" (its cells are Pointer and Gamepad); upstream's "Controllers"
    # names the same row
    ("vr_panel_controllers", SAME),
    ("vr_panel_pointer_sleep", SAME),
    ("vr_panel_ray", SAME),
    # "Show controllers" here, "Controller model" upstream: the same row (which model is drawn)
    ("vr_panel_controller_model", SAME),
    ("vr_panel_click_sound", SAME),
    ("vr_panel_normal", SAME),
    ("vr_panel_quality", SAME),
    ("vr_panel_pointer", SAME),
    ("vr_panel_gamepad", SAME),
    ("vr_panel_glow_level", SAME),
    ("vr_panel_notices", NATIVE),
    ("vr_panel_click_steadiness", NATIVE),
    ("vr_panel_steady", NATIVE),
    ("vr_panel_notices_all", NATIVE),
    ("vr_panel_notices_important", NATIVE),
    # upstream says "left menu + grip"; the Frame's button is View, so its translations name the
    # wrong button
    ("vr_panel_shortcut_menu_grip", DRIFT),
    ("vr_panel_shortcut_sticks", SAME),
    ("vr_panel_shortcut_triggers_grips", SAME),
    ("vr_panel_preset", SAME),
    ("native_preset_desk", NATIVE),
    ("native_preset_couch", NATIVE),
    ("native_preset_cinema", NATIVE),
    ("native_bring_here", NATIVE),
    ("native_per_app", NATIVE),
    ("native_forget", NATIVE),
    ("native_angular_lock", NATIVE),
    ("native_follow", NATIVE),
    ("vr_panel_comfort", SAME),
    ("vr_panel_balanced", SAME),
    ("vr_panel_strong", SAME),
    ("vr_panel_depth", SAME),
    ("vr_panel_convergence", SAME),
    ("vr_panel_3d_off_in_settings", SAME),
    ("vr_panel_contrast", SAME),
    ("vr_panel_gamma", SAME),
    ("vr_panel_saturation", SAME),
    ("native_background", NATIVE),
    ("native_glow_edge", NATIVE),
    ("native_feather_soft", NATIVE),
    ("native_feather_wide", NATIVE),
    ("vr_panel_version", SAME),
    ("vr_panel_version_no_commit", SAME),
    ("vr_panel_log_file", SAME),
    ("vr_panel_log_file_off", SAME),
    ("vr_panel_built_by", NATIVE),
    ("vr_panel_based_on", NATIVE),
    # upstream: "Sends a note with the headset..."; here the note is saved, nothing is sent
    ("vr_panel_report_sends", DRIFT),
    ("vr_panel_rooms", SAME),
    # environments
    ("vr_environment_passthrough", SAME),
    ("vr_environment_void", SAME),
    ("vr_environment_home_theater", SAME),
    ("vr_environment_grand_cinema", SAME),
    ("vr_environment_synthwave", SAME),
    # keyboard
    ("vr_key_shift", SAME),
    ("vr_key_space", SAME),
    ("vr_key_enter", SAME),
    ("vr_key_hide", SAME),
    ("vr_key_ctrl", SAME),
    ("vr_key_alt", SAME),
    ("vr_key_win", SAME),
    ("native_key_full", NATIVE),
    ("native_key_compact", NATIVE),
    # exit prompt, hints, sheets
    ("vr_exit_question", SAME),
    ("vr_exit", SAME),
    ("vr_exit_quit_app", NATIVE),
    ("vr_exit_disconnect", NATIVE),
    ("vr_exit_disconnect_hint", NATIVE),
    ("cancel", "applist_menu_cancel"),
    ("ok", NATIVE),
    ("vr_hand_hint_title", SAME),
    ("vr_hand_hint_body", SAME),
    ("vr_hand_hint_never", SAME),
    ("vr_kofi_title", DRIFT),
    ("vr_kofi_close", DRIFT),
    # the Host sheet
    ("native_host_title", NATIVE),
    ("native_host_send_keys", NATIVE),
    ("native_host_send_keys_hint", NATIVE),
    ("native_host_send_keys_short", NATIVE),
    ("native_host_close", "vr_kofi_close"),
    ("native_host_confirm_q", NATIVE),
    ("native_host_confirm_send", "bug_report_send_direct"),
    ("native_host_hint_esc", NATIVE),
    ("native_host_hint_win", NATIVE),
    ("native_host_hint_desktop", NATIVE),
    ("native_host_hint_fullscreen", NATIVE),
    ("native_host_hint_switcher", NATIVE),
    ("native_host_hint_steam", NATIVE),
    ("native_host_hint_gamebar", NATIVE),
    ("native_host_hint_move", NATIVE),
    ("native_host_hint_paste", NATIVE),
    ("native_host_hint_taskmgr", NATIVE),
    ("native_host_hint_close_window", NATIVE),
    # the report sheet
    ("title_bug_report", SAME),
    ("bug_report_message_hint", SAME),
    ("vr_report_note_hint", SAME),
    # upstream describes what is sent; here the note is saved on the Frame
    ("vr_report_sends", DRIFT),
    # upstream "Send", here "Save"
    ("bug_report_send_direct", DRIFT),
    ("native_report_zip", NATIVE),
    ("native_report_zip_done", NATIVE),
    ("native_report_zip_fail", NATIVE),
    ("native_report_zip_busy", NATIVE),
    ("native_slow_conn", NATIVE),
    # toasts
    ("vr_toast_rate", SAME),
    ("vr_toast_hands_locked", SAME),
    ("vr_toast_hands_unlocked", SAME),
    ("vr_toast_3d_off", SAME),
    ("vr_toast_3d_on", SAME),
    ("vr_toast_head_aim_on", SAME),
    ("vr_toast_head_aim_on_more", SAME),
    ("vr_toast_head_aim_off", SAME),
    ("vr_toast_gamepad_mode", SAME),
    # upstream "left menu button", the Frame's is View
    ("vr_toast_gamepad_back_menu_grip", DRIFT),
    ("vr_toast_gamepad_back_sticks", SAME),
    ("vr_toast_gamepad_back_triggers_grips", SAME),
    ("vr_toast_pointer_mode", SAME),
    ("vr_battery", SAME),
    # PC list and app list
    ("pcview_menu_header_online", SAME),
    ("pcview_menu_header_offline", SAME),
    ("pcview_menu_header_unknown", SAME),
    ("pcview_menu_pair_pc", SAME),
    ("pcview_menu_app_list", SAME),
    ("title_add_pc", SAME),
    ("applist_menu_resume", SAME),
    ("applist_menu_quit", SAME),
    ("applist_refresh_title", SAME),
    ("applist_refresh_msg", SAME),
    ("applist_refresh_error_msg", SAME),
    ("applist_connect_msg", SAME),
    # native only
    ("native_pc_list_title", NATIVE),
    ("native_pc_not_paired", NATIVE),
    ("native_pc_paired", NATIVE),
    ("native_pc_running", NATIVE),
    ("native_pc_none", NATIVE),
    ("native_app_none", NATIVE),
    ("native_back", "title_settings_back"),
    ("native_page_up", NATIVE),
    ("native_page_down", NATIVE),
    ("native_page_of", NATIVE),
    ("native_app_running", NATIVE),
    ("native_later", NATIVE),
    ("native_3d_later", NATIVE),
    ("native_3d_no_model", NATIVE),
    ("native_3d_failed", NATIVE),
    ("native_3d_loading", NATIVE),
    ("native_3d_source", NATIVE),
    ("native_3d_source_depth", NATIVE),
    ("native_3d_source_sbs", NATIVE),
    ("native_3d_source_tb", NATIVE),
    ("native_3d_sbs_no_depth", NATIVE),
    ("native_row_edge", NATIVE),
    ("native_row_depth_detail", NATIVE),
    ("native_row_depth_shape", NATIVE),
    ("native_row_motion_reset", NATIVE),
    ("native_row_eye_balance", NATIVE),
    ("native_tile_later", NATIVE),
    ("native_dot", NATIVE),
    ("native_ray", "vr_panel_ray"),
    ("native_model_frame", NATIVE),
    ("native_model_gun", NATIVE),
    ("native_exit_app", NATIVE),
    ("native_pair_later", NATIVE),
    ("native_not_paired", NATIVE),
    ("native_offline_wake", NATIVE),
    ("native_starting", NATIVE),
    ("native_stream_failed", NATIVE),
    ("native_stream_ended", NATIVE),
    ("native_quitting", NATIVE),
    ("native_disconnected", NATIVE),
    ("native_disconnected_keys", NATIVE),
    ("native_disconnected_bar", NATIVE),
    ("native_reconnecting", NATIVE),
    ("native_disconnected_doff", NATIVE),
    ("native_disconnected_idle", NATIVE),
    # native (lane a11y-tab): the Accessibility tab
    ("native_tab_access", NATIVE),
    ("native_row_pointer_source", NATIVE),
    ("native_cell_controller", NATIVE),
    ("native_cell_head", NATIVE),
    ("native_cell_eyes", NATIVE),
    ("native_row_dwell", NATIVE),
    ("native_dwell_06", NATIVE),
    ("native_dwell_09", NATIVE),
    ("native_dwell_12", NATIVE),
    ("native_dwell_15", NATIVE),
    ("native_row_head_look", NATIVE),
    ("native_row_back_guide", NATIVE),
    ("native_row_mouse_mode", NATIVE),
    ("native_cell_absolute", NATIVE),
    ("native_cell_relative", NATIVE),
    ("native_needs_head_pointer", NATIVE),
    ("native_eyes_ready", NATIVE),
    ("native_eyes_waiting", NATIVE),
    ("native_eyes_not_asked", NATIVE),
    ("native_eyes_unavailable", NATIVE),
    ("native_access_help", NATIVE),
    ("native_access_help_eyes", NATIVE),
]

# Keys of lanes not merged into main yet, all written for this port: listed so that their merges
# need no MAP line. A key here that ui_strings.h does not have is not an error.
PENDING = [
    # wave3-cards: the first-run and gamepad cards
    "native_firstrun_title", "native_firstrun_bar_l", "native_firstrun_bar", "native_firstrun_cog_l",
    "native_firstrun_cog", "native_firstrun_kb_l", "native_firstrun_kb", "native_firstrun_pad_l",
    "native_firstrun_pad_menu_grip", "native_firstrun_pad_sticks", "native_firstrun_pad_triggers_grips",
    "native_firstrun_host_l", "native_firstrun_host", "native_firstrun_keys_l", "native_firstrun_keys",
    "native_firstrun_mouse_l", "native_firstrun_mouse", "native_firstrun_mouse_both",
    "native_firstrun_mouse_off", "native_padcard_xbox", "native_padcard_playstation",
    "native_padcard_nintendo", "native_padcard_steam", "native_padcard_generic", "native_padcard_gyro",
    "native_padcard_touchpad", "native_padcard_hint", "native_padcard_start_menu",
    "native_padcard_start_options", "native_padcard_start_plus", "native_padcard_start_generic",
    "native_padcard_back_view", "native_padcard_back_create", "native_padcard_back_minus",
    "native_padcard_back_generic",
    # wave4-apollo: the Host sheet's PC page
    "native_host_page_pc", "native_host_page_back", "native_host_pc_title", "native_host_paste",
    "native_host_copy", "native_host_confirm_run_q", "native_host_confirm_run", "native_host_pc_not_apollo",
    "native_host_pc_no_permission", "native_host_pc_no_commands", "native_host_compose_prompt",
    # wave6-kofi
    "vr_kofi_open",
    # wave6-splash
    "native_splash_connecting", "native_splash_connecting_nopc", "native_splash_reconnecting",
]
MAP += [(k, NATIVE) for k in PENDING]

# ---- the languages ------------------------------------------------------------------------------------
# (setting code, upstream values-<dir>, name in its own language, script). The setting code is
# what config.ini's [ui] language takes; the order is the launcher's list. Scripts other than
# Latin, Cyrillic and Greek need a system font (ui/text.h ui_font_find_cjk).
LANGS = [
    ("cs", "cs", "Čeština", "LATIN"),
    ("da", "da", "Dansk", "LATIN"),
    ("de", "de", "Deutsch", "LATIN"),
    ("es", "es", "Español", "LATIN"),
    ("fi", "fi", "Suomi", "LATIN"),
    ("fr", "fr", "Français", "LATIN"),
    ("it", "it", "Italiano", "LATIN"),
    ("nb", "nb-rNO", "Norsk bokmål", "LATIN"),
    ("nl", "nl", "Nederlands", "LATIN"),
    ("pl", "pl", "Polski", "LATIN"),
    ("pt-BR", "pt-rBR", "Português (Brasil)", "LATIN"),
    ("sv", "sv", "Svenska", "LATIN"),
    ("tr", "tr", "Türkçe", "LATIN"),
    ("ru", "ru", "Русский", "CYRILLIC"),
    ("uk", "uk", "Українська", "CYRILLIC"),
    ("ja", "ja", "日本語", "CJK_JA"),
    ("ko", "ko", "한국어", "CJK_KO"),
    ("zh-CN", "zh-rCN", "简体中文", "CJK_SC"),
    ("zh-TW", "zh-rTW", "繁體中文", "CJK_TC"),
]
# Upstream directories left out, and why (the docs list them)
SKIPPED = {
    "ckb": "no strings translated upstream",
    "fa": "no strings translated upstream",
    "eo": "4 of the headset's keys translated upstream",
    "lv": "2 of the headset's keys translated upstream",
    "iw": "Hebrew is written right to left; the headset's text drawing has no bidi",
    "ta": "Tamil needs text shaping, which the headset's text drawing does not do",
    # upstream translated only its Android screens for these, and 13 of the headset's strings
    # (10 for Romanian): the panels would be English with a word here and there
    "bg": "13 of the headset's strings translated upstream",
    "el": "13 of the headset's strings translated upstream",
    "hu": "13 of the headset's strings translated upstream",
    "in": "13 of the headset's strings translated upstream",
    "pt": "13 of the headset's strings translated upstream; pt-rBR is complete",
    "ro": "10 of the headset's strings translated upstream",
    "vi": "13 of the headset's strings translated upstream",
}

# Translations of the native and drift strings, made for this port by people (never by a
# machine): tools/ui/native/values-<dir>/strings.xml, upstream's format, keys from MAP's NATIVE
# and DRIFT lines only. None exist yet; docs/dev/i18n.md lists what each language lacks.
NATIVE_DIR = os.path.join(HERE, "native")
INC_PATH = os.path.join(ROOT, "ui", "ui_strings_i18n.inc")
HEADER_PATH = os.path.join(ROOT, "ui", "ui_strings.h")
DOC_PATH = os.path.join(ROOT, "docs", "dev", "i18n.md")
DOC_BEGIN = "<!-- gen_strings.py: begin -->"
DOC_END = "<!-- gen_strings.py: end -->"


# ---- inputs -------------------------------------------------------------------------------------------
def read_header(path):
    """[(ID, key, english)] from the UI_STRINGS X-macro"""
    text = open(path, encoding="utf-8").read()
    out = []
    for m in re.finditer(r'X\(\s*(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)', text):
        en = m.group(3).encode("latin-1", "backslashreplace").decode("unicode_escape") \
            if "\\" in m.group(3) else m.group(3)
        out.append((m.group(1), m.group(2), en))
    return out


def android_text(elem):
    """A <string>'s text as Android shows it: markup dropped, quotes and escapes resolved"""
    raw = "".join(elem.itertext())
    out = []
    quoted = False
    i = 0
    pending_space = False
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            n = raw[i + 1]
            i += 2
            if pending_space:
                out.append(" ")
                pending_space = False
            if n == "n":
                out.append("\n")
            elif n == "t":
                out.append("\t")
            elif n == "u" and re.match(r"[0-9a-fA-F]{4}", raw[i:i + 4]):
                out.append(chr(int(raw[i:i + 4], 16)))
                i += 4
            else:
                out.append(n)   # \' \" \\ \@ \?
            continue
        i += 1
        if c == '"':
            quoted = not quoted
            continue
        if c.isspace() and not quoted:
            pending_space = bool(out)
            continue
        if pending_space:
            out.append(" ")
            pending_space = False
        out.append(c)
    # a quoted string keeps its spaces on Android; a label here is centred, so they go
    return "".join(out).strip(" ")


def read_strings(path):
    root = ET.parse(path).getroot()
    out = {}
    for e in root:
        if e.tag == "string" and e.get("name") and e.get("translatable") != "false":
            out[e.get("name")] = android_text(e)
    return out


# ---- formats ------------------------------------------------------------------------------------------
FMT = re.compile(r"%(?:(\d+)\$)?([-#+ 0,(]*\d*(?:\.\d+)?)([a-zA-Z%])")


def conversions(s):
    """[(position or None, flags, type)] of a printf string, %% left out; None if malformed"""
    out = []
    pos = 0
    while True:
        i = s.find("%", pos)
        if i < 0:
            return out
        m = FMT.match(s, i)
        if not m:
            return None
        if m.group(3) != "%":
            out.append((m.group(1), m.group(2), m.group(3)))
        pos = m.end()


def c_format(text, english):
    """The translation as a C format with the English one's conversions, or (None, why)"""
    en = conversions(english)
    if "%" not in english:
        # not a format: a % in a translation would be one if a caller ever formatted it
        return (None, "has a % where the English has none") if "%" in text else (text, None)
    tr = conversions(text)
    if tr is None:
        return None, "malformed format"
    numbered = [c[0] for c in tr]
    if any(numbered) and not all(numbered):
        return None, "mixes numbered and plain conversions"
    if all(numbered) and numbered and [int(n) for n in numbered] != list(range(1, len(tr) + 1)):
        return None, "reorders the values"
    if [c[2] for c in tr] != [c[2] for c in en]:
        return None, "conversions differ from the English"
    return FMT.sub(lambda m: "%" + m.group(2) + m.group(3), text), None


# ---- output -------------------------------------------------------------------------------------------
def c_string(s):
    out = []
    prev = ""
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "?" and prev == "?":
            out.append("\\?")   # no trigraphs
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
        prev = ch
    return '"' + "".join(out) + '"'


def map_hash():
    """What the translations depend on in MAP: the keys that take upstream text and where from"""
    h = hashlib.sha256()
    for key, src in MAP:
        if src not in (NATIVE, DRIFT):
            h.update(("%s=%s\n" % (key, key if src == SAME else src)).encode())
    return h.hexdigest()[:16]


def en_hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:8]


def check_map(header, strict=True):
    """Problems with MAP against the header's keys, as messages"""
    keys = [k for _, k, _ in header]
    mapped = [k for k, _ in MAP]
    errs = []
    for k in keys:
        if k not in mapped:
            errs.append("%s is in ui/ui_strings.h but not in gen_strings.py's MAP: add a line "
                        "(NATIVE for a string written for this port)" % k)
    for k in mapped:
        if k not in keys and k not in PENDING:
            errs.append("%s is in gen_strings.py's MAP but not in ui/ui_strings.h" % k)
    dup = {k for k in mapped if mapped.count(k) > 1}
    for k in sorted(dup):
        errs.append("%s is in MAP twice" % k)
    return errs


def generate(res, header, upstream_rev=None, strict=True, native_dir=NATIVE_DIR):
    """Returns (inc text, report) for upstream's res directory"""
    ids = {k: i for i, k, _ in header}
    english = {k: e for _, k, e in header}
    srcmap = dict(MAP)
    up_en = read_strings(os.path.join(res, "values", "strings.xml"))
    translatable = []
    for key, src in MAP:
        if key not in ids or src in (NATIVE, DRIFT):
            continue
        upkey = key if src == SAME else src
        if upkey not in up_en:
            if strict:
                raise SystemExit("upstream has no %s (for %s)" % (upkey, key))
            continue
        translatable.append((key, upkey))
    report = {"translatable": [k for k, _ in translatable],
              "native": [k for k, s in MAP if k in ids and s is NATIVE],
              "drift": [k for k, s in MAP if k in ids and s == DRIFT],
              "langs": []}
    lines = []
    lines.append("// Generated by tools/ui/gen_strings.py: the headset UI's translations, taken from Moonlight")
    lines.append("// XR's (github.com/Gilleece/moonlight-android-xr) app/src/main/res/values-*/strings.xml%s." %
                 (" at " + upstream_rev if upstream_rev else ""))
    lines.append("// Do not edit; run the script (docs/dev/i18n.md). Translations by upstream's translators")
    lines.append("// (CREDITS.md). GPLv3.")
    lines.append("// map: %s" % map_hash())
    lines.append("// The English each translated key had when this was made (--verify fails when one changes):")
    for key, _ in translatable:
        lines.append("// en %s %s" % (key, en_hash(english[key])))
    lines.append("")
    lines.append("// X(ident, setting code, name in its own language, UI_SCRIPT_*) for each language,")
    lines.append("// English (the built-in table) not included")
    lines.append("#ifndef UI_I18N_LANGS")
    lines.append("#define UI_I18N_LANGS(X) \\")
    present = []
    for code, d, name, script in LANGS:
        path = os.path.join(res, "values-" + d, "strings.xml")
        if not os.path.exists(path):
            if strict:
                raise SystemExit("upstream has no values-%s" % d)
            continue
        own = {}
        npath = os.path.join(native_dir, "values-" + d, "strings.xml") if native_dir else ""
        if npath and os.path.exists(npath):
            own = read_strings(npath)
            for k in own:
                if srcmap.get(k, SAME) not in (NATIVE, DRIFT):
                    raise SystemExit("%s: %s is not a NATIVE or DRIFT key" % (npath, k))
        present.append((code, d, name, script, read_strings(path), own))
    for n, (code, d, name, script, _, _) in enumerate(present):
        ident = re.sub(r"[^A-Z0-9]", "_", code.upper())
        lines.append("    X(%s, %s, %s, UI_SCRIPT_%s)%s" % (ident, c_string(code), c_string(name), script,
                                                          " \\" if n + 1 < len(present) else ""))
    if not present:
        lines[-1] = "#define UI_I18N_LANGS(X)"
    lines.append("#endif")
    lines.append("")
    lines.append("#ifdef UI_I18N_TABLES")
    for code, d, name, script, tr, own in present:
        ident = re.sub(r"[^A-Z0-9]", "_", code.upper())
        have, missing, refused = [], [], []
        lines.append("")
        lines.append("// %s (values-%s)" % (code, d))
        lines.append("static const char* const I18N_%s[UIS_COUNT] = {" % ident)
        for key, upkey in translatable:
            text = tr.get(upkey)
            if text is None or text.strip() == "":
                missing.append(key)
                continue
            ctext, why = c_format(text, english[key])
            if ctext is None:
                refused.append((key, why))
                continue
            have.append(key)
            lines.append("    [UIS_%s] = %s," % (ids[key], c_string(ctext)))
        native_have, native_missing = [], []
        for key, src in MAP:
            if key not in ids or src not in (NATIVE, DRIFT):
                continue
            text = own.get(key)
            if not text:
                native_missing.append(key)
                continue
            ctext, why = c_format(text, english[key])
            if ctext is None:
                refused.append((key, why))
                continue
            native_have.append(key)
            lines.append("    [UIS_%s] = %s, // tools/ui/native" % (ids[key], c_string(ctext)))
        lines.append("};")
        report["langs"].append({"code": code, "dir": d, "name": name, "script": script,
                                "have": have + native_have, "missing": missing, "refused": refused,
                                "native_missing": native_missing})
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines), report


def doc_block(report, header):
    english = {k: e for _, k, e in header}
    n = len(header)
    out = [DOC_BEGIN, ""]
    out.append("Generated by `tools/ui/gen_strings.py`; %d strings in `ui/ui_strings.h`, %d of them with "
               "upstream translations, %d written for this port (native) and %d whose English changed "
               "its meaning here (drift)." % (n, len(report["translatable"]), len(report["native"]),
                                              len(report["drift"])))
    out.append("")
    out.append("| Setting | Language | Translated | English fallback |")
    out.append("| --- | --- | --- | --- |")
    for L in report["langs"]:
        fb = n - len(L["have"])
        out.append("| `%s` | %s | %d of %d | %d |" % (L["code"], L["name"], len(L["have"]), n, fb))
    out.append("")
    out.append("Left out of upstream's languages: " + "; ".join(
        "`values-%s` (%s)" % (d, why) for d, why in sorted(SKIPPED.items())) + ".")
    out.append("")
    common = None
    for L in report["langs"]:
        common = set(L["native_missing"]) if common is None else common & set(L["native_missing"])
    common = [k for k in report["native"] + report["drift"] if k in (common or set())]
    out.append("### TODO in every language: the native and drift strings")
    out.append("")
    out.append("No language has these yet, so each shows the English below. A translation of them is made by")
    out.append("a person who speaks the language, never by a machine, and goes in")
    out.append("`tools/ui/native/values-<dir>/strings.xml` (see \"Translating a native string\" above).")
    out.append("")
    out.append("| Key | English | Why |")
    out.append("| --- | --- | --- |")
    for k in common:
        why = "native" if k in report["native"] else "drift"
        out.append("| `%s` | %s | %s |" % (k, english[k].replace("|", "\\|").replace("\n", " "), why))
    out.append("")
    out.append("### TODO per language, beyond that list")
    out.append("")
    for L in report["langs"]:
        rest = L["missing"] + [k for k, _ in L["refused"]] + [k for k in L["native_missing"] if k not in common]
        if not rest:
            out.append("* `%s` (%s): nothing more" % (L["code"], L["name"]))
            continue
        out.append("* `%s` (%s): %s" % (L["code"], L["name"], ", ".join("`%s`" % k for k in rest)))
        for k, why in L["refused"]:
            out.append("  * `%s` stays English: the translation %s" % (k, why))
    out.append("")
    out.append(DOC_END)
    return "\n".join(out)


def write_doc(path, block):
    text = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    if DOC_BEGIN in text and DOC_END in text:
        a = text.index(DOC_BEGIN)
        b = text.index(DOC_END) + len(DOC_END)
        text = text[:a] + block + text[b:]
    else:
        text = text.rstrip("\n") + "\n\n" + block + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def verify(inc_path, header_path):
    """Checks without upstream: MAP covers the header, and the committed table was made from MAP"""
    header = read_header(header_path)
    errs = check_map(header)
    text = open(inc_path, encoding="utf-8").read()
    m = re.search(r"^// map: ([0-9a-f]+)$", text, re.M)
    if not m:
        errs.append("%s has no map line: regenerate it" % inc_path)
    elif m.group(1) != map_hash():
        errs.append("%s was made from another MAP (%s, MAP is %s): regenerate it with --res" %
                    (inc_path, m.group(1), map_hash()))
    ids = {i for i, _, _ in header}
    used = set(re.findall(r"^    \[UIS_(\w+)\] = ", text, re.M))
    for ident in sorted(used):
        if ident not in ids:
            errs.append("the table has UIS_%s, which ui/ui_strings.h does not" % ident)
    # the English of a translated key must be what it was when the table was made: a changed one
    # may no longer mean what the translations say
    english = {k: e for _, k, e in header}
    recorded = dict(re.findall(r"^// en (\S+) ([0-9a-f]{8})$", text, re.M))
    for key, src in MAP:
        if key not in english or src in (NATIVE, DRIFT):
            continue
        if key not in recorded:
            errs.append("%s has no recorded English in %s: regenerate it" % (key, inc_path))
        elif recorded[key] != en_hash(english[key]):
            errs.append("the English of %s changed since %s was made (now \"%s\"): if it means something "
                        "else now, make its MAP line DRIFT; then regenerate" % (key, inc_path, english[key]))
    natives = {i for i, k, _ in header if dict(MAP).get(k, SAME) in (NATIVE, DRIFT)}
    for ident in sorted(used & natives):
        errs.append("the table translates UIS_%s, which MAP keeps in English" % ident)
    codes = re.findall(r'^    X\(\w+, "([^"]+)"', text, re.M)
    if codes != [c for c, _, _, _ in LANGS]:
        errs.append("the table's languages %s are not LANGS %s: regenerate it" % (codes, [c for c, _, _, _ in LANGS]))
    return errs


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--res", help="upstream's app/src/main/res")
    ap.add_argument("--upstream-rev", help="upstream's commit, written into the header")
    ap.add_argument("--header", default=HEADER_PATH)
    ap.add_argument("--out", default=INC_PATH)
    ap.add_argument("--doc", default=DOC_PATH, help="docs/dev/i18n.md ('' for none)")
    ap.add_argument("--fixture", action="store_true",
                    help="tests: languages missing from --res are left out, no doc, no tools/ui/native")
    ap.add_argument("--verify", action="store_true", help="check the committed table against MAP")
    a = ap.parse_args(argv)
    if a.verify:
        errs = verify(a.out, a.header)
        for e in errs:
            print("gen_strings: " + e, file=sys.stderr)
        print("gen_strings: %s" % ("ok" if not errs else "%d problem(s)" % len(errs)))
        return 1 if errs else 0
    if not a.res:
        ap.error("--res or --verify")
    header = read_header(a.header)
    errs = check_map(header) if not a.fixture else [e for e in check_map(header) if "not in gen_strings" in e]
    if errs:
        for e in errs:
            print("gen_strings: " + e, file=sys.stderr)
        return 1
    text, report = generate(a.res, header, a.upstream_rev, strict=not a.fixture,
                            native_dir=None if a.fixture else NATIVE_DIR)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if a.doc and not a.fixture:
        write_doc(a.doc, doc_block(report, header))
    print("gen_strings: %d strings, %d translatable, %d languages -> %s" % (
        len(header), len(report["translatable"]), len(report["langs"]), a.out))
    for L in report["langs"]:
        print("  %-6s %3d translated, %3d missing, %d refused%s" % (
            L["code"], len(L["have"]), len(L["missing"]), len(L["refused"]),
            "".join("\n         %s: %s" % r for r in L["refused"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
