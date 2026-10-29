// Moonlight XR Native, tests/m4: the launcher's state machine (app/launcher.c).
// GPLv3.
#include <string.h>

#include "check.h"
#include "launcher.h"

static int take(Launcher* l, LaunchCmdKind kind, LaunchCmd* out) {
    LaunchCmd c;
    while (launcher_take_cmd(l, &c)) {
        if (c.kind == kind) {
            if (out) *out = c;
            return 1;
        }
    }
    return 0;
}

static void hosts(Launcher* l) {
    LaunchPc pcs[3];
    memset(pcs, 0, sizeof(pcs));
    strcpy(pcs[0].name, "gaming-pc");
    pcs[0].paired = 1;
    pcs[0].canWake = 1;
    strcpy(pcs[1].name, "laptop");
    pcs[1].paired = 0;
    strcpy(pcs[2].name, "offline-pc");
    pcs[2].paired = 1;
    launcher_hosts(l, pcs, 3);
    launcher_pc_status(l, 0, UI_PC_ONLINE, 1, 0);
    launcher_pc_status(l, 1, UI_PC_ONLINE, 0, 0);
    launcher_pc_status(l, 2, UI_PC_OFFLINE, 1, 0);
}

static void apps(Launcher* l, int pc) {
    LaunchApp a[7];
    for (int i = 0; i < 7; i++) {
        snprintf(a[i].name, sizeof(a[i].name), "App %d", i);
        a[i].id = 100 + i;
    }
    launcher_apps(l, pc, a, 7, 1);
}

