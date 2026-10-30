// Moonlight XR Native (lane wave1-failover): the failover loop (failover.h) with the real session, netpath and
// settings behind it. GPLv3.
#include "failover.h"

#include "../xr/mlxr_log.h"
#include "../xr/xr_stream_prefs.h"
#include "session.h"

// ---- the real session behind the ops

static uint64_t realConnectedMs(void* u) { (void)u; return stream_connected_ms(); }
static int realFigures(void* u, uint64_t* rx, uint64_t* lost) {
    (void)u;
    StreamNetFigures nf;
    if (!stream_network_figures(&nf)) return 0;
    *rx = nf.framesReceived;
    *lost = nf.framesLost;
    return 1;
}
static int realLossWindow(void* u, uint64_t windowMs, mlxr_failover_window* w) { (void)u; return stream_loss_window(windowMs, w); }
static void realQuality(void* u, mlxr_net_quality* q) { (void)u; mlxr_net_quality_get(q); }
static int realEnabled(void* u, int* burstMs) {
    (void)u;
    *burstMs = xrStreamPrefsFailoverBurstMs();
    return xrStreamPrefsFailover();
}
static void realNote(void* u, const char* from, const char* to) { (void)u; stream_note_path_switch(from, to); }
static void realForce(void* u, mlxr_net_path to) { (void)u; mlxr_net_force_path(to); }
static void realAbandon(void* u, mlxr_net_path from) { (void)u; mlxr_net_quality_abandon(from); }
static void realNotice(void* u, const char* text) {
    void (*notice)(const char*) = (void (*)(const char*))u;
    if (notice) notice(text);
}
static void realLog(void* u, const char* line) { (void)u; MLOG_W("%s", line); }

int mlxr_failover_poll(MlxrFailover* f, MlxrReconnect* rc, int64_t nowMs, void (*notice)(const char* text)) {
    MlxrFailoverOps ops = { (void*)notice, realConnectedMs, realFigures, realLossWindow, realQuality,
                            realEnabled,   realNote,        realForce,   realAbandon,    realNotice, realLog };
    // the user pointer is only the notice function, which the other ops ignore
    return mlxr_failover_step(f, rc, &ops, nowMs);
}
