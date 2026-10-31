// ABR-lite: the start bitrate for the network path in use. See xr_abr.h. Native (lane wave4b-abr).
#include "xr_abr.h"

#include <stdio.h>
#include <string.h>

static int clampKbps(int v) {
    return v < MLXR_ABR_FLOOR_KBPS ? MLXR_ABR_FLOOR_KBPS : v > MLXR_ABR_CAP_KBPS ? MLXR_ABR_CAP_KBPS : v;
}

MlxrAbrPick mlxr_abr_pick(int kind, int launcherKbps, const int overrides[MLXR_ABR_KINDS]) {
    MlxrAbrPick p;
    p.overridden = 0;
    if (overrides != NULL && kind >= 0 && kind < MLXR_ABR_KINDS && overrides[kind] > 0) {
        p.kbps = clampKbps(overrides[kind]);
        p.overridden = 1;
        return p;
    }
    p.kbps = launcherKbps > 0 ? clampKbps(launcherKbps) : launcherKbps;
    return p;
}

const char* mlxr_abr_kind_name(int kind) {
    switch (kind) {
        case MLXR_ABR_WIFI: return "Wi-Fi";
        case MLXR_ABR_ADAPTER: return "Steam Frame Wireless Adapter";
        case MLXR_ABR_WIRED: return "Wired network";
        case MLXR_ABR_USB: return "USB cable";
        default: return "unknown path";
    }
}

void mlxr_abr_line(int kind, const MlxrAbrPick* pick, char* buf, size_t cap) {
    if (cap == 0) return;
    snprintf(buf, cap, "bitrate for %s: %d kbps (%s)", mlxr_abr_kind_name(kind), pick->kbps,
             pick->overridden ? "override" : "launcher");
}

// ---- Native (lane flat-input): the start decision and its source ----------------

MlxrAbrStart mlxr_abr_start(int kind, int argKbps, int configKbps, int defaultKbps, const int overrides[MLXR_ABR_KINDS]) {
    MlxrAbrStart st;
    st.kind = kind;
    int base;
    if (argKbps > 0) {
        base = argKbps;
        st.source = MLXR_BITRATE_FROM_ARG;
    } else if (configKbps > 0) {
        base = configKbps;
        st.source = MLXR_BITRATE_FROM_CONFIG;
    } else {
        base = defaultKbps;
        st.source = MLXR_BITRATE_FROM_DEFAULT;
    }
    MlxrAbrPick p = mlxr_abr_pick(kind, base, overrides);
    if (p.overridden) {
        st.source = MLXR_BITRATE_FROM_PATH;
        st.asked = overrides[kind];
    } else {
        st.asked = base;
    }
    st.kbps = p.kbps;
    return st;
}

const char* mlxr_abr_key(int kind) {
    switch (kind) {
        case MLXR_ABR_WIFI: return "bitrate_wifi";
        case MLXR_ABR_ADAPTER: return "bitrate_adapter";
        case MLXR_ABR_WIRED: return "bitrate_wired";
        case MLXR_ABR_USB: return "bitrate_usb";
        default: return NULL;
    }
}

static const char* overText(int kind) {
    switch (kind) {
        case MLXR_ABR_WIFI: return "Wi-Fi";
        case MLXR_ABR_ADAPTER: return "the Steam Frame Wireless Adapter";
        case MLXR_ABR_WIRED: return "a wired network";
        case MLXR_ABR_USB: return "the USB cable";
        default: return "an unknown network path";
    }
}

// The flat launcher's row for each path (launcher/qml/XrSettingsView.qml)
static const char* rowName(int kind) {
    switch (kind) {
        case MLXR_ABR_WIFI: return "Bitrate over Wi-Fi";
        case MLXR_ABR_ADAPTER: return "Bitrate over the Steam Frame Wireless Adapter";
        case MLXR_ABR_WIRED: return "Bitrate over a wired network";
        case MLXR_ABR_USB: return "Bitrate over the USB cable";
        default: return "";
    }
}

void mlxr_abr_source_text(const MlxrAbrStart* st, char* buf, size_t cap) {
    if (cap == 0) return;
    switch (st->source) {
        case MLXR_BITRATE_FROM_ARG:
            snprintf(buf, cap, "the flat launcher's Video bitrate (--bitrate)");
            break;
        case MLXR_BITRATE_FROM_CONFIG:
            snprintf(buf, cap, "config.ini [stream] bitrate_kbps");
            break;
        case MLXR_BITRATE_FROM_PATH: {
            const char* key = mlxr_abr_key(st->kind);
            snprintf(buf, cap, "config.ini [stream] %s (%s)", key ? key : "?", rowName(st->kind));
            break;
        }
        default:
            snprintf(buf, cap, "the default for this resolution and frame rate");
            break;
    }
}

void mlxr_abr_start_line(const MlxrAbrStart* st, char* buf, size_t cap) {
    if (cap == 0) return;
    char from[96];
    mlxr_abr_source_text(st, from, sizeof(from));
    // Mbps with no trailing zeros: 300, 87.5, 123.456
    char mbps[24];
    snprintf(mbps, sizeof(mbps), "%d", st->kbps / 1000);
    if (st->kbps % 1000) {
        snprintf(mbps, sizeof(mbps), "%.3f", st->kbps / 1000.0);
        size_t n = strlen(mbps);
        while (n > 0 && mbps[n - 1] == '0') mbps[--n] = 0;
    }
    char held[80] = "";
    if (st->asked != st->kbps && st->asked > 0) {
        snprintf(held, sizeof(held), ", asked for %d kbps, held to %s", st->asked,
                 st->kbps >= MLXR_ABR_CAP_KBPS ? "the 500 Mbps cap" : "the 500 kbps floor");
    }
    snprintf(buf, cap, "stream bitrate: %d kbps (%s Mbps) over %s, from %s%s", st->kbps, mbps, overText(st->kind), from,
             held);
}
