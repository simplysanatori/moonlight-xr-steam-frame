// Moonlight XR Native (1.3.4): hosts that end the app when their last client leaves (host_app.h). GPLv3.
#include "host_app.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Apollo's and Vibepollo's /resume with no app running: status_code 503, "No running app to resume"
#define RESUME_NO_APP 503

mlxr_hostapp_outcome mlxr_hostapp_outcome_of(int appId, int currentGame, int resumeStatus) {
    if (resumeStatus == RESUME_NO_APP) return MLXR_HOSTAPP_ENDED;
    if (currentGame < 0) return MLXR_HOSTAPP_UNSURE;
    if (currentGame == 0) return MLXR_HOSTAPP_ENDED;
    if (currentGame != appId) return MLXR_HOSTAPP_OTHER;
    // The app is there: resumed, or asked and refused for some other reason
    return resumeStatus == 200 ? MLXR_HOSTAPP_RESUMED : MLXR_HOSTAPP_UNSURE;
}

mlxr_hostapp_keep mlxr_hostapp_learn(mlxr_hostapp_keep before, mlxr_hostapp_outcome o) {
    if (o == MLXR_HOSTAPP_RESUMED) return MLXR_HOSTAPP_KEEPS;
    if (o == MLXR_HOSTAPP_ENDED) return MLXR_HOSTAPP_ENDS;
    return before;
}

int mlxr_hostapp_may_leave(mlxr_hostapp_keep k, mlxr_hostapp_why why) {
    return !(why == MLXR_HOSTAPP_WHY_FAILOVER && k == MLXR_HOSTAPP_ENDS);
}

mlxr_hostapp_notice mlxr_hostapp_notice_before(mlxr_hostapp_keep k, mlxr_hostapp_why why) {
    switch (why) {
    case MLXR_HOSTAPP_WHY_FAILOVER:
        return k == MLXR_HOSTAPP_ENDS ? MLXR_HOSTAPP_NOTICE_HELD
             : k == MLXR_HOSTAPP_UNKNOWN ? MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END : MLXR_HOSTAPP_NOTICE_NONE;
    case MLXR_HOSTAPP_WHY_DOFF:
    case MLXR_HOSTAPP_WHY_IDLE:
        return k == MLXR_HOSTAPP_ENDS ? MLXR_HOSTAPP_NOTICE_MAY_END : MLXR_HOSTAPP_NOTICE_NONE;
    default:
        return MLXR_HOSTAPP_NOTICE_NONE;   // a drop has already happened: the outcome says
    }
}

mlxr_hostapp_notice mlxr_hostapp_notice_after(mlxr_hostapp_why why, mlxr_hostapp_outcome o) {
    if (o != MLXR_HOSTAPP_ENDED) return MLXR_HOSTAPP_NOTICE_NONE;
    // The timers end the stream on purpose and do not resume: only the failover and a drop come here
    return why == MLXR_HOSTAPP_WHY_FAILOVER || why == MLXR_HOSTAPP_WHY_DROP ? MLXR_HOSTAPP_NOTICE_ENDED
                                                                          : MLXR_HOSTAPP_NOTICE_NONE;
}

const char* mlxr_hostapp_keep_name(mlxr_hostapp_keep k) {
    return k == MLXR_HOSTAPP_KEEPS ? "keeps" : k == MLXR_HOSTAPP_ENDS ? "ends" : "unknown";
}

const char* mlxr_hostapp_notice_name(mlxr_hostapp_notice n) {
    switch (n) {
    case MLXR_HOSTAPP_NOTICE_HELD: return "held";
    case MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END: return "switch-may-end";
    case MLXR_HOSTAPP_NOTICE_ENDED: return "ended";
    case MLXR_HOSTAPP_NOTICE_MAY_END: return "may-end";
    default: return "none";
    }
}

const char* mlxr_hostapp_why_name(mlxr_hostapp_why w) {
    switch (w) {
    case MLXR_HOSTAPP_WHY_FAILOVER: return "the network failover";
    case MLXR_HOSTAPP_WHY_DROP: return "a dropped link";
    case MLXR_HOSTAPP_WHY_DOFF: return "the headset-off timer";
    case MLXR_HOSTAPP_WHY_IDLE: return "the idle timer";
    default: return "?";
    }
}

