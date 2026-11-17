// Moonlight XR Native (1.3.4): hosts that end the app when their last client leaves. GPLv3.
//
// Apollo and its forks (Vibepollo) have a per-app "terminate on pause" setting: when the last stream
// session of the host ends, the app is ended with it ("Terminating app [..] when all clients are
// disconnected"). The host says nothing of it to a client: /serverinfo and /applist carry no such field,
// so the client cannot know beforehand. Afterwards it can: a resume finds currentgame 0 in /serverinfo,
// or /resume answers status_code 503 ("No running app to resume").
//
// Every way this client disconnects from a running app ends the last session on a one-client host: the
// network failover (a switch of link, which this client chooses), a dropped link (it reconnects), the
// headset-off and idle timers. moonlight-common-c holds one connection at a time, so the new session
// cannot be up before the old one ends.
//
// So: what a resume after a disconnect finds is remembered per PC and app (host-apps.ini in the client's
// config directory, a file earlier versions never read). A PC and app known to end the app are not
// switched by the failover (the stream stays on its link, losing video, rather than lose the game), and
// each case has a notice kind for the headset to say. No network, no clock: the caller passes what it saw.
#pragma once
#include <stddef.h>

typedef enum {
    MLXR_HOSTAPP_UNKNOWN = 0,   // never seen a disconnect and a resume
    MLXR_HOSTAPP_KEEPS,         // the app was there to resume after a disconnect
    MLXR_HOSTAPP_ENDS           // the host ended it when the client left
} mlxr_hostapp_keep;

// What a resume after a disconnect found
typedef enum {
    MLXR_HOSTAPP_RESUMED = 0,   // the same app was running and resumed
    MLXR_HOSTAPP_ENDED,         // no app runs any more (currentgame 0, or /resume said 503)
    MLXR_HOSTAPP_OTHER,         // another app runs now: says nothing about the setting
    MLXR_HOSTAPP_UNSURE         // the host did not answer, or refused for another reason
} mlxr_hostapp_outcome;

// Why the client left the session
typedef enum {
    MLXR_HOSTAPP_WHY_FAILOVER = 0,   // the network failover's switch of link (the client's own choice)
    MLXR_HOSTAPP_WHY_DROP,           // the link dropped
    MLXR_HOSTAPP_WHY_DOFF,           // the headset was off for doff_disconnect seconds
    MLXR_HOSTAPP_WHY_IDLE            // no input for idle_disconnect minutes
} mlxr_hostapp_why;

// What the headset is to say (the wording waits for the owner: the log names the kind meanwhile)
typedef enum {
    MLXR_HOSTAPP_NOTICE_NONE = 0,
    MLXR_HOSTAPP_NOTICE_HELD,            // the failover did not switch: this PC ends the app when the client leaves
    MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END,  // the failover switches on a PC not known either way: the app may end
    MLXR_HOSTAPP_NOTICE_ENDED,           // after the switch or the drop, the PC had ended the app
    MLXR_HOSTAPP_NOTICE_MAY_END          // the headset-off or idle disconnect on a PC known to end the app
} mlxr_hostapp_notice;

// A resume's findings: appId the app streamed before; currentGame /serverinfo's currentgame read for the
// resume (-1 when it could not be read); resumeStatus the resume's status_code (0 not tried or no answer,
// 200 accepted).
mlxr_hostapp_outcome mlxr_hostapp_outcome_of(int appId, int currentGame, int resumeStatus);
// What is known after an outcome (OTHER and UNSURE keep what was known)
mlxr_hostapp_keep mlxr_hostapp_learn(mlxr_hostapp_keep before, mlxr_hostapp_outcome o);
// Whether the client may leave the session for this reason: the failover not on a PC and app known to end
// the app; the others are not the client's to hold (a drop has happened, the timers are the user's).
int mlxr_hostapp_may_leave(mlxr_hostapp_keep k, mlxr_hostapp_why why);
// The notice before leaving (NONE when there is nothing to say)
mlxr_hostapp_notice mlxr_hostapp_notice_before(mlxr_hostapp_keep k, mlxr_hostapp_why why);
// The notice after a resume's outcome
mlxr_hostapp_notice mlxr_hostapp_notice_after(mlxr_hostapp_why why, mlxr_hostapp_outcome o);
const char* mlxr_hostapp_keep_name(mlxr_hostapp_keep k);       // "unknown", "keeps", "ends"
const char* mlxr_hostapp_notice_name(mlxr_hostapp_notice n);
const char* mlxr_hostapp_why_name(mlxr_hostapp_why w);

// The file: <dir>/host-apps.ini, one line a PC and app, "<host uuid> <app id> keeps|ends". A missing file,
// PC or app is UNKNOWN; unreadable lines are passed over. save rewrites the file through a temporary one,
// keeping the other lines (at most MLXR_HOSTAPP_MAX, the oldest dropped); 0 ok, -1 failed.
#define MLXR_HOSTAPP_MAX 64
mlxr_hostapp_keep mlxr_hostapp_load(const char* dir, const char* uuid, int appId);
int mlxr_hostapp_save(const char* dir, const char* uuid, int appId, mlxr_hostapp_keep k);
