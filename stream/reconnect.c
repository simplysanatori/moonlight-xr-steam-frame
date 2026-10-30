// See reconnect.h. GPLv3.
#include "reconnect.h"

#include <errno.h>
#include <string.h>

int mlxr_reconnect_retryable(int termCode) {
    switch (termCode) {
    case -100: // ML_ERROR_NO_VIDEO_TRAFFIC
    case -101: // ML_ERROR_NO_VIDEO_FRAME
    case -102: // ML_ERROR_UNEXPECTED_EARLY_TERMINATION
    case -1:   // the control stream timed out, was closed unannounced, or a thread failed
        return 1;
    case ENETDOWN:
    case ENETUNREACH:
    case ENETRESET:
    case ECONNABORTED:
    case ECONNRESET:
    case ENOTCONN:
    case ETIMEDOUT:
    case EHOSTDOWN:
    case EHOSTUNREACH:
    case EPIPE:
        return 1;
    default:
        return 0; // 0 graceful, -103 protected content, -104 frame conversion, the host's short reasons
    }
}

void mlxr_reconnect_init(MlxrReconnect* r) {
    memset(r, 0, sizeof(*r));
    r->upMs = -1;
}

static int delayFor(int attempt) {
    return attempt <= 1 ? MLXR_RECONNECT_DELAY1_MS : MLXR_RECONNECT_DELAY2_MS;
}

static void idle(MlxrReconnect* r) {
    r->state = MLXR_RC_IDLE;
    r->attempt = 0;
}

int mlxr_reconnect_drop(MlxrReconnect* r, int termCode, int64_t nowMs) {
    if (!mlxr_reconnect_retryable(termCode)) {
        idle(r);
        r->used = 0;
        return 0;
    }
    // A stream that held for the stable time is a recovery: a new series. A flapping one carries on with the
    // tries it has used.
    if (r->upMs < 0 || nowMs - r->upMs >= MLXR_RECONNECT_STABLE_MS) r->used = 0;
    r->lastCode = termCode;
    if (r->used >= MLXR_RECONNECT_TRIES) {
        idle(r);
        r->used = 0;
        return 0;
    }
    r->attempt = r->used + 1;
    r->dueMs = nowMs + delayFor(r->attempt);
    r->state = MLXR_RC_WAITING;
    return 1;
}

int mlxr_reconnect_switch(MlxrReconnect* r, int64_t nowMs) {
    if (r->state != MLXR_RC_IDLE) return 0;
    r->used = 0;
    r->lastCode = MLXR_RC_CODE_SWITCH;
    r->attempt = 1;
    r->dueMs = nowMs + MLXR_RECONNECT_SWITCH_DELAY_MS;
    r->state = MLXR_RC_WAITING;
    return 1;
}

int mlxr_reconnect_due(MlxrReconnect* r, int64_t nowMs) {
    if (r->state != MLXR_RC_WAITING || nowMs < r->dueMs) return 0;
    r->state = MLXR_RC_TRYING;
    r->used = r->attempt;
    return r->attempt;
}

int mlxr_reconnect_result(MlxrReconnect* r, MlxrReconnectOutcome outcome, int64_t nowMs) {
    if (r->state != MLXR_RC_TRYING) return 0;
    if (outcome == MLXR_RC_OK) {
        r->upMs = nowMs;
        idle(r);
        return 0;
    }
    if (outcome == MLXR_RC_FATAL || r->used >= MLXR_RECONNECT_TRIES) {
        idle(r);
        r->used = 0;
        return 0;
    }
    r->attempt = r->used + 1;
    r->dueMs = nowMs + delayFor(r->attempt);
    r->state = MLXR_RC_WAITING;
    return 1;
}

void mlxr_reconnect_cancel(MlxrReconnect* r) {
    idle(r);
    r->used = 0;
}

int mlxr_reconnect_active(const MlxrReconnect* r) {
    return r->state != MLXR_RC_IDLE;
}

MlxrReconnectEvent mlxr_reconnect_on_drop(MlxrReconnect* r, const MlxrReconnectOps* ops, int termCode, int64_t nowMs) {
    if (mlxr_reconnect_drop(r, termCode, nowMs)) {
        if (ops->scheduled) ops->scheduled(ops->user, r->attempt, MLXR_RECONNECT_TRIES, termCode);
        return MLXR_RC_EV_SCHEDULED;
    }
    if (ops->gaveUp) ops->gaveUp(ops->user, termCode, MLXR_RC_FAILED);
    return MLXR_RC_EV_GAVE_UP;
}

MlxrReconnectEvent mlxr_reconnect_poll(MlxrReconnect* r, const MlxrReconnectOps* ops, int64_t nowMs) {
    if (r->state != MLXR_RC_WAITING) return MLXR_RC_EV_NONE;
    int attempt = mlxr_reconnect_due(r, nowMs);
    if (attempt == 0) return MLXR_RC_EV_WAITING;
    MlxrReconnectOutcome out = ops->attempt ? ops->attempt(ops->user, attempt) : MLXR_RC_FAILED;
    int64_t endMs = ops->nowMs ? ops->nowMs(ops->user) : nowMs;
    int more = mlxr_reconnect_result(r, out, endMs);
    if (out == MLXR_RC_OK) {
        if (ops->resumed) ops->resumed(ops->user, attempt);
        return MLXR_RC_EV_RESUMED;
    }
    if (more) {
        if (ops->scheduled) ops->scheduled(ops->user, r->attempt, MLXR_RECONNECT_TRIES, r->lastCode);
        return MLXR_RC_EV_SCHEDULED;
    }
    if (ops->gaveUp) ops->gaveUp(ops->user, r->lastCode, out);
    return MLXR_RC_EV_GAVE_UP;
}