// ---- the file

typedef struct {
    char uuid[72];
    int app;
    mlxr_hostapp_keep keep;
} Entry;

static int pathOf(const char* dir, char* out, size_t cap) {
    if (dir == NULL || !*dir) return 0;
    int n = snprintf(out, cap, "%s/host-apps.ini", dir);
    return n > 0 && (size_t)n < cap;
}

// Reads every good line, the oldest first; the count back
static int readAll(const char* dir, Entry* e, int cap) {
    char path[600];
    if (!pathOf(dir, path, sizeof path)) return 0;
    FILE* f = fopen(path, "r");
    if (f == NULL) return 0;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        char uuid[72], word[16];
        int app = 0;
        if (line[0] == '#' || sscanf(line, "%71s %d %15s", uuid, &app, word) != 3 || app <= 0) continue;
        mlxr_hostapp_keep k = !strcmp(word, "ends") ? MLXR_HOSTAPP_ENDS
                            : !strcmp(word, "keeps") ? MLXR_HOSTAPP_KEEPS : MLXR_HOSTAPP_UNKNOWN;
        if (k == MLXR_HOSTAPP_UNKNOWN) continue;
        // a later line for the same PC and app wins
        int at = -1;
        for (int i = 0; i < n; i++) {
            if (e[i].app == app && !strcmp(e[i].uuid, uuid)) at = i;
        }
        if (at < 0) {
            if (n == cap) {
                memmove(e, e + 1, sizeof(Entry) * (size_t)(cap - 1));
                n--;
            }
            at = n++;
        }
        snprintf(e[at].uuid, sizeof e[at].uuid, "%s", uuid);
        e[at].app = app;
        e[at].keep = k;
    }
    fclose(f);
    return n;
}

mlxr_hostapp_keep mlxr_hostapp_load(const char* dir, const char* uuid, int appId) {
    if (uuid == NULL || !*uuid || appId <= 0) return MLXR_HOSTAPP_UNKNOWN;
    Entry e[MLXR_HOSTAPP_MAX];
    int n = readAll(dir, e, MLXR_HOSTAPP_MAX);
    for (int i = 0; i < n; i++) {
        if (e[i].app == appId && !strcmp(e[i].uuid, uuid)) return e[i].keep;
    }
    return MLXR_HOSTAPP_UNKNOWN;
}

int mlxr_hostapp_save(const char* dir, const char* uuid, int appId, mlxr_hostapp_keep k) {
    char path[600], tmp[620];
    if (uuid == NULL || !*uuid || appId <= 0 || strpbrk(uuid, " \t\r\n") != NULL || !pathOf(dir, path, sizeof path))
        return -1;
    Entry e[MLXR_HOSTAPP_MAX];
    int n = readAll(dir, e, MLXR_HOSTAPP_MAX);
    // this PC and app move to the end (the newest), or go when unknown
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (!(e[i].app == appId && !strcmp(e[i].uuid, uuid))) e[w++] = e[i];
    }
    n = w;
    if (k != MLXR_HOSTAPP_UNKNOWN) {
        if (n == MLXR_HOSTAPP_MAX) {
            memmove(e, e + 1, sizeof(Entry) * (MLXR_HOSTAPP_MAX - 1));
            n--;
        }
        snprintf(e[n].uuid, sizeof e[n].uuid, "%s", uuid);
        e[n].app = appId;
        e[n].keep = k;
        n++;
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "w");
    if (f == NULL) return -1;
    fprintf(f, "# Moonlight XR for Frame: what each PC did with its app when this client left it (keeps or ends)\n");
    for (int i = 0; i < n; i++) fprintf(f, "%s %d %s\n", e[i].uuid, e[i].app, mlxr_hostapp_keep_name(e[i].keep));
    int bad = ferror(f);
    if (fclose(f) != 0 || bad || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}
