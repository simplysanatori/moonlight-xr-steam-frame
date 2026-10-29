// Moonlight XR Native: the launcher's state machine (see launcher.h). GPLv3.
#include "launcher.h"

#include <stdio.h>
#include <string.h>

const char* launcher_state_name(LaunchState s) {
    static const char* const N[] = { "pcs", "apps", "starting", "streaming", "stopping", "quit" };
    return (unsigned)s < sizeof(N) / sizeof(N[0]) ? N[s] : "?";
}

static void push(Launcher* l, LaunchCmdKind kind, int pc, int appId, int quitFirst, int quitHost) {
    if (l->ncmd >= (int)(sizeof(l->cmds) / sizeof(l->cmds[0]))) {
        // A full queue drops the oldest: the newest press is what was meant
        memmove(&l->cmds[0], &l->cmds[1], sizeof(l->cmds[0]) * (size_t)(l->ncmd - 1));
        l->ncmd--;
    }
    LaunchCmd* c = &l->cmds[l->ncmd++];
    c->kind = kind;
    c->pc = pc;
    c->appId = appId;
    c->quitFirst = quitFirst;
    c->quitHost = quitHost;
}

static void say(Launcher* l, const char* fmt, const char* arg) {
    snprintf(l->notice, sizeof(l->notice), fmt, arg != NULL ? arg : "");
}

void launcher_init(Launcher* l) {
    memset(l, 0, sizeof(*l));
    l->state = LAUNCH_PCS;
    l->pc = -1;
    push(l, LCMD_REFRESH_PCS, -1, 0, 0, 0);
}

void launcher_init_file(Launcher* l) {
    memset(l, 0, sizeof(*l));
    l->state = LAUNCH_STREAMING;
    l->pc = -1;
    l->fileMode = 1;
}

void launcher_hosts(Launcher* l, const LaunchPc* pcs, int n) {
    if (n > LAUNCH_MAX_PCS) n = LAUNCH_MAX_PCS;
    for (int i = 0; i < n; i++) {
        l->pcs[i] = pcs[i];
    }
    l->pcCount = n;
    if (l->pcPage * UI_LIST_ROWS >= n) l->pcPage = 0;
}

void launcher_pc_status(Launcher* l, int pc, int online, int paired, int currentGame) {
    if (pc < 0 || pc >= l->pcCount) return;
    l->pcs[pc].online = online;
    l->pcs[pc].paired = paired;
    l->pcs[pc].currentGame = currentGame;
}

void launcher_apps(Launcher* l, int pc, const LaunchApp* apps, int n, int ok) {
    if (pc != l->pc) return;   // a list for a PC no longer shown
    l->appsLoading = 0;
    l->appsFailed = !ok;
    if (n > LAUNCH_MAX_APPS) n = LAUNCH_MAX_APPS;
    l->appCount = ok ? n : 0;
    for (int i = 0; i < l->appCount; i++) {
        l->apps[i] = apps[i];
    }
    if (l->appPage * UI_LIST_ROWS >= l->appCount) l->appPage = 0;
}

void launcher_autostart(Launcher* l, int pc, int appId) {
    if (pc < 0 || pc >= l->pcCount) return;
    l->pc = pc;
    l->appPage = 0;
    l->appsLoading = 1;
    l->startingApp = appId;
    l->state = LAUNCH_STARTING;
    int other = l->pcs[pc].currentGame != 0 && l->pcs[pc].currentGame != appId;
    push(l, LCMD_LAUNCH, pc, appId, other, 0);
}

void launcher_stream_started(Launcher* l) {
    if (l->state == LAUNCH_STARTING) {
        l->state = LAUNCH_STREAMING;
        if (l->pc >= 0) l->pcs[l->pc].currentGame = l->startingApp;
    }
}

