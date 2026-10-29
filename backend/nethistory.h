/* Moonlight XR Native (lane netpath-quality): how the last stream went on each network path. GPLv3.
 *
 * A small file in the config directory (next to config.ini), netpath-history.ini. Per path
 * ("home" for Wi-Fi, "adapter" for the Steam Frame Wireless Adapter, "usb" for the USB cable) it keeps the last stream's
 * frames received, frames lost on the network, arrival jitter, rtt and the time. The automatic
 * network choice reads it and charges a path whose last stream lost frames (see netpath.h).
 *
 * The file holds the path keyword, numbers and a Unix timestamp. No addresses, no names.
 * A missing or damaged file means no history: nothing is charged.
 */
#ifndef MLXR_NETHISTORY_H
#define MLXR_NETHISTORY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MLXR_NETHIST_HOME 0
#define MLXR_NETHIST_ADAPTER 1
#define MLXR_NETHIST_USB 2          /* Native (lane wave4c-usb): the USB cable */
#define MLXR_NETHIST_PATHS 3

/* A stream shorter than this many frames (received plus lost) says too little and is not kept. */
#define MLXR_NETHIST_MIN_FRAMES 500
/* History older than this is not used (the room, the channel or the PC may have changed). */
#define MLXR_NETHIST_MAX_AGE_S (14 * 24 * 3600)

typedef struct {
    bool valid;
    uint64_t frames;            /* received */
    uint64_t lost;              /* lost on the network */
    double jitter_ms;           /* arrival jitter */
    double rtt_ms;
    int64_t time_s;             /* Unix time the stream ended */
} mlxr_nethist_entry;

typedef struct {
    mlxr_nethist_entry e[MLXR_NETHIST_PATHS];
} mlxr_nethist;

const char *mlxr_nethist_key(int path);                         /* "home" "adapter" "usb" */
/* Percent of the frames lost: lost / (frames + lost) * 100; 0 for an empty or invalid entry. */
double mlxr_nethist_loss_pct(const mlxr_nethist_entry *e);

/* The file's name inside `dir`. */
const char *mlxr_nethist_filename(void);
/* Pure: parses the file's text. Anything wrong with an entry leaves it invalid. Returns the number of valid entries. */
int mlxr_nethist_parse(const char *text, mlxr_nethist *h);
/* Pure: the file's text. */
int mlxr_nethist_format(const mlxr_nethist *h, char *out, int cap);
/* Reads <dir>/netpath-history.ini. A missing or unreadable file gives an empty history (returns 0). */
int mlxr_nethist_load(const char *dir, mlxr_nethist *h);
/* Writes it (temporary file, then rename; mode 0600). */
bool mlxr_nethist_save(const char *dir, const mlxr_nethist *h);
/* Loads, replaces the entry of `path`, saves. False when it could not be saved. */
bool mlxr_nethist_record(const char *dir, int path, const mlxr_nethist_entry *e);
/* A stream ended: works out which path `address` is on (the interfaces' subnets, as the choice does)
 * and records the figures for it. Ignores a short stream or an address it cannot place; returns true
 * when something was written. `now_s` 0 means the current time. */
bool mlxr_nethist_record_stream(const char *dir, const char *address, uint64_t frames, uint64_t lost, double jitter_ms,
                                double rtt_ms, int64_t now_s);

#ifdef __cplusplus
}
#endif
#endif
