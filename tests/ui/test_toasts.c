// Native (1.3.3): every notice the headset shows fits its toast without an ellipsis. The toast is a
// 1024x160 strip (xr_shared.h); a long notice gets a little smaller, then takes two lines
// (panel_misc.c toast_draw), and only past that is it cut. This draws each notice the way the
// headset does and fails on any cut, so a too long one shows here rather than in the headset.
//
// Covered: the string table's toasts and the launcher's notices in every language with a font,
// and the English notices the app writes itself (app/main.c, xr/core/xr_heat.h, backend/), with
// the longest numbers and names they take. A new notice belongs in one of the lists below.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "panels.h"
#include "text.h"
#include "ui_strings.h"
#include "../../xr/core/xr_heat.h"

// What a %s stands for: a PC or app name of a usual length
#define NAME "Living Room Gaming PC"

static uint8_t* g_buf;
static UiCanvas g_cv;
static int g_cuts;

// A notice's text as the app builds it: %d as a 3 digit number, %s as NAME
static void fill(const char* fmt, char* out, size_t cap) {
    size_t o = 0;
    for (const char* p = fmt; *p && o + 1 < cap; p++) {
        const char* put = NULL;
        char one[2] = { *p, 0 };
        if (p[0] == '%' && p[1] == 'd') put = "144", p++;
        else if (p[0] == '%' && p[1] == 's') put = NAME, p++;
        else if (p[0] == '%' && p[1] == '%') put = "%", p++;
        else put = one;
        size_t n = strlen(put);
        if (o + n >= cap) break;
        memcpy(out + o, put, n);
        o += n;
    }
    out[o] = 0;
}

static void check_one(const char* where, const char* text, const char* more) {
    // xr_panels.c keeps a notice in 160 bytes and its second line in 256
    if (strlen(text) >= 160 || (more && strlen(more) >= 256)) {
        printf("ui_toasts: %s: too long to keep: \"%s\"\n", where, text);
        g_cuts++;
        CHECK(0);
        return;
    }
    ui_cv_clear(&g_cv);
    if (ui_toast_cut(&g_cv, text, more)) {
        printf("ui_toasts: %s: cut: \"%s\"%s%s%s\n", where, text, more ? " / \"" : "", more ? more : "", more ? "\"" : "");
        g_cuts++;
        CHECK(0);
    }
}

// String table entries shown as toasts: every toast key, and the launcher's notices
static int is_toast(UiStr id) {
    const char* key = ui_str_key(id);
    if (strstr(key, "_toast_")) return 1;
    switch (id) {
    case UIS_NATIVE_RECONNECTING:
    case UIS_NATIVE_DISCONNECTED:
    case UIS_NATIVE_DISCONNECTED_BAR:
    case UIS_NATIVE_DISCONNECTED_DOFF:
    case UIS_NATIVE_DISCONNECTED_IDLE:
    case UIS_NATIVE_DISCONNECTED_KEYS:
    case UIS_NATIVE_NOT_PAIRED:
    case UIS_NATIVE_OFFLINE_WAKE:
    case UIS_NATIVE_PAIR_LATER:
    case UIS_NATIVE_QUITTING:
    case UIS_NATIVE_STARTING:
    case UIS_NATIVE_STREAM_ENDED:
    case UIS_NATIVE_STREAM_FAILED:
        return 1;
    default:
        return 0;
    }
}

// The English notices the app writes itself, filled in as the app fills them
static const char* const ENGLISH[] = {
    // app/main.c: the refresh rate
    "Headset at 144 Hz, not 120: set 120 Hz for this app in SteamVR",
    "Headset at 144 Hz, stream at 120 fps: start the stream again to match",
    // app/main.c: the bar and the PC
    "Not streaming",
    "Still busy with the last request",
    "Could not start",
    "No paired host: pair with mlxr-cli",
    "Sent 9999 characters to the PC clipboard",
    "Sent \"Restart Steam and open Big Picture on the main display now\" to the PC",   // %.60s
    "The command could not be sent",
    "The PC clipboard is empty",
    "The PC did not answer",
    "The PC clipboard could not be reached",
    // backend/hostcmd.c
    "Not supported by this host",
    "Not allowed for this client in Apollo",
    "No server commands are set up in Apollo",
    "The host has no such command",
    "The host sees no stream from this client",
    // xr/core/xr_heat.h
    HEAT_TEXT_STOP,
    // backend/netpath.c: "%s not reachable: Using %s", its longest labels (the reason goes to the log)
    "Steam Frame Wireless Adapter not reachable: Using Wi-Fi",
    "Wi-Fi not reachable: Using Steam Frame Wireless Adapter",
    "USB cable not reachable: Using Steam Frame Wireless Adapter",
};

int main(void) {
    UiFont* font = ui_font_default();
    CHECK(font != NULL);
    if (!font) return checksDone("ui_toasts");
    int w, h;
    ui_panel_native_size(UI_PANEL_TOAST, &w, &h);
    g_buf = calloc((size_t)w * h, 4);
    ui_cv_init(&g_cv, g_buf, w, h);

    // the English notices, and the heat warning with its second line
    for (size_t i = 0; i < sizeof ENGLISH / sizeof ENGLISH[0]; i++) check_one("en (app)", ENGLISH[i], NULL);
    check_one("en (heat)", HEAT_TEXT_WARN, HEAT_TEXT_WARN_3D_MORE);
    check_one("en (heat)", HEAT_TEXT_WARN, HEAT_TEXT_WARN_FLAT_MORE);

    // the string table's toasts in every language there is a font for
    int skipped = 0, checked = 0;
    for (int lang = 0; lang < ui_lang_count(); lang++) {
        char msg[768];
        if (ui_lang_start(lang, msg, sizeof msg) != lang) {
            CHECK(ui_lang_script(lang) >= UI_SCRIPT_CJK_JA);   // only CJK may lack a font here
            skipped++;
            continue;
        }
        char where[32];
        snprintf(where, sizeof where, "%s (%s)", ui_lang_code(lang), "strings");
        for (int id = 0; id < UIS_COUNT; id++) {
            if (!is_toast((UiStr)id)) continue;
            char text[512];
            fill(ui_str_lang(lang, (UiStr)id), text, sizeof text);
            char label[96];
            snprintf(label, sizeof label, "%s %s", where, ui_str_key((UiStr)id));
            check_one(label, text, NULL);
            checked++;
        }
        ui_font_clear_fallback(font);
    }
    ui_lang_start(0, NULL, 0);
    printf("ui_toasts: %d string table notices checked, %d language(s) skipped for want of a font, %d cut\n",
           checked, skipped, g_cuts);
    free(g_buf);
    ui_panels_shutdown();
    return checksDone("ui_toasts");
}