void launcher_stream_failed(Launcher* l, const char* why) {
    if (l->fileMode) {
        l->state = LAUNCH_QUIT;
        push(l, LCMD_EXIT, -1, 0, 0, 0);
        return;
    }
    l->state = l->pc >= 0 ? LAUNCH_APPS : LAUNCH_PCS;
    say(l, "%s", why != NULL ? why : ui_str(UIS_NATIVE_STREAM_FAILED));
    if (l->pc >= 0) {
        l->appsLoading = 1;
        push(l, LCMD_FETCH_APPS, l->pc, 0, 0, 0);
    }
}

void launcher_stream_ended(Launcher* l, int byHost) {
    if (l->fileMode) {
        l->state = LAUNCH_QUIT;
        push(l, LCMD_EXIT, -1, 0, 0, 0);
        return;
    }
    if (l->state == LAUNCH_QUIT) {
        return;
    }
    if (byHost) {
        say(l, "%s", ui_str(UIS_NATIVE_STREAM_ENDED));
    }
    if (l->pc >= 0) {
        l->pcs[l->pc].currentGame = byHost ? l->pcs[l->pc].currentGame : 0;
        l->state = LAUNCH_APPS;
        l->appsLoading = 1;
        push(l, LCMD_FETCH_APPS, l->pc, 0, 0, 0);
    }
    else {
        l->state = LAUNCH_PCS;
    }
}

static void pressPc(Launcher* l, int i) {
    if (i < 0 || i >= l->pcCount) return;
    LaunchPc* pc = &l->pcs[i];
    if (!pc->paired) {
        snprintf(l->notice, sizeof(l->notice), ui_str(UIS_NATIVE_NOT_PAIRED), pc->name);
        return;
    }
    if (pc->online != UI_PC_ONLINE) {
        if (pc->canWake) {
            snprintf(l->notice, sizeof(l->notice), ui_str(UIS_NATIVE_OFFLINE_WAKE), pc->name);
            pc->online = UI_PC_REFRESHING;
            push(l, LCMD_WAKE, i, 0, 0, 0);
        }
        else {
            push(l, LCMD_REFRESH_PCS, -1, 0, 0, 0);
            pc->online = UI_PC_REFRESHING;
        }
        return;
    }
    l->pc = i;
    l->state = LAUNCH_APPS;
    l->appCount = 0;
    l->appPage = 0;
    l->appsLoading = 1;
    l->appsFailed = 0;
    push(l, LCMD_FETCH_APPS, i, 0, 0, 0);
}

static void pressApp(Launcher* l, int i) {
    if (i < 0 || i >= l->appCount || l->pc < 0 || l->appsLoading) return;
    int id = l->apps[i].id;
    l->startingApp = id;
    l->state = LAUNCH_STARTING;
    snprintf(l->notice, sizeof(l->notice), ui_str(UIS_NATIVE_STARTING), l->apps[i].name);
    // Another app running on the host ends first, as Moonlight asks before
    int other = l->pcs[l->pc].currentGame != 0 && l->pcs[l->pc].currentGame != id;
    push(l, LCMD_LAUNCH, l->pc, id, other, 0);
}

static void page(int* p, int count, int dir) {
    int pages = (count + UI_LIST_ROWS - 1) / UI_LIST_ROWS;
    if (pages < 1) pages = 1;
    *p += dir;
    if (*p < 0) *p = 0;
    if (*p >= pages) *p = pages - 1;
}

void launcher_press(Launcher* l, int kind, int a, int b) {
    (void)b;
    if (l->state == LAUNCH_PCS) {
        if (kind == UI_HIT_LIST_ITEM) pressPc(l, a);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_PAGE_UP) page(&l->pcPage, l->pcCount, -1);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_PAGE_DOWN) page(&l->pcPage, l->pcCount, 1);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_ADD) say(l, "%s", ui_str(UIS_NATIVE_PAIR_LATER));
    }
    else if (l->state == LAUNCH_APPS) {
        if (kind == UI_HIT_LIST_ITEM) pressApp(l, a);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_PAGE_UP) page(&l->appPage, l->appCount, -1);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_PAGE_DOWN) page(&l->appPage, l->appCount, 1);
        else if (kind == UI_HIT_BUTTON && a == UI_BTN_LIST_BACK) {
            l->state = LAUNCH_PCS;
            l->pc = -1;
            push(l, LCMD_REFRESH_PCS, -1, 0, 0, 0);
        }
    }
}

