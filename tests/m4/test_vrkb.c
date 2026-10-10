// Moonlight XR Native, tests/m4: the on-screen keyboard's keys as host events
// (stream/vr_keyboard.c, VrKeyboard.java in C). GPLv3.
#include <Limelight.h>
#include <string.h>

#include "check.h"
#include "../../stream/vr_keyboard.h"
#include "xr_shared.h"

static char g_log[2048];

static void key(void* user, short code, char action, char mods) {
    (void)user;
    char b[64];
    snprintf(b, sizeof(b), "%s%x/%d ", action == KEY_ACTION_DOWN ? "+" : "-", (unsigned short)code, mods);
    strncat(g_log, b, sizeof(g_log) - strlen(g_log) - 1);
}

static void text(void* user, const char* s) {
    (void)user;
    strncat(g_log, "t:", sizeof(g_log) - strlen(g_log) - 1);
    strncat(g_log, s, sizeof(g_log) - strlen(g_log) - 1);
    strncat(g_log, " ", sizeof(g_log) - strlen(g_log) - 1);
}

static const char* run(VrKeyboard* k, int code, int mods) {
    g_log[0] = '\0';
    vrkb_type(k, code, mods);
    return g_log;
}

int main(void) {
    VrKeyboard k;
    VrKeyboardSink sink = { NULL, key, text };
    vrkb_init(&k, sink);
    CHECK(strcmp(run(&k, 'a', 0), "+41/0 -41/0 ") == 0);
    CHECK(strcmp(run(&k, '7', 0), "+37/0 -37/0 ") == 0);
    CHECK(strcmp(run(&k, 'A', 0), "+10/0 +41/1 -41/1 -10/0 ") == 0);
    CHECK(strcmp(run(&k, 8, 0), "+8/0 -8/0 ") == 0);
    CHECK(strcmp(run(&k, KB_CODE_VK + 0x70, 0), "+70/0 -70/0 ") == 0); // F1
    CHECK(strcmp(run(&k, '!', 0), "t:! ") == 0);
    CHECK(strcmp(run(&k, 0xE9, 0), "t:\xC3\xA9 ") == 0);              // é as UTF-8
    // Ctrl lit: put down once, the key carries it, and it stays down
    CHECK(strcmp(run(&k, 'c', KB_MOD_CTRL), "+a2/2 +43/2 -43/2 ") == 0);
    CHECK(k.held == KB_MOD_CTRL);
    CHECK(strcmp(run(&k, '-', KB_MOD_CTRL), "+bd/2 -bd/2 ") == 0);     // punctuation as its key with a modifier
    g_log[0] = '\0';
    vrkb_hold(&k, KB_MOD_CTRL | KB_MOD_ALT);
    CHECK(strcmp(g_log, "+a4/6 ") == 0);
    g_log[0] = '\0';
    vrkb_release_all(&k);
    CHECK(strcmp(g_log, "-a4/2 -a2/0 ") == 0 && k.held == 0);
    return checksDone("m4 vrkb");
}