int main(void) {
    Launcher l;
    LaunchCmd c;
    LaunchView v;

    // The PC list first, refreshed
    launcher_init(&l);
    CHECK(l.state == LAUNCH_PCS);
    CHECK(take(&l, LCMD_REFRESH_PCS, NULL));
    hosts(&l);
    launcher_view(&l, &v);
    CHECK(v.mode == 1 && v.pcCount == 3 && v.pcs[0].state == UI_PC_ONLINE && !v.pcs[1].paired);

    // Not paired: a toast, nothing else
    launcher_press(&l, UI_HIT_LIST_ITEM, 1, 0);
    CHECK(l.state == LAUNCH_PCS && l.notice[0] != '\0' && l.ncmd == 0);
    l.notice[0] = '\0';
    // Offline without a MAC: refresh; Add PC: a toast (pairing comes later)
    launcher_press(&l, UI_HIT_LIST_ITEM, 2, 0);
    CHECK(take(&l, LCMD_REFRESH_PCS, NULL) && l.state == LAUNCH_PCS);
    launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_ADD, 0);
    CHECK(l.notice[0] != '\0');
    l.notice[0] = '\0';

    // Online and paired: its apps
    launcher_press(&l, UI_HIT_LIST_ITEM, 0, 0);
    CHECK(l.state == LAUNCH_APPS && l.pc == 0 && take(&l, LCMD_FETCH_APPS, &c) && c.pc == 0);
    launcher_view(&l, &v);
    CHECK(v.mode == 2 && v.loading);
    // A press while the list loads does nothing
    launcher_press(&l, UI_HIT_LIST_ITEM, 0, 0);
    CHECK(l.state == LAUNCH_APPS);
    apps(&l, 0);
    launcher_view(&l, &v);
    CHECK(v.mode == 2 && !v.loading && v.appCount == 7 && strcmp(v.pcName, "gaming-pc") == 0);
    // Pages
    launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_PAGE_DOWN, 0);
    CHECK(l.appPage == 1);
    launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_PAGE_DOWN, 0);
    CHECK(l.appPage == 1);
    launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_PAGE_UP, 0);
    CHECK(l.appPage == 0);

    // Launch, stream, exit: the stream stops with the host's session ended,
    // and the app list comes back
    launcher_press(&l, UI_HIT_LIST_ITEM, 3, 0);
    CHECK(l.state == LAUNCH_STARTING && take(&l, LCMD_LAUNCH, &c) && c.pc == 0 && c.appId == 103 && !c.quitFirst);
    launcher_view(&l, &v);
    CHECK(v.mode == 2 && v.loading);
    launcher_stream_started(&l);
    CHECK(l.state == LAUNCH_STREAMING);
    launcher_view(&l, &v);
    CHECK(v.mode == 0);
    launcher_exit(&l);
    CHECK(l.state == LAUNCH_STOPPING && take(&l, LCMD_STOP, &c) && c.quitHost == 1);
    launcher_stream_ended(&l, 0);
    CHECK(l.state == LAUNCH_APPS && take(&l, LCMD_FETCH_APPS, NULL));
    apps(&l, 0);

    // The exit prompt's Disconnect: the stream stops but the host's session
    // stays (quitHost 0), with its own notice, and the app list comes back
    launcher_press(&l, UI_HIT_LIST_ITEM, 3, 0);
    CHECK(take(&l, LCMD_LAUNCH, &c));
    launcher_stream_started(&l);
    launcher_disconnect_bar(&l);
    CHECK(l.state == LAUNCH_STOPPING && take(&l, LCMD_STOP, &c) && c.quitHost == 0);
    CHECK(strcmp(l.notice, ui_str(UIS_NATIVE_DISCONNECTED_BAR)) == 0);
    l.notice[0] = '\0';
    launcher_stream_ended(&l, 0);
    CHECK(l.state == LAUNCH_APPS && take(&l, LCMD_FETCH_APPS, NULL));
    apps(&l, 0);
    // outside a stream it does nothing
    launcher_disconnect_bar(&l);
    CHECK(l.state == LAUNCH_APPS && l.ncmd == 0);

    // Another app running on the host is ended before a different one starts
    l.pcs[0].currentGame = 101;
    launcher_view(&l, &v);
    CHECK(v.apps[1].running && !v.apps[2].running);
    launcher_press(&l, UI_HIT_LIST_ITEM, 2, 0);
    CHECK(take(&l, LCMD_LAUNCH, &c) && c.appId == 102 && c.quitFirst == 1);
    // A launch that fails goes back to the apps, with a word why
    launcher_stream_failed(&l, NULL);
    CHECK(l.state == LAUNCH_APPS && l.notice[0] != '\0');
    l.notice[0] = '\0';
    apps(&l, 0);
    // Resuming the running one does not end it first
    launcher_press(&l, UI_HIT_LIST_ITEM, 1, 0);
    CHECK(take(&l, LCMD_LAUNCH, &c) && c.appId == 101 && c.quitFirst == 0);
    launcher_stream_started(&l);
    // The host ends the stream: back to the apps, said on the toast
    launcher_stream_ended(&l, 1);
    CHECK(l.state == LAUNCH_APPS && l.notice[0] != '\0');
    apps(&l, 0);

    // Back to the PCs, and exit from the lists quits the app
    launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_BACK, 0);
    CHECK(l.state == LAUNCH_PCS && l.pc == -1 && take(&l, LCMD_REFRESH_PCS, NULL));
    launcher_exit(&l);
    CHECK(l.state == LAUNCH_QUIT && take(&l, LCMD_EXIT, NULL));

    // --host/--app: straight to the app, and back to its list after
    launcher_init(&l);
    hosts(&l);
    launcher_autostart(&l, 0, 105);
    CHECK(l.state == LAUNCH_STARTING && take(&l, LCMD_LAUNCH, &c) && c.appId == 105);
    launcher_stream_started(&l);
    launcher_exit(&l);
    CHECK(take(&l, LCMD_STOP, &c) && c.quitHost);
    launcher_stream_ended(&l, 0);
    CHECK(l.state == LAUNCH_APPS && l.pc == 0);

    // Offline with a MAC: Wake-on-LAN
    launcher_init(&l);
    hosts(&l);
    launcher_pc_status(&l, 0, UI_PC_OFFLINE, 1, 0);
    launcher_press(&l, UI_HIT_LIST_ITEM, 0, 0);
    CHECK(take(&l, LCMD_WAKE, &c) && c.pc == 0 && l.state == LAUNCH_PCS);

    // --file: exit quits, and so does the clip ending
    launcher_init_file(&l);
    CHECK(l.state == LAUNCH_STREAMING);
    launcher_view(&l, &v);
    CHECK(v.mode == 0);
    launcher_exit(&l);
    CHECK(l.state == LAUNCH_QUIT && take(&l, LCMD_EXIT, NULL));
    // and Disconnect in file mode has nothing to keep running: it quits too
    launcher_init_file(&l);
    launcher_disconnect_bar(&l);
    CHECK(l.state == LAUNCH_QUIT && take(&l, LCMD_EXIT, NULL));

    // A full queue keeps the newest
    launcher_init(&l);
    for (int i = 0; i < 20; i++) launcher_press(&l, UI_HIT_BUTTON, UI_BTN_LIST_ADD, 0);
    CHECK(l.ncmd <= 8);
    return checksDone("m4 launcher");
}
