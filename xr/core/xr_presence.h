// Headset on and off, and a stream nobody is using: the timers behind the
// "headset off" and "idle" disconnects. Plain arithmetic on a clock passed in,
// with no OpenXR, stream or context, so the host tests reach all of it.
//
// The runtime says when the headset comes off or goes back on (the
// XR_EXT_user_presence event). Off, the app mutes and lets go of its input
// at once (the caller's job); after doffSeconds it disconnects from the host
// without ending the host's session, and a don before then cancels that. A
// separate idle timer counts minutes without any input and does the same
// disconnect. Either limit is 0 for never.
//
// Not thread safe: the caller locks (the event comes on the frame loop, the
// timers are read on the worker thread).

#ifndef XR_PRESENCE_H
#define XR_PRESENCE_H

#include <stdint.h>

// What a call tells the caller to do
#define PRESENCE_NONE 0
#define PRESENCE_DOFF 1             // the headset just came off: mute, let go of input
#define PRESENCE_DON 2              // it just went back on: unmute
#define PRESENCE_DISCONNECT_DOFF 3  // it has been off for doffSeconds: disconnect, keep the host's session
#define PRESENCE_DISCONNECT_IDLE 4  // idleMinutes without input: the same

typedef struct {
    int present;              // 1 until a doff, back to 1 on a don
    int64_t offSinceNs;       // when it came off (while !present)
    int64_t lastInputNs;      // the last input, or the stream's start
    int doffSeconds;          // 0 never
    int idleMinutes;          // 0 never
    int doffFired;            // the doff disconnect has been said for this doff
    int idleFired;            // the idle disconnect has been said since the last input
} PresenceState;

// Present, clocks at now. The limits are as presenceSetLimits takes them.
void presenceInit(PresenceState* p, int doffSeconds, int idleMinutes, int64_t nowNs);

// Seconds (0 never, at most 3600) and minutes (0 never, at most 1440); out of range is held to it
void presenceSetLimits(PresenceState* p, int doffSeconds, int idleMinutes);

// A new stream: the idle clock starts now and nothing has been said. The
// headset's state is kept (a stream can begin with it off).
void presenceReset(PresenceState* p, int64_t nowNs);

// The runtime's report. Returns PRESENCE_DOFF or PRESENCE_DON on a change,
// PRESENCE_NONE for a repeat. A don counts as input.
int presenceEvent(PresenceState* p, int present, int64_t nowNs);

// Something was pressed, moved or typed: the idle clock starts over
void presenceInput(PresenceState* p, int64_t nowNs);

// Once a turn of the worker's loop. Returns PRESENCE_DISCONNECT_DOFF or
// PRESENCE_DISCONNECT_IDLE once when a limit is reached while streaming (the
// doff one first if both are), otherwise PRESENCE_NONE. Neither limit is
// counted while not streaming.
int presenceTick(PresenceState* p, int64_t nowNs, int streaming);

// Seconds the headset has been off, 0 while it is on
double presenceOffSeconds(const PresenceState* p, int64_t nowNs);

// ---- the activity clock: any thread ----
// The input threads note that something happened; the worker reads it and
// hands it to presenceInput. A monotonic nanosecond clock in an atomic.
void presenceNoteActivity(void);
// The last noted activity, 0 when none
int64_t presenceActivityNs(void);
// The clock the above uses
int64_t presenceNowNs(void);

#endif
