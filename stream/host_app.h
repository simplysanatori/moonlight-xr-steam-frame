// Moonlight XR Native (1.3.4): hosts that end the app when their last client leaves. GPLv3.
//
// Apollo and its forks (Vibepollo) have a per-app "terminate on pause" setting: when the last stream
// session of the host ends, the app is ended with it ("Terminating app [..] when all clients are
// disconnected"). The host says nothing of it to a client: /serverinfo and /applist carry no such field,
// so the client cannot know beforehand. Afterwards it can: /serverinfo's currentgame is 0, or /resume
// answers status_code 503 ("No running app to resume").
//
// Every way this client disconnects from a running app ends the last session on a one-client host: the
// network failover (a switch of link), a dropped link (it reconnects), the headset-off and idle timers and
// the user's own Disconnect (the chord, the keys, the bar's or the exit sheet's). moonlight-common-c holds
// one connection at a time, so a new session cannot be up before the old one ends.
//
// So what the PC did is looked at after each of those, and only those: by the reconnect's resume, or once
// MLXR_HOSTAPP_LOOK_AFTER_MS after the timers' and the user's disconnects (or at a launch of the same app
// before then). A sighting of the app still running before MLXR_HOSTAPP_KEPT_AFTER_MS counts only once the
// stream resumed with it is still up then (mlxr_hostapp_confirm_later). A quit (the bar's Exit, a quit asked first), a stream the host ended (the app quit or
// crashed) and a sighting outside MLXR_HOSTAPP_WINDOW_MS never count. What is found is kept per PC and app
// (host-apps.ini in the client's config directory, a file earlier versions never read): each sighting of the
// app ended adds one, a sighting of it still running clears them. The failover holds (stays on its link,
// losing video, rather than lose the game) from MLXR_HOSTAPP_HOLD_AFTER sightings. Each case has a notice,
// MLXR_HOSTAPP_TEXT_* (English in every language). No network, no clock: the caller passes what it saw.
#pragma once
#include <stddef.h>
#include <stdint.h>

// What is known of a PC and app: MLXR_HOSTAPP_KEEPS (seen running after the client left), 0 nothing, or how
// many times in a row it was seen ended (1 up to MLXR_HOSTAPP_ENDS_MAX)
#define MLXR_HOSTAPP_KEEPS (-1)
#define MLXR_HOSTAPP_UNKNOWN 0
#define MLXR_HOSTAPP_ENDS_MAX 9
#define MLXR_HOSTAPP_HOLD_AFTER 2

// A sighting counts within this long after the client left
#define MLXR_HOSTAPP_WINDOW_MS 120000
// and the app still running only from this long after: a host notices a dropped link at its ping timeout
// (10 s by default) and gives the app its exit timeout (5 s) to end, so a quicker resume can find it
// still there on a PC that is ending it
#define MLXR_HOSTAPP_KEPT_AFTER_MS 15000
// The timers' and the user's disconnects have no resume: /serverinfo is read once this long after
#define MLXR_HOSTAPP_LOOK_AFTER_MS 15000

// What a look after the client left found
typedef enum {
    MLXR_HOSTAPP_RESUMED = 0,   // the same app runs (resumed, or seen running)
    MLXR_HOSTAPP_ENDED,         // no app runs any more (currentgame 0, or /resume said 503 with no app)
    MLXR_HOSTAPP_OTHER,         // another app runs now: says nothing about the setting
    MLXR_HOSTAPP_UNSURE         // the host did not answer, refused for another reason, or it does not count
} mlxr_hostapp_outcome;

// Why the client left the session
typedef enum {
    MLXR_HOSTAPP_WHY_NONE = 0,       // a quit, a stream the host ended, nothing: never counts
    MLXR_HOSTAPP_WHY_FAILOVER,       // the network failover's switch of link
    MLXR_HOSTAPP_WHY_DROP,           // the link dropped
    MLXR_HOSTAPP_WHY_DOFF,           // the headset was off for doff_disconnect seconds
    MLXR_HOSTAPP_WHY_IDLE,           // no input for idle_disconnect minutes
    MLXR_HOSTAPP_WHY_DISCONNECT      // the user's Disconnect: the chord, the keys, the bar or the exit sheet
} mlxr_hostapp_why;

// What the headset says
typedef enum {
    MLXR_HOSTAPP_NOTICE_NONE = 0,
    MLXR_HOSTAPP_NOTICE_HELD,            // the failover did not switch: this PC ends the app when the client leaves
    MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END,  // the failover switches on a PC not known to keep the app
    MLXR_HOSTAPP_NOTICE_ENDED,           // after the switch or the drop, the PC had ended the app
    MLXR_HOSTAPP_NOTICE_MAY_END          // the headset-off or idle disconnect on a PC seen ending the app
} mlxr_hostapp_notice;

