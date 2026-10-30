/* Path failover policy and loss history. See failover.h. GPLv3. */
#include "failover.h"

#include <string.h>

/* ---------------------------------------------------------------- history */

void mlxr_failover_history_init(mlxr_failover_history *h) { memset(h, 0, sizeof *h); }

void mlxr_failover_history_add(mlxr_failover_history *h, uint64_t startMs, uint64_t endMs, uint64_t frames)
{
    mlxr_failover_burst *b = &h->v[h->next];
    b->startMs = startMs;
    b->endMs = endMs < startMs ? startMs : endMs;
    b->frames = frames;
    h->next = (h->next + 1) % MLXR_FAILOVER_RING;
    if (h->n < MLXR_FAILOVER_RING) h->n++;
}

static void window_add(mlxr_failover_window *w, const mlxr_failover_burst *b, uint64_t lo, uint64_t now)
{
    uint64_t s = b->startMs > lo ? b->startMs : lo;
    uint64_t e = b->endMs < now ? b->endMs : now;
    if (e < s) return;   /* before the window, or not begun yet */
    w->count++;
    w->lossMs += e - s;
    w->frames += b->frames;
}

void mlxr_failover_history_window(const mlxr_failover_history *h, const mlxr_failover_burst *open, uint64_t nowMs,
                                  uint64_t windowMs, mlxr_failover_window *out)
{
    memset(out, 0, sizeof *out);
    uint64_t lo = nowMs > windowMs ? nowMs - windowMs : 0;
    for (int i = 0; i < h->n; i++) window_add(out, &h->v[i], lo, nowMs);
    if (open) window_add(out, open, lo, nowMs);
}

/* ---------------------------------------------------------------- samples */

void mlxr_failover_samples_reset(mlxr_failover_samples *s) { memset(s, 0, sizeof *s); }

void mlxr_failover_samples_add(mlxr_failover_samples *s, uint64_t nowMs, uint64_t received, uint64_t lost)
{
    s->v[s->next] = (mlxr_failover_sample){ nowMs, received, lost };
    s->next = (s->next + 1) % MLXR_FAILOVER_SAMPLES;
    if (s->n < MLXR_FAILOVER_SAMPLES) s->n++;
}

double mlxr_failover_loss_pct(const mlxr_failover_samples *s, uint64_t nowMs, uint64_t windowMs)
{
    if (s->n < 2) return -1;
    uint64_t lo = nowMs > windowMs ? nowMs - windowMs : 0;
    const mlxr_failover_sample *oldest = NULL, *newest = NULL;
    for (int i = 0; i < s->n; i++) {
        const mlxr_failover_sample *x = &s->v[i];
        if (x->atMs < lo || x->atMs > nowMs) continue;
        if (!oldest || x->atMs < oldest->atMs) oldest = x;
        if (!newest || x->atMs > newest->atMs) newest = x;
    }
    if (!oldest || oldest == newest || newest->atMs - oldest->atMs < windowMs / 2) return -1;
    if (newest->received < oldest->received || newest->lost < oldest->lost) return -1;   /* a new stream's counters */
    uint64_t r = newest->received - oldest->received, l = newest->lost - oldest->lost;
    if (r + l == 0) return 100.0;
    return 100.0 * (double)l / (double)(r + l);
}

/* ---------------------------------------------------------------- decision */

int mlxr_failover_burst_ms(int configured)
{
    if (configured <= 0) return MLXR_FAILOVER_BURST_MS_DEFAULT;
    if (configured < MLXR_FAILOVER_BURST_MS_MIN) return MLXR_FAILOVER_BURST_MS_MIN;
    if (configured > MLXR_FAILOVER_BURST_MS_MAX) return MLXR_FAILOVER_BURST_MS_MAX;
    return configured;
}

const char *mlxr_failover_reason_text(mlxr_failover_reason r)
{
    switch (r) {
    case MLXR_FO_STAY_OFF: return "stay: failover is off";
    case MLXR_FO_STAY_PINNED: return "stay: the network path is set by hand";
    case MLXR_FO_STAY_NO_BASIS: return "stay: the current path has no measurement";
    case MLXR_FO_STAY_SETTLING: return "stay: the stream is still young";
    case MLXR_FO_STAY_COOLDOWN: return "stay: the path was switched lately";
    case MLXR_FO_STAY_NO_BURST: return "stay: not enough loss lately";
    case MLXR_FO_STAY_NO_ALTERNATIVE: return "stay: no other path to go to";
    case MLXR_FO_STAY_NOT_BETTER: return "stay: no other path is clearly better";
    case MLXR_FO_SWITCH: return "switch";
    }
    return "stay";
}

mlxr_failover_out mlxr_failover_decide(const mlxr_failover_in *in)
{
    mlxr_failover_out o = { false, MLXR_PATH_NONE, MLXR_FO_STAY_OFF, -1, -1 };
    if (!in->enabled) return o;
    if (!in->pref_auto) { o.reason = MLXR_FO_STAY_PINNED; return o; }
    if (in->cur_path == MLXR_PATH_NONE || in->cur_score_ms < 0) { o.reason = MLXR_FO_STAY_NO_BASIS; return o; }
    if (in->now_ms - in->stream_start_ms < MLXR_FAILOVER_SETTLE_MS) { o.reason = MLXR_FO_STAY_SETTLING; return o; }
    if (in->last_switch_ms >= 0 && in->now_ms - in->last_switch_ms < MLXR_FAILOVER_MIN_GAP_MS) {
        o.reason = MLXR_FO_STAY_COOLDOWN;
        return o;
    }
    if (in->loss.lossMs < (uint64_t)mlxr_failover_burst_ms(in->burst_ms)) { o.reason = MLXR_FO_STAY_NO_BURST; return o; }

    /* the best other path that was measured, is here, and is not stale */
    const mlxr_failover_cand *best = NULL;
    for (int i = 0; i < in->n_cand; i++) {
        const mlxr_failover_cand *c = &in->cand[i];
        if (c->path == in->cur_path || c->path == MLXR_PATH_NONE || !c->present || c->score_ms < 0) continue;
        if (in->now_ms - c->probed_ms > MLXR_FAILOVER_PROBE_MAX_AGE_MS) continue;
        if (!best || c->score_ms < best->score_ms || (c->score_ms == best->score_ms && c->path < best->path)) best = c;
    }
    if (!best) { o.reason = MLXR_FO_STAY_NO_ALTERNATIVE; return o; }

    double loss = in->cur_loss_pct > 0 ? in->cur_loss_pct : 0;
    o.cur_effective_ms = in->cur_score_ms + mlxr_net_hist_penalty(loss);
    o.to_score_ms = best->score_ms;
    if (best->score_ms + MLXR_FAILOVER_CLEAR_MS > o.cur_effective_ms) { o.reason = MLXR_FO_STAY_NOT_BETTER; return o; }
    o.sw = true;
    o.to = best->path;
    o.reason = MLXR_FO_SWITCH;
    return o;
}
