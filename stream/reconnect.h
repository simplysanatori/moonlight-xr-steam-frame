// Moonlight XR Native (lane wave3-session): when a dropped stream is worth
// trying again, and the retry schedule. No network, no headset, no clock of
// its own (the caller passes milliseconds), so a test drives all of it with a
// fake session. GPLv3.
//
// The app keeps the scene (room, screen, 3D state) up while it retries: two
// tries on the same app, the first 2 s after the drop and the second 5 s
// after the first one failed. Each try asks the host to resume its running
// session, so the app is never launched twice. A user's exit or disconnect
// cancels the series; a refusal that retrying cannot fix (not paired, the
// certificate changed) ends it. When it is over the caller takes the old way
// out: back to the launcher with the stream-ended exit code.
#pragma once
#include <stdint.h>

#define MLXR_RECONNECT_TRIES 2
// Milliseconds from the drop (try 1) or from the failure of the previous try
// (try 2) to the try
#define MLXR_RECONNECT_DELAY1_MS 2000
#define MLXR_RECONNECT_DELAY2_MS 5000
// A reconnected stream that lasts this long counts as a recovery: a drop after
// it starts a fresh series. A shorter one is a flapping link, and the tries
// already used stay used, so the series cannot go on for ever.
#define MLXR_RECONNECT_STABLE_MS 30000

// Whether a termination code of moonlight-common-c's connectionTerminated is a
// dropped connection worth another try: -100 no video traffic, -101 no video
// frame, -102 early termination, -1 the control stream timed out or was
// closed without a word, and the socket errors (an errno, positive) that a
// lost link ends with. Not 0 (the host ended the stream: the app exited or
// the user quit it there), -103 protected content and -104 frame conversion
// (the host cannot encode, retrying meets the same wall) or anything else.
int mlxr_reconnect_retryable(int termCode);

// What a try came to
typedef enum {
    MLXR_RC_OK = 0,      // the stream is up again
    MLXR_RC_FAILED = 1,  // did not connect: the next try, if any, may
    MLXR_RC_FATAL = 2    // refused for a reason a retry will not change (pairing, certificate, the host's session is gone)
} MlxrReconnectOutcome;

typedef enum {
    MLXR_RC_IDLE = 0,
    MLXR_RC_WAITING,     // a try is scheduled
    MLXR_RC_TRYING       // a try is under way (the caller is inside the connect)
} MlxrReconnectState;

typedef struct MlxrReconnect {
    int state;           // MlxrReconnectState
    int used;            // tries started in this series (kept when a short-lived reconnect drops again)
    int attempt;         // the try waiting or under way, 1-based; 0 idle
    int64_t dueMs;       // when the waiting try starts
    int64_t upMs;        // when the last try succeeded, -1 never
    int lastCode;        // the termination code that began the series
} MlxrReconnect;

void mlxr_reconnect_init(MlxrReconnect* r);

// The stream dropped with this code at nowMs. Returns 1 when a try was
// scheduled (state WAITING, attempt set), 0 when the drop is not worth one or
// the series has used its tries: the caller takes the old way out.
int mlxr_reconnect_drop(MlxrReconnect* r, int termCode, int64_t nowMs);

// Returns the try number to start now (1 or 2) and moves to TRYING, else 0
int mlxr_reconnect_due(MlxrReconnect* r, int64_t nowMs);

// How the try went. Returns 1 when another try was scheduled, 0 when the
// series is over (it worked, was fatal, or there are no tries left): idle.
int mlxr_reconnect_result(MlxrReconnect* r, MlxrReconnectOutcome outcome, int64_t nowMs);

// The user left (exit, disconnect): whatever was scheduled is dropped
void mlxr_reconnect_cancel(MlxrReconnect* r);

// 1 while a try is scheduled or under way
int mlxr_reconnect_active(const MlxrReconnect* r);

// ---- the loop around it, with the session behind hooks ----

typedef struct MlxrReconnectOps {
    void* user;
    // A try is scheduled: the notice "Connection lost, reconnecting (n/max)"
    void (*scheduled)(void* user, int attempt, int max, int termCode);
    // Make the try; the same app, resuming the host's session
    MlxrReconnectOutcome (*attempt)(void* user, int attempt);
    // The stream is back
    void (*resumed)(void* user, int attempt);
    // No more tries: the caller takes the old way out
    void (*gaveUp)(void* user, int termCode, MlxrReconnectOutcome last);
    // Optional: the time now, in the caller's milliseconds. A try can take
    // seconds, and the next delay counts from its end; without this it
    // counts from the poll's own time.
    int64_t (*nowMs)(void* user);
} MlxrReconnectOps;

typedef enum {
    MLXR_RC_EV_NONE = 0,   // nothing is scheduled
    MLXR_RC_EV_SCHEDULED,  // a try was scheduled (by the drop, or after a failed try)
    MLXR_RC_EV_RESUMED,
    MLXR_RC_EV_GAVE_UP,    // the drop was not worth a try, or the tries are used
    MLXR_RC_EV_WAITING     // a series is waiting for its time
} MlxrReconnectEvent;

// The stream dropped: decides, and calls scheduled() or gaveUp()
MlxrReconnectEvent mlxr_reconnect_on_drop(MlxrReconnect* r, const MlxrReconnectOps* ops, int termCode, int64_t nowMs);
// Once per loop turn: when a try is due, makes it and calls resumed(),
// scheduled() (the next try) or gaveUp()
MlxrReconnectEvent mlxr_reconnect_poll(MlxrReconnect* r, const MlxrReconnectOps* ops, int64_t nowMs);
