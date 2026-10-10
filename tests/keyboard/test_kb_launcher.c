// Moonlight XR Native, tests/keyboard: launcher_disconnect leaves the stream
// without ending the host's session (app/launcher.c). GPLv3.
#include <string.h>

#include "check.h"
#include "launcher.h"

static void startStream(Launcher* l) {
    LaunchPc pcs[1];
    memset(pcs, 0, sizeof(pcs));
    strcpy(pcs[0].name, "DESKTOP-EXAMPLE");
    pcs[0].paired = 1;
    launcher_hosts(l, pcs, 1);
    launcher_pc_status(l, 0, UI_PC_ONLINE, 1, 0);
    LaunchApp a[2] = { { "Desktop", 1 }, { "Game", 2 } };
    launcher_apps(l, 0, a, 2, 1);
    launcher_press(l, UI_HIT_LIST_ITEM, 0, 0);   // the PC
    launcher_apps(l, 0, a, 2, 1);
    launcher_press(l, UI_HIT_LIST_ITEM, 1, 0);   // an app
    launcher_stream_started(l);
}

static int drain(Launcher* l, LaunchCmd* last) {
    LaunchCmd c;
    int n = 0;
    while (launcher_take_cmd(l, &c)) {
        *last = c;
        n++;
    }
    return n;
}

int main(void) {
    LaunchCmd c = { 0 };

    // From a stream: LCMD_STOP, quitHost 0, the app list afterwards
    Launcher l;
    launcher_init(&l);
    startStream(&l);
    CHECK(l.state == LAUNCH_STREAMING);
    drain(&l, &c);
    launcher_disconnect(&l);
    CHECK(l.state == LAUNCH_STOPPING);
    CHECK(drain(&l, &c) == 1 && c.kind == LCMD_STOP && c.quitHost == 0 && c.pc == 0);
    CHECK(strstr(l.notice, "Start+Select+L1+R1") != NULL);
    launcher_stream_ended(&l, 0);
    CHECK(l.state == LAUNCH_APPS);
    CHECK(drain(&l, &c) == 1 && c.kind == LCMD_FETCH_APPS);

    // A physical keyboard's Ctrl+Alt+Shift+Q: the same, with its own notice
    launcher_init(&l);
    startStream(&l);
    drain(&l, &c);
    launcher_disconnect_keys(&l);
    CHECK(l.state == LAUNCH_STOPPING);
    CHECK(drain(&l, &c) == 1 && c.kind == LCMD_STOP && c.quitHost == 0);
    CHECK(strcmp(l.notice, "Disconnected: Ctrl+Alt+Shift+Q") == 0);

    // The bar's Exit still ends the host's session
    launcher_init(&l);
    startStream(&l);
    drain(&l, &c);
    launcher_exit(&l);
    CHECK(drain(&l, &c) == 1 && c.kind == LCMD_STOP && c.quitHost == 1);

    // Twice: the second finds it already stopping and does nothing
    launcher_init(&l);
    startStream(&l);
    drain(&l, &c);
    launcher_disconnect(&l);
    launcher_disconnect(&l);
    CHECK(drain(&l, &c) == 1);

    // Outside a stream it does nothing at all
    launcher_init(&l);
    drain(&l, &c);
    launcher_disconnect(&l);
    CHECK(l.state == LAUNCH_PCS && l.ncmd == 0);

    // File mode behaves as exit
    launcher_init_file(&l);
    drain(&l, &c);
    launcher_disconnect(&l);
    CHECK(l.state == LAUNCH_QUIT && drain(&l, &c) == 1 && c.kind == LCMD_EXIT);
    return checksDone("kb launcher");
}
