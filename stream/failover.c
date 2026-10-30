// See failover.h. GPLv3.
#include "failover.h"

#include <stdio.h>
#include <string.h>


#define POLL_PERIOD_MS 1000

void mlxr_failover_switch_text(mlxr_net_path from, mlxr_net_path to, uint64_t lossMs, char* log, size_t logCap,
                               char* notice, size_t noticeCap) {
    if (log)
        snprintf(log, logCap, "network path: switching from %s to %s after %llu ms of loss", mlxr_net_path_label(from),
                 mlxr_net_path_label(to), (unsigned long long)lossMs);
    if (notice)
        snprintf(notice, noticeCap, "Video keeps breaking up on %s: switching to %s", mlxr_net_path_label(from),
                 mlxr_net_path_label(to));
}

int mlxr_failover_step(MlxrFailover* f, MlxrReconnect* rc, const MlxrFailoverOps* ops, int64_t nowMs) {
    if (f->lastPollMs && nowMs - f->lastPollMs < POLL_PERIOD_MS) return 0;
    f->lastPollMs = nowMs ? nowMs : 1;
    uint64_t key = ops->connectedMs(ops->user);
    if (!key) return 0;                        // no stream is connected
    if (key != f->streamKey) {                 // a new stream (or a reconnect): its counts start again
        f->streamKey = key;
        f->streamStartMs = nowMs;
        mlxr_failover_samples_reset(&f->samples);
    }
    uint64_t received = 0, lost = 0;
    if (ops->figures(ops->user, &received, &lost)) mlxr_failover_samples_add(&f->samples, (uint64_t)nowMs, received, lost);
    int burstMs = 0;
    if (!ops->enabled(ops->user, &burstMs)) return 0;

    mlxr_net_quality q;
    ops->quality(ops->user, &q);
    if (!q.valid) return 0;
    mlxr_failover_window loss;
    if (!ops->lossWindow(ops->user, MLXR_FAILOVER_WINDOW_MS, &loss)) return 0;

    mlxr_failover_cand cand[3];
    int n = 0;
    for (int path = MLXR_PATH_HOME; path <= MLXR_PATH_USB; path++) {
        mlxr_failover_cand* c = &cand[n++];
        c->path = (mlxr_net_path)path;
        c->present = q.p[path].present;
        c->score_ms = q.p[path].score_ms;
        c->probed_ms = q.p[path].probed_ms;
        c->vpn = q.p[path].vpn;   // Native (lane vpn-fallback)
    }
    mlxr_failover_in in;
    memset(&in, 0, sizeof(in));
    in.enabled = 1;
    in.pref_auto = q.pref_auto;
    in.burst_ms = burstMs;
    in.now_ms = nowMs;
    in.stream_start_ms = f->streamStartMs;
    in.last_switch_ms = f->hasSwitched ? f->lastSwitchMs : -1;
    in.cur_path = q.chosen;
    in.cur_score_ms = q.chosen >= MLXR_PATH_HOME && q.chosen <= MLXR_PATH_USB ? q.p[q.chosen].score_ms : -1;
    in.cur_loss_pct = mlxr_failover_loss_pct(&f->samples, (uint64_t)nowMs, MLXR_FAILOVER_WINDOW_MS);
    in.loss = loss;
    in.cand = cand;
    in.n_cand = n;
    in.vpn_only_away = q.vpn_only_away; // Native (lane vpn-fallback): a direct path is never left for a VPN one
    in.cur_vpn = q.chosen_vpn;
    mlxr_failover_out out = mlxr_failover_decide(&in);
    if (!out.sw) {
        snprintf(f->lastWhy, sizeof(f->lastWhy), "%s", mlxr_failover_reason_text(out.reason));
        return 0;
    }
    if (!mlxr_reconnect_switch(rc, nowMs)) return 0; // a reconnect is already on its way

    char logText[200], text[200];
    mlxr_failover_switch_text(q.chosen, out.to, loss.lossMs, logText, sizeof(logText), text, sizeof(text));
    char line[400];
    snprintf(line, sizeof(line),
             "%s (%d bursts, %llu frames lost in the last %d s, %.1f%% of the frames; the current path scores %.1f ms with "
             "that, %s %.1f ms)",
             logText, loss.count, (unsigned long long)loss.frames, MLXR_FAILOVER_WINDOW_MS / 1000,
             in.cur_loss_pct > 0 ? in.cur_loss_pct : 0.0, out.cur_effective_ms, mlxr_net_path_label(out.to), out.to_score_ms);
    if (ops->log) ops->log(ops->user, line);
    ops->noteSwitch(ops->user, mlxr_net_path_label(q.chosen), mlxr_net_path_label(out.to));
    ops->forcePath(ops->user, out.to);
    ops->abandon(ops->user, q.chosen);
    f->hasSwitched = 1;
    f->lastSwitchMs = nowMs;
    f->switches++;
    if (ops->notice) ops->notice(ops->user, text);
    return 1;
}
