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

// Native (lane flat-input): the whole decision at the start of a stream, and where its figure came from, so the log
// says it plainly. In order: the connection's own row (overrides[kind] above 0), else --bitrate (the flat launcher's
// Video bitrate, its Custom bitrate row typed in the same place), else config.ini [stream] bitrate_kbps, else the
// default for the resolution and frame rate. Every figure is held to 500 kbps .. 500 Mbps, nothing lower: no 150 Mbps
// cap anywhere on this path (only the default curve stops at 150 Mbps, settings_default_bitrate_kbps).
enum {
    MLXR_BITRATE_FROM_DEFAULT = 0,   // the default for the resolution and frame rate
    MLXR_BITRATE_FROM_ARG = 1,       // --bitrate, the flat launcher's Video bitrate
    MLXR_BITRATE_FROM_CONFIG = 2,    // config.ini [stream] bitrate_kbps
    MLXR_BITRATE_FROM_PATH = 3,      // config.ini [stream] bitrate_wifi / _adapter / _wired / _usb
};

typedef struct MlxrAbrStart {
    int kbps;      // what the stream asks the PC for
    int asked;     // the figure before it was held to 500 .. 500000 (equal to kbps unless it was out of range)
    int source;    // MLXR_BITRATE_FROM_*
    int kind;      // the path kind it was decided for
} MlxrAbrStart;

MlxrAbrStart mlxr_abr_start(int kind, int argKbps, int configKbps, int defaultKbps, const int overrides[MLXR_ABR_KINDS]);

// "[stream] bitrate_wifi" for a path kind (NULL for an unknown one)
const char* mlxr_abr_key(int kind);

// Where it came from, in words for the log: "the flat launcher's Video bitrate (--bitrate)",
// "config.ini [stream] bitrate_wifi (Bitrate over Wi-Fi)", "config.ini [stream] bitrate_kbps",
// "the default for this resolution and frame rate"
void mlxr_abr_source_text(const MlxrAbrStart* st, char* buf, size_t cap);

// The log line: "stream bitrate: 300000 kbps (300 Mbps) over Wi-Fi, from the flat launcher's Video bitrate (--bitrate)",
// with ", asked for 900000 kbps, held to the 500 Mbps cap" when it was out of range
void mlxr_abr_start_line(const MlxrAbrStart* st, char* buf, size_t cap);

#endif
