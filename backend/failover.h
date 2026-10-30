/* Moonlight XR Native (lane wave1-failover): leaving a network path that keeps losing video for another
 * one. GPLv3.
 *
 * The client picks one path at the start of a stream (netpath.h) and the PC sends one UDP flow to that
 * address, so there is no using two links at once. A path can go bad in the middle of a stream though
 * (the 6 GHz link in one corner of a room, loss in bursts of seconds). With `[stream] failover = on` the
 * session watches the loss bursts (stream/rfi_watch.h) and, when a path loses video for
 * `failover_burst_ms` (default 3000 ms) within the last 10 s, asks mlxr_failover_decide whether another
 * path should carry the stream. The answer is "stay" or "switch to <path>"; this file only decides and
 * has no network, clock or thread of its own, so a table drives all of it.
 *
 * Rules, all of them must hold to switch:
 *  - the setting is on, and the network path setting is Automatic (a path the user picked is never left);
 *  - the stream has run for MLXR_FAILOVER_SETTLE_MS (the first seconds of a stream are a connect storm);
 *  - the last switch was at least MLXR_FAILOVER_MIN_GAP_MS ago (no flapping: five minutes);
 *  - the burst history of the last 10 s adds up to failover_burst_ms of loss;
 *  - another path was present and answered the probe (at the start of the stream, or the forced
 *    probe of an earlier switch) and that measurement is not older than MLXR_FAILOVER_PROBE_MAX_AGE_MS;
 *    a path that was absent (no adapter station, no USB cable) or that this stream already left is
 *    never a candidate;
 *  - its score (netpath.h: round trip, jitter, failed probes, the last stream's losses; lower is better)
 *    is at least MLXR_FAILOVER_CLEAR_MS below the current path's score plus what the loss of the last
 *    10 s costs it (the same 20 ms per percent as the history penalty, at most 150 ms). So a path
 *    with a worse round trip, such as the adapter against a good 5 GHz link, wins only when the
 *    current path really is losing frames.
 */
#ifndef MLXR_FAILOVER_H
#define MLXR_FAILOVER_H

#include <stdbool.h>
#include <stdint.h>

#include "netpath.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MLXR_FAILOVER_WINDOW_MS 10000       /* the loss is summed over this */
#define MLXR_FAILOVER_BURST_MS_DEFAULT 3000 /* [stream] failover_burst_ms */
#define MLXR_FAILOVER_BURST_MS_MIN 500
#define MLXR_FAILOVER_BURST_MS_MAX 9000     /* the window is 10 s: more could never be reached */
#define MLXR_FAILOVER_SETTLE_MS 20000
#define MLXR_FAILOVER_MIN_GAP_MS 300000     /* 5 minutes between two switches */
#define MLXR_FAILOVER_CLEAR_MS 15.0
#define MLXR_FAILOVER_PROBE_MAX_AGE_MS (2 * 3600 * 1000)

/* ---- the loss history ---- */

#define MLXR_FAILOVER_RING 32

typedef struct { uint64_t startMs, endMs, frames; } mlxr_failover_burst;   /* a closed burst, first to last lost frame */

typedef struct {
    mlxr_failover_burst v[MLXR_FAILOVER_RING];
    int n, next;
} mlxr_failover_history;

typedef struct mlxr_failover_window {
    int count;                  /* bursts that touch the window */
    uint64_t lossMs;            /* their time inside the window, summed */
    uint64_t frames;            /* frames they lost on the network */
} mlxr_failover_window;

void mlxr_failover_history_init(mlxr_failover_history *h);
void mlxr_failover_history_add(mlxr_failover_history *h, uint64_t startMs, uint64_t endMs, uint64_t frames);
/* The loss in [nowMs - windowMs, nowMs]. `open` (may be NULL) is a burst that has not closed yet. */
void mlxr_failover_history_window(const mlxr_failover_history *h, const mlxr_failover_burst *open, uint64_t nowMs,
                                  uint64_t windowMs, mlxr_failover_window *out);

/* Frame counts of the stream so far, sampled about once a second, for the share of frames lost lately. */
#define MLXR_FAILOVER_SAMPLES 24
typedef struct { uint64_t atMs, received, lost; } mlxr_failover_sample;
typedef struct {
    mlxr_failover_sample v[MLXR_FAILOVER_SAMPLES];
    int n, next;
} mlxr_failover_samples;

void mlxr_failover_samples_reset(mlxr_failover_samples *s);
void mlxr_failover_samples_add(mlxr_failover_samples *s, uint64_t nowMs, uint64_t received, uint64_t lost);
/* Percent of frames lost in the last windowMs, from the oldest sample inside it to the newest. -1 when the
 * samples span less than half the window. 100 when they span it and no frame arrived at all (the video
 * stopped). */
double mlxr_failover_loss_pct(const mlxr_failover_samples *s, uint64_t nowMs, uint64_t windowMs);

/* ---- the decision ---- */

typedef struct {
    mlxr_net_path path;
    bool present;               /* answered a probe; false: absent (no adapter, no cable) or left already */
    double score_ms;            /* mlxr_net_score at that probe; < 0 unknown */
    int64_t probed_ms;          /* when, on the caller's clock */
} mlxr_failover_cand;

typedef struct {
    bool enabled;               /* [stream] failover */
    bool pref_auto;             /* the network path setting is Automatic */
    int burst_ms;               /* [stream] failover_burst_ms; 0 means the default */
    int64_t now_ms, stream_start_ms;
    int64_t last_switch_ms;     /* < 0: never */
    mlxr_net_path cur_path;
    double cur_score_ms;        /* the current path's score at its probe; < 0 unknown */
    double cur_loss_pct;        /* percent of frames lost in the window, < 0 unknown */
    mlxr_failover_window loss;
    const mlxr_failover_cand *cand;
    int n_cand;
} mlxr_failover_in;

typedef enum {
    MLXR_FO_STAY_OFF = 0,       /* failover is off */
    MLXR_FO_STAY_PINNED,        /* the user picked a path */
    MLXR_FO_STAY_NO_BASIS,      /* no current path or no score for it */
    MLXR_FO_STAY_SETTLING,      /* the stream is young */
    MLXR_FO_STAY_COOLDOWN,      /* a switch happened lately */
    MLXR_FO_STAY_NO_BURST,      /* not enough loss in the window */
    MLXR_FO_STAY_NO_ALTERNATIVE,/* no other path to go to (absent, left, stale or never measured) */
    MLXR_FO_STAY_NOT_BETTER,    /* the best other path is not clearly better */
    MLXR_FO_SWITCH
} mlxr_failover_reason;

typedef struct {
    bool sw;
    mlxr_net_path to;           /* with sw */
    mlxr_failover_reason reason;
    double cur_effective_ms;    /* the current path's score plus the cost of its loss (-1 when not worked out) */
    double to_score_ms;         /* the best other path's score (-1 none) */
} mlxr_failover_out;

mlxr_failover_out mlxr_failover_decide(const mlxr_failover_in *in);
/* A sentence for a log: "stay: failover is off". */
const char *mlxr_failover_reason_text(mlxr_failover_reason r);
/* The setting's burst length held to its range (0 or less is the default). */
int mlxr_failover_burst_ms(int configured);

#ifdef __cplusplus
}
#endif
#endif