// The notices' words (approved, 2026-11-17). SWITCH takes the link switched to, as the failover's own
// notice names it; IDLE the minutes and DOFF the seconds of the user's settings.
#define MLXR_HOSTAPP_TEXT_HELD "Video keeps breaking up. Not switching networks: this PC would end the app"
#define MLXR_HOSTAPP_TEXT_SWITCH "Switching to %s. A PC set to end the app on disconnect will close it"
#define MLXR_HOSTAPP_TEXT_ENDED "The PC ended the app when the connection dropped. Start it again from the app list"
#define MLXR_HOSTAPP_TEXT_IDLE "No input for %d min: disconnecting. This PC may end the app"
#define MLXR_HOSTAPP_TEXT_DOFF "Headset off for %d s: disconnecting. This PC may end the app"

// A resume's findings: appId the app streamed before; currentGame /serverinfo's currentgame read for the
// resume (-1 when it could not be read); resumeStatus the resume's status_code (0 not tried or no answer,
// 200 accepted) and resumeMessage its status_message (NULL or "" when none). A 503 is an ended app only
// when it says no app runs: Sunshine-family hosts also answer 503 at their concurrent stream limit, which
// a quick reconnect can meet while the host still counts the session it lost.
mlxr_hostapp_outcome mlxr_hostapp_outcome_of(int appId, int currentGame, int resumeStatus, const char* resumeMessage);
// A look without a resume: /serverinfo's currentgame (-1 not read)
mlxr_hostapp_outcome mlxr_hostapp_outcome_seen(int appId, int currentGame);
// Whether an outcome counts: why the client left and how long before (ms); UNSURE when it does not
mlxr_hostapp_outcome mlxr_hostapp_counted(mlxr_hostapp_why why, int64_t sinceLeftMs, mlxr_hostapp_outcome o);
// Whether an early sighting of the app still running (before MLXR_HOSTAPP_KEPT_AFTER_MS) is looked at again:
// it counts as RESUMED when the stream resumed with it is still up MLXR_HOSTAPP_KEPT_AFTER_MS after the leave
// (an app the PC was ending would have ended that stream). Only after a leave the host sees at once (the
// failover's switch, the timers, a Disconnect): after a dropped link the host can still hold the lost
// session, and the app then stays for the new one whatever the setting.
int mlxr_hostapp_confirm_later(mlxr_hostapp_why why, int64_t sinceLeftMs, mlxr_hostapp_outcome o);
// What is known after a counted outcome (OTHER and UNSURE keep what was known)
int mlxr_hostapp_learn(int before, mlxr_hostapp_outcome counted);
// Whether the client may leave for this reason: not the failover on a PC and app seen ending the app
// MLXR_HOSTAPP_HOLD_AFTER times; the others are not the client's to hold
int mlxr_hostapp_may_leave(int known, mlxr_hostapp_why why);
// The notice before leaving (NONE when there is nothing to say)
mlxr_hostapp_notice mlxr_hostapp_notice_before(int known, mlxr_hostapp_why why);
// The notice after a resume's outcome (as found, counted or not)
mlxr_hostapp_notice mlxr_hostapp_notice_after(mlxr_hostapp_why why, mlxr_hostapp_outcome o);
// The failover's answer for mayLeave (failover.h): 0 hold, 1 switch, 2 switch and say the app may end
int mlxr_hostapp_failover_answer(int known);
// "keeps", "unknown", "ends 1", "ends 2", ...
const char* mlxr_hostapp_known_name(int known, char* buf, size_t cap);
const char* mlxr_hostapp_notice_name(mlxr_hostapp_notice n);
const char* mlxr_hostapp_why_name(mlxr_hostapp_why w);

// The file: <dir>/host-apps.ini, one line a PC and app, "<host uuid> <app id> keeps" or "... ends <n>". A
// missing file, PC or app is UNKNOWN; unreadable lines are passed over. save rewrites the file through a
// temporary one, keeping the other lines (at most MLXR_HOSTAPP_MAX, the oldest dropped; UNKNOWN takes the
// line away); 0 ok, -1 failed.
#define MLXR_HOSTAPP_MAX 64
int mlxr_hostapp_load(const char* dir, const char* uuid, int appId);
int mlxr_hostapp_save(const char* dir, const char* uuid, int appId, int known);
