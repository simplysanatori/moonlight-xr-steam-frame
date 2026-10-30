// ABR-lite: a start bitrate for each network path. Apollo and Sunshine cannot change the bitrate of a
// stream that is running, so the only moment to choose it is the start, and the only thing known then
// is which path Automatic (or the user) picked: Wi-Fi, the Steam Frame Wireless Adapter, a wired
// network or the USB cable. Four settings, one per path ([stream] bitrate_wifi, bitrate_adapter,
// bitrate_wired, bitrate_usb, kbps, 0 = use the launcher's bitrate), each replace the launcher's
// bitrate when the stream starts on that path. Plain numbers and words, no I/O.
//
// Native (lane wave4b-abr). PLAN 4b.8. GPLv3.
#ifndef XR_ABR_H
#define XR_ABR_H

#include <stddef.h>

// The path kinds, in the order of the settings. backend/netpath.h's mlxr_net_bitrate_kind() answers with
// these numbers.
#define MLXR_ABR_UNKNOWN (-1)   // no path was chosen (a direct address, a failed probe): the launcher's bitrate
#define MLXR_ABR_WIFI 0
#define MLXR_ABR_ADAPTER 1
#define MLXR_ABR_WIRED 2
#define MLXR_ABR_USB 3
#define MLXR_ABR_KINDS 4

// What an override may ask for: the same range as the launcher's bitrate (settings.h MLXR_BITRATE_MAX_KBPS)
#define MLXR_ABR_FLOOR_KBPS 500
#define MLXR_ABR_CAP_KBPS 500000

typedef struct MlxrAbrPick {
    int kbps;          // the bitrate to start the stream with
    int overridden;    // 1: it came from the path's own setting, 0: it is the launcher's
} MlxrAbrPick;

// The decision. `launcherKbps` is the bitrate the launcher (or --bitrate, or the default curve) gave;
// `overrides[kind]` is the path's setting, 0 for none. A kind outside 0..3 or an override of 0 gives the
// launcher's bitrate. An override, and any launcher bitrate above 0, is held to 500 kbps .. 500 Mbps.
MlxrAbrPick mlxr_abr_pick(int kind, int launcherKbps, const int overrides[MLXR_ABR_KINDS]);

// "Wi-Fi", "Steam Frame Wireless Adapter", "Wired network", "USB cable", "unknown path"
const char* mlxr_abr_kind_name(int kind);

// The log line: "bitrate for Wi-Fi: 60000 kbps (override)" or "... (launcher)"
void mlxr_abr_line(int kind, const MlxrAbrPick* pick, char* buf, size_t cap);

#endif