void launcher_exit(Launcher* l) {
    switch (l->state) {
    case LAUNCH_STREAMING:
    case LAUNCH_STARTING:
        if (l->fileMode) {
            l->state = LAUNCH_QUIT;
            push(l, LCMD_EXIT, -1, 0, 0, 0);
            break;
        }
        // Leaving a stream ends the host's session too (the owner's rule),
        // then the app list of that PC comes back
        l->state = LAUNCH_STOPPING;
        if (l->pc >= 0) {
            snprintf(l->notice, sizeof(l->notice), ui_str(UIS_NATIVE_QUITTING), l->pcs[l->pc].name);
        }
        push(l, LCMD_STOP, l->pc, 0, 0, 1);
        break;
    case LAUNCH_PCS:
    case LAUNCH_APPS:
        l->state = LAUNCH_QUIT;
        push(l, LCMD_EXIT, -1, 0, 0, 0);
        break;
    default:
        break;
    }
}

static void disconnectWith(Launcher* l, UiStr notice) {
    if (l->state != LAUNCH_STREAMING && l->state != LAUNCH_STARTING) {
        return;
    }
    if (l->fileMode) {
        launcher_exit(l);
        return;
    }
    l->state = LAUNCH_STOPPING;
    snprintf(l->notice, sizeof(l->notice), "%s", ui_str(notice));
    push(l, LCMD_STOP, l->pc, 0, 0, 0);
}

void launcher_disconnect(Launcher* l) {
    disconnectWith(l, UIS_NATIVE_DISCONNECTED);
}

void launcher_disconnect_keys(Launcher* l) {
    disconnectWith(l, UIS_NATIVE_DISCONNECTED_KEYS);
}

void launcher_disconnect_bar(Launcher* l) {
    disconnectWith(l, UIS_NATIVE_DISCONNECTED_BAR);
}

int launcher_take_cmd(Launcher* l, LaunchCmd* out) {
    if (l->ncmd == 0) return 0;
    *out = l->cmds[0];
    memmove(&l->cmds[0], &l->cmds[1], sizeof(l->cmds[0]) * (size_t)(l->ncmd - 1));
    l->ncmd--;
    return 1;
}

void launcher_view(Launcher* l, LaunchView* v) {
    memset(v, 0, sizeof(*v));
    if (l->state == LAUNCH_PCS) {
        for (int i = 0; i < l->pcCount; i++) {
            UiPcItem* it = &l->pcItems[i];
            memset(it, 0, sizeof(*it));
            snprintf(it->name, sizeof(it->name), "%s", l->pcs[i].name);
            it->state = l->pcs[i].online;
            it->paired = l->pcs[i].paired;
            it->game_running = l->pcs[i].currentGame != 0;
        }
        v->mode = 1;
        v->pcs = l->pcItems;
        v->pcCount = l->pcCount;
        v->page = l->pcPage;
    }
    else if (l->state == LAUNCH_APPS || l->state == LAUNCH_STARTING || l->state == LAUNCH_STOPPING) {
        int running = l->pc >= 0 ? l->pcs[l->pc].currentGame : 0;
        for (int i = 0; i < l->appCount; i++) {
            UiAppItem* it = &l->appItems[i];
            memset(it, 0, sizeof(*it));
            snprintf(it->name, sizeof(it->name), "%s", l->apps[i].name);
            it->running = running != 0 && l->apps[i].id == running;
        }
        v->mode = 2;
        v->apps = l->appItems;
        v->appCount = l->appCount;
        v->pcName = l->pc >= 0 ? l->pcs[l->pc].name : "";
        v->page = l->appPage;
        v->loading = l->appsLoading || l->state != LAUNCH_APPS;
        v->failed = l->appsFailed;
    }
}
