// Moonlight XR Native (lane wave1-failover): moves a running stream to another network path when its own
// keeps losing video. GPLv3.
//
// The policy is backend/failover.h; this is the loop around it, once a second from the app's worker while a
// stream runs: it samples the stream's frame counts, reads the loss bursts (stream/session.h) and the
// per-path measurements of the choice made at the start (backend/netpath.h), asks mlxr_failover_decide, and
// when the answer is "switch" it
//   - logs "network path: switching from X to Y after N ms of loss",
//   - writes the open burst's summary line with the switch named at its end (stream_note_path_switch),
//   - tells netpath to try the chosen path first at the next choice (mlxr_net_force_path) and to leave the old
//     path alone until the next stream start (mlxr_net_quality_abandon),
//   - schedules the reconnect (mlxr_reconnect_switch: the stream is torn down, the scene stays, the same app
//     resumes; the connect chooses the path as for every try),
//   - shows a notice with the new path's name.
// [stream] failover (xrStreamPrefsFailover) and failover_burst_ms are read at every poll.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../backend/failover.h"
#include "reconnect.h"

typedef struct MlxrFailover {
    uint64_t streamKey;              // stream_connected_ms() of the stream the samples belong to
    int64_t streamStartMs;           // ... on the caller's clock
    int64_t lastPollMs;
    int hasSwitched;
    int64_t lastSwitchMs;            // survives streams: a switch just before a new start still counts
    int switches;                    // moves made since the app started
    mlxr_failover_samples samples;
    char lastWhy[96];                // the last answer that was not "switch" (for a test)
} MlxrFailover;                      // all zero is the start state

// What the loop reads and does outside itself, so a test runs it with a fake session
typedef struct MlxrFailoverOps {
    void* user;
    uint64_t (*connectedMs)(void* user);                                        // stream_connected_ms: 0 no stream
    int (*figures)(void* user, uint64_t* received, uint64_t* lost);             // frame counts of the stream so far
    int (*lossWindow)(void* user, uint64_t windowMs, mlxr_failover_window* w);  // 1 while a stream runs
    void (*quality)(void* user, mlxr_net_quality* q);                           // per-path measurements of the choice
    int (*enabled)(void* user, int* burstMs);                                   // [stream] failover, failover_burst_ms
    void (*noteSwitch)(void* user, const char* from, const char* to);           // the burst summary names the move
    void (*forcePath)(void* user, mlxr_net_path to);                            // the next choice goes there first
    void (*abandon)(void* user, mlxr_net_path from);                            // ... and not back until the next start
    void (*notice)(void* user, const char* text);                               // the headset's text (may be NULL)
    void (*log)(void* user, const char* line);                                  // moonlight.log (may be NULL)
} MlxrFailoverOps;

// One step at nowMs (the caller's CLOCK_MONOTONIC milliseconds; calls less than a second after the last one do
// nothing). Returns 1 when a switch was begun: the reconnect is scheduled (mlxr_reconnect_switch) and the caller
// tears the stream down for a retry exactly as for a dropped link.
int mlxr_failover_step(MlxrFailover* f, MlxrReconnect* rc, const MlxrFailoverOps* ops, int64_t nowMs);

// The app's call, with the real session, netpath and settings behind the ops. notice (may be NULL) is
// xr_app_notice.
int mlxr_failover_poll(MlxrFailover* f, MlxrReconnect* rc, int64_t nowMs, void (*notice)(const char* text));

// The text of the log line and the notice
void mlxr_failover_switch_text(mlxr_net_path from, mlxr_net_path to, uint64_t lossMs, char* log, size_t logCap,
                               char* notice, size_t noticeCap);
