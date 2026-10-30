// ABR-lite: the start bitrate for the network path in use. See xr_abr.h. Native (lane wave4b-abr).
#include "xr_abr.h"

#include <stdio.h>

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
