// The Host sheet's keys (xr_hostkeys.c): the quick actions, the macros.ini
// parser, the sequence runner, the chord capture and the sheet's zones.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "xr_hostkeys.h"
#include "xr_kbfull.h"
#include "xr_shared.h"

static void testQuick(void) {
    CHECK(XR_HOST_QUICK_COUNT == 11);
    static const struct { const char* label; int mods, vk, confirm; } WANT[XR_HOST_QUICK_COUNT] = {
        { "Esc", 0, 0x1B, 0 },
        { "Win", 0, 0x5B, 0 },
        { "Win+D", KBF_LWIN, 0x44, 0 },
        { "Alt+Enter", KBF_LALT, 0x0D, 0 },
        { "Ctrl+Alt+Tab", KBF_LCTRL | KBF_LALT, 0x09, 0 },
        { "Shift+Tab", KBF_LSHIFT, 0x09, 0 },
        { "Win+G", KBF_LWIN, 0x47, 0 },
        { "Win+Shift+Left", KBF_LWIN | KBF_LSHIFT, 0x25, 0 },
        { "Ctrl+V", KBF_LCTRL, 0x56, 0 },
        { "Ctrl+Shift+Esc", KBF_LCTRL | KBF_LSHIFT, 0x1B, 0 },
        { "Alt+F4", KBF_LALT, 0x73, 1 },
    };
    for (int i = 0; i < XR_HOST_QUICK_COUNT; i++) {
        const XrHostQuick* q = xrHostQuick(i);
        CHECK(q != NULL);
        if (q == NULL) continue;
        CHECK(strcmp(q->label, WANT[i].label) == 0);
        CHECK(q->mods == WANT[i].mods && q->vk == WANT[i].vk && q->confirm == WANT[i].confirm);
        // only the one that closes the window asks first
        CHECK((q->confirm != 0) == (i == XR_HOST_QUICK_COUNT - 1));
        // a cell plays that one tap
        XrHostSeq seq;
        int confirm = -1;
        char label[40];
        CHECK(xrHostCellSeq(i, NULL, &seq, &confirm, label, sizeof(label)) == XR_HOST_CELL_QUICK);
        CHECK(seq.count == 1 && seq.step[0].kind == XR_HOST_STEP_TAP && seq.step[0].vk == q->vk
              && seq.step[0].mods == q->mods && confirm == q->confirm && strcmp(label, q->label) == 0);
    }
    CHECK(xrHostQuick(-1) == NULL && xrHostQuick(XR_HOST_QUICK_COUNT) == NULL);
    // the names the table uses parse to the same chords as xr_bind's key specs
    CHECK(xrHostModBit(0xA0) == KBF_LSHIFT && xrHostModBit(0x11) == KBF_LCTRL && xrHostModBit(0xA5) == KBF_RALT
          && xrHostModBit(0x5B) == KBF_LWIN && xrHostModBit(0x5C) == KBF_RWIN && xrHostModBit(0x41) == 0);
}

static void testZones(void) {
    // The cells are in a grid inside the sheet and none overlap
    for (int a = 0; a < XR_HOST_CELLS; a++) {
        float ra[4];
        xrHostCellRect(a, ra);
        CHECK(ra[0] >= 0 && ra[1] >= 0 && ra[2] <= 1 && ra[3] <= 1 && ra[2] > ra[0] && ra[3] > ra[1]);
        for (int b = a + 1; b < XR_HOST_CELLS; b++) {
            float rb[4];
            xrHostCellRect(b, rb);
            CHECK(ra[2] <= rb[0] || rb[2] <= ra[0] || ra[3] <= rb[1] || rb[3] <= ra[1]);
        }
        float c[4];
        xrHostCloseRect(c);
        CHECK(ra[2] <= c[0] || c[2] <= ra[0] || ra[3] <= c[1] || c[3] <= ra[1]);
    }
    CHECK(XR_HOST_CELLS == XR_HOST_COLS * XR_HOST_ROWS);
    // kinds
    CHECK(xrHostCellKind(0, 0) == XR_HOST_CELL_QUICK && xrHostCellKind(10, 0) == XR_HOST_CELL_QUICK);
    CHECK(xrHostCellKind(XR_HOST_CELL_SEND_KEYS, 0) == XR_HOST_CELL_KEYS);
    CHECK(xrHostCellKind(XR_HOST_MACRO_FIRST, 0) == XR_HOST_CELL_NONE);
    CHECK(xrHostCellKind(XR_HOST_MACRO_FIRST, 1) == XR_HOST_CELL_MACRO);
    CHECK(xrHostCellKind(XR_HOST_MACRO_FIRST + 1, 1) == XR_HOST_CELL_NONE);
    CHECK(xrHostCellKind(XR_HOST_CELLS, 99) == XR_HOST_CELL_NONE && xrHostCellKind(-1, 99) == XR_HOST_CELL_NONE);
    // the middle of a used cell is that cell; of an empty one, nothing
    for (int i = 0; i < XR_HOST_CELLS; i++) {
        float r[4];
        xrHostCellRect(i, r);
        float u = (r[0] + r[2]) * 0.5f, v = (r[1] + r[3]) * 0.5f;
        CHECK(xrHostZoneAt(u, v, 3, 0) == (xrHostCellKind(i, 3) == XR_HOST_CELL_NONE ? XR_HOST_ZONE_NONE : i));
        // asking first: the cells are out of reach, only the two buttons are zones
        int asking = xrHostZoneAt(u, v, 3, 1);
        CHECK(asking == XR_HOST_ZONE_NONE || asking == XR_HOST_ZONE_CONFIRM_SEND || asking == XR_HOST_ZONE_CONFIRM_CANCEL);
    }
    float r[4];
    xrHostCloseRect(r);
    CHECK(xrHostZoneAt((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, 0, 0) == XR_HOST_ZONE_CLOSE);
    CHECK(xrHostZoneAt((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, 0, 1) == XR_HOST_ZONE_NONE);
    // Native (PLAN 4.1): PC VR is a zone left of Close, over no cell, and out of reach while asking
    float pc[4], cl[4];
    xrHostPcvrRect(pc);
    xrHostCloseRect(cl);
    CHECK(pc[2] <= cl[0] && pc[0] >= 0.0f && pc[1] >= 0.0f && pc[3] <= 1.0f);
    for (int i = 0; i < XR_HOST_CELLS; i++) {
        float cr[4];
        xrHostCellRect(i, cr);
        CHECK(pc[2] <= cr[0] || cr[2] <= pc[0] || pc[3] <= cr[1] || cr[3] <= pc[1]);
    }
    CHECK(xrHostZoneAt((pc[0] + pc[2]) * 0.5f, (pc[1] + pc[3]) * 0.5f, 0, 0) == XR_HOST_ZONE_PCVR);
    CHECK(xrHostZoneAt((pc[0] + pc[2]) * 0.5f, (pc[1] + pc[3]) * 0.5f, 8, 0) == XR_HOST_ZONE_PCVR);
    CHECK(xrHostZoneAt((pc[0] + pc[2]) * 0.5f, (pc[1] + pc[3]) * 0.5f, 0, 1) == XR_HOST_ZONE_NONE);
    CHECK(XR_HOST_CONFIRM_PCVR >= XR_HOST_CELLS && XR_HOST_ZONE_PCVR != XR_HOST_ZONE_CLOSE);
    xrHostConfirmSendRect(r);
    CHECK(xrHostZoneAt((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, 0, 1) == XR_HOST_ZONE_CONFIRM_SEND);
    CHECK(xrHostZoneAt((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, 0, 0) != XR_HOST_ZONE_CONFIRM_SEND);
    xrHostConfirmCancelRect(r);
    CHECK(xrHostZoneAt((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, 0, 1) == XR_HOST_ZONE_CONFIRM_CANCEL);
    float box[4], s[4], c[4];
    xrHostConfirmBox(box);
    xrHostConfirmSendRect(s);
    xrHostConfirmCancelRect(c);
    CHECK(s[0] >= box[0] && s[2] <= box[2] && s[1] >= box[1] && s[3] <= box[3]);
    CHECK(c[0] >= box[0] && c[2] <= box[2] && c[1] >= box[1] && c[3] <= box[3] && s[2] <= c[0]);
    CHECK(xrHostZoneAt(0.001f, 0.001f, 3, 0) == XR_HOST_ZONE_NONE);
}

// ---- macros.ini ----------------------------------------------------------------------------------

static XrMacros g_m;

static int stepIs(const XrHostSeq* s, int i, int kind, int vk, int mods, int ms) {
    return i < s->count && s->step[i].kind == kind && s->step[i].vk == vk && s->step[i].mods == mods
           && s->step[i].ms == ms;
}

static void testParse(void) {
    const char* text =
        "\xEF\xBB\xBF# my macros\r\n"
        "[macros]\r\n"
        "Task manager = ctrl+shift+esc\r\n"
        "Alt-Tab twice = down:alt, tab, tab, up:alt   ; hold Alt for both\r\n"
        "  Mute   =  key:mute\n"
        "Paste in chat = enter, wait:200, ctrl+v, wait:50 enter\n"
        "Close it = !alt+f4\n"
        "Hex = 0x41\n";
    CHECK(xrMacrosParse(&g_m, text) == 6);
    CHECK(g_m.count == 6 && g_m.errors == 0);
    CHECK(strcmp(g_m.macro[0].name, "Task manager") == 0 && g_m.macro[0].seq.count == 1 && !g_m.macro[0].confirm);
    CHECK(stepIs(&g_m.macro[0].seq, 0, XR_HOST_STEP_TAP, 0x1B, KBF_LCTRL | KBF_LSHIFT, 0));
    // press and release in the order written
    const XrHostSeq* at = &g_m.macro[1].seq;
    CHECK(strcmp(g_m.macro[1].name, "Alt-Tab twice") == 0 && at->count == 4);
    CHECK(stepIs(at, 0, XR_HOST_STEP_DOWN, 0xA4, 0, 0) && stepIs(at, 1, XR_HOST_STEP_TAP, 0x09, 0, 0)
          && stepIs(at, 2, XR_HOST_STEP_TAP, 0x09, 0, 0) && stepIs(at, 3, XR_HOST_STEP_UP, 0xA4, 0, 0));
    CHECK(strcmp(g_m.macro[2].name, "Mute") == 0 && stepIs(&g_m.macro[2].seq, 0, XR_HOST_STEP_TAP, 0xAD, 0, 0));
    const XrHostSeq* pc = &g_m.macro[3].seq;
    CHECK(pc->count == 5 && stepIs(pc, 0, XR_HOST_STEP_TAP, 0x0D, 0, 0) && stepIs(pc, 1, XR_HOST_STEP_WAIT, 0, 0, 200)
          && stepIs(pc, 2, XR_HOST_STEP_TAP, 0x56, KBF_LCTRL, 0) && stepIs(pc, 3, XR_HOST_STEP_WAIT, 0, 0, 50)
          && stepIs(pc, 4, XR_HOST_STEP_TAP, 0x0D, 0, 0));
    CHECK(strcmp(g_m.macro[4].name, "Close it") == 0 && g_m.macro[4].confirm == 1
          && stepIs(&g_m.macro[4].seq, 0, XR_HOST_STEP_TAP, 0x73, KBF_LALT, 0));
    CHECK(stepIs(&g_m.macro[5].seq, 0, XR_HOST_STEP_TAP, 0x41, 0, 0));

    // a cell plays a macro's steps and carries its confirm
    XrHostSeq seq;
    int confirm;
    char label[40];
    CHECK(xrHostCellSeq(XR_HOST_MACRO_FIRST + 4, &g_m, &seq, &confirm, label, sizeof(label)) == XR_HOST_CELL_MACRO);
    CHECK(confirm == 1 && strcmp(label, "Close it") == 0 && seq.count == 1);
    CHECK(xrHostCellSeq(XR_HOST_MACRO_FIRST + 6, &g_m, &seq, &confirm, label, sizeof(label)) == XR_HOST_CELL_NONE
          && seq.count == 0 && label[0] == '\0');
    CHECK(xrHostCellSeq(XR_HOST_CELL_SEND_KEYS, &g_m, &seq, &confirm, label, sizeof(label)) == XR_HOST_CELL_KEYS
          && seq.count == 0);

    // no section needed; names and key names are case blind; a name twice keeps the first
    CHECK(xrMacrosParse(&g_m, "A = F5\na = F6\nB = SHIFT+tab\n") == 2);
    CHECK(g_m.errors == 1 && strstr(g_m.error, "line 2") != NULL && strstr(g_m.error, "already") != NULL);
    CHECK(stepIs(&g_m.macro[0].seq, 0, XR_HOST_STEP_TAP, 0x74, 0, 0));
    CHECK(stepIs(&g_m.macro[1].seq, 0, XR_HOST_STEP_TAP, 0x09, KBF_LSHIFT, 0));

    // bad lines are left out with where and why, the good ones stay
    const char* bad =
        "Good = esc\n"
        "no equals sign\n"
        "= esc\n"
        "Empty =\n"
        "Unknown key = frobnicate\n"
        "Chord down = down:ctrl+c\n"
        "Wait = wait:0\n"
        "Wait big = wait:99999\n"
        "Wait word = wait:soon\n"
        "[other]\n"
        "x = y\n"
        "[macros]\n"
        "Also good = enter\n";
    CHECK(xrMacrosParse(&g_m, bad) == 2);
    CHECK(strcmp(g_m.macro[0].name, "Good") == 0 && strcmp(g_m.macro[1].name, "Also good") == 0);
    CHECK(g_m.errors == 9);
    CHECK(strstr(g_m.error, "line 2") != NULL);

    // limits: a long name, too many steps, too many macros
    char big[8192];
    int n = 0;
    n += snprintf(big + n, sizeof(big) - (size_t)n, "%s = esc\n", "a very long name indeed, over the limit");
    n += snprintf(big + n, sizeof(big) - (size_t)n, "Steps =");
    for (int i = 0; i < XR_HOST_STEPS + 1; i++) n += snprintf(big + n, sizeof(big) - (size_t)n, " a");
    n += snprintf(big + n, sizeof(big) - (size_t)n, "\n");
    for (int i = 0; i < XR_MACRO_MAX + 2; i++) n += snprintf(big + n, sizeof(big) - (size_t)n, "M%d = esc\n", i);
    CHECK(xrMacrosParse(&g_m, big) == XR_MACRO_MAX);
    CHECK(g_m.errors == 4);
    // exactly the limit of steps is fine
    n = snprintf(big, sizeof(big), "Full =");
    for (int i = 0; i < XR_HOST_STEPS; i++) n += snprintf(big + n, sizeof(big) - (size_t)n, " a");
    CHECK(xrMacrosParse(&g_m, big) == 1 && g_m.macro[0].seq.count == XR_HOST_STEPS && g_m.errors == 0);

    CHECK(xrMacrosParse(&g_m, "") == 0 && g_m.errors == 0);
    CHECK(xrMacrosParse(&g_m, NULL) == 0);
    CHECK(xrMacrosParse(&g_m, "# only a comment\n; and another\n\n") == 0 && g_m.errors == 0);
}

static void testFile(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/mlxr-macros-test-%d.ini", (int)getpid());
    CHECK(xrMacrosLoadFile(&g_m, "/nonexistent-dir/macros.ini") == 0 && g_m.count == 0);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs("[macros]\nSnip = win+shift+s\n", f);
    fclose(f);
    CHECK(xrMacrosLoadFile(&g_m, path) == 1 && g_m.count == 1 && strcmp(g_m.macro[0].name, "Snip") == 0);
    remove(path);
    // the default path
    char buf[300];
    setenv("MLXR_CONFIG_DIR", "/cfg", 1);
    CHECK(strcmp(xrMacrosDefaultPath(buf, sizeof(buf)), "/cfg/macros.ini") == 0);
    unsetenv("MLXR_CONFIG_DIR");
    setenv("HOME", "/tmp/hh", 1);
    CHECK(strcmp(xrMacrosDefaultPath(buf, sizeof(buf)), "/tmp/hh/.config/moonlight-xr/macros.ini") == 0);
}

// ---- the runner ---------------------------------------------------------------------------------------

#define MS 1000000LL

static void play(const char* text, XrHostEmit* out, long long* times, int* n, long long stepNs) {
    XrMacros m;
    xrMacrosParse(&m, text);
    XrHostRun r;
    xrHostRunInit(&r);
    CHECK(m.count == 1 && xrHostRunStart(&r, &m.macro[0].seq) == 1);
    *n = 0;
    for (long long now = 0; now < 10000 * MS && xrHostRunBusy(&r); now += stepNs) {
        XrHostEmit e;
        if (xrHostRunStep(&r, now, &e)) {
            out[*n] = e;
            times[*n] = now;
            (*n)++;
        }
    }
    CHECK(!xrHostRunBusy(&r));
}

static void testRun(void) {
    XrHostEmit e[16];
    long long t[16];
    int n;
    // a chord is one event
    play("X = ctrl+shift+esc", e, t, &n, 11 * MS);
    CHECK(n == 1 && e[0].kind == XR_HOST_EMIT_CHORD && e[0].vk == 0x1B && e[0].mods == (KBF_LCTRL | KBF_LSHIFT));

    // down and up keep their order, the modifier is held round the keys between, and a tap inside it carries it
    play("X = down:alt, tab, tab, up:alt", e, t, &n, 11 * MS);
    CHECK(n == 4);
    CHECK(e[0].kind == XR_HOST_EMIT_HOLD && e[0].mods == KBF_LALT);
    CHECK(e[1].kind == XR_HOST_EMIT_CHORD && e[1].vk == 0x09 && e[1].mods == KBF_LALT);
    CHECK(e[2].kind == XR_HOST_EMIT_CHORD && e[2].vk == 0x09 && e[2].mods == KBF_LALT);
    CHECK(e[3].kind == XR_HOST_EMIT_LET && e[3].mods == KBF_LALT);

    // a key held: down with the modifiers held, up, the same order
    play("X = down:ctrl down:a up:a up:ctrl", e, t, &n, 11 * MS);
    CHECK(n == 4 && e[0].kind == XR_HOST_EMIT_HOLD && e[1].kind == XR_HOST_EMIT_DOWN && e[1].vk == 0x41
          && e[1].mods == KBF_LCTRL && e[2].kind == XR_HOST_EMIT_UP && e[2].vk == 0x41 && e[3].kind == XR_HOST_EMIT_LET);

    // one event a frame, and a wait holds the next back by its time
    play("X = a, wait:100, b", e, t, &n, 11 * MS);
    CHECK(n == 2 && e[0].vk == 0x41 && e[1].vk == 0x42);
    CHECK(t[0] == 0 && t[1] >= 100 * MS && t[1] < 100 * MS + 22 * MS);
    play("X = a b c", e, t, &n, 11 * MS);
    CHECK(n == 3 && t[1] - t[0] == 11 * MS && t[2] - t[1] == 11 * MS);

    // left unfinished (keys down with no up), the end lets go: key first, then the modifiers
    play("X = down:win down:shift down:d", e, t, &n, 11 * MS);
    CHECK(n == 6);
    CHECK(e[2].kind == XR_HOST_EMIT_DOWN && e[2].vk == 0x44 && e[2].mods == (KBF_LWIN | KBF_LSHIFT));
    CHECK(e[3].kind == XR_HOST_EMIT_UP && e[3].vk == 0x44);
    CHECK(e[4].kind == XR_HOST_EMIT_LET && e[5].kind == XR_HOST_EMIT_LET && (e[4].mods | e[5].mods) == (KBF_LWIN | KBF_LSHIFT));

    // aborted half way: everything held comes up and the rest is skipped
    XrMacros m;
    xrMacrosParse(&m, "X = down:ctrl, down:c, wait:5000, v");
    XrHostRun r;
    xrHostRunInit(&r);
    xrHostRunStart(&r, &m.macro[0].seq);
    XrHostEmit ev;
    CHECK(xrHostRunStep(&r, 0, &ev) == 1 && xrHostRunStep(&r, 11 * MS, &ev) == 1);
    CHECK(xrHostRunStep(&r, 22 * MS, &ev) == 0 && xrHostRunBusy(&r));       // waiting
    xrHostRunAbort(&r);
    CHECK(xrHostRunStep(&r, 33 * MS, &ev) == 1 && ev.kind == XR_HOST_EMIT_UP && ev.vk == 0x43);
    CHECK(xrHostRunStep(&r, 44 * MS, &ev) == 1 && ev.kind == XR_HOST_EMIT_LET && ev.mods == KBF_LCTRL);
    CHECK(xrHostRunStep(&r, 55 * MS, &ev) == 0 && !xrHostRunBusy(&r));

    // a new run while one holds something lets it go first
    xrMacrosParse(&m, "X = down:alt\nY = esc");
    xrHostRunInit(&r);
    xrHostRunStart(&r, &m.macro[0].seq);
    CHECK(xrHostRunStep(&r, 0, &ev) == 1 && ev.kind == XR_HOST_EMIT_HOLD);
    xrHostRunStart(&r, &m.macro[1].seq);
    CHECK(xrHostRunStep(&r, 11 * MS, &ev) == 1 && ev.kind == XR_HOST_EMIT_LET && ev.mods == KBF_LALT);   // not the old run's: it goes first
    CHECK(r.heldMods == 0);
    CHECK(xrHostRunStep(&r, 22 * MS, &ev) == 1 && ev.kind == XR_HOST_EMIT_CHORD && ev.vk == 0x1B);

    // nothing to play, nothing started; nothing running, nothing emitted
    XrHostSeq none;
    memset(&none, 0, sizeof(none));
    xrHostRunInit(&r);
    CHECK(xrHostRunStart(&r, &none) == 0 && !xrHostRunBusy(&r) && xrHostRunStep(&r, 0, &ev) == 0);
}

// ---- Send keys ----------------------------------------------------------------------------------------------

static void testChord(void) {
    XrHostChord c;
    xrHostChordStop(&c);
    int vk = 0, mods = 0;
    CHECK(xrHostChordKey(&c, 'a', &vk, &mods) == XR_CHORD_IGNORE);     // not in chord mode
    xrHostChordStart(&c);
    CHECK(c.active && c.mods == 0);
    // Ctrl, Alt, then F4: Ctrl+Alt+F4, once
    CHECK(xrHostChordKey(&c, KB_CODE_CTRL, &vk, &mods) == XR_CHORD_MOD && c.mods == KBF_LCTRL);
    CHECK(xrHostChordKey(&c, KB_CODE_ALT, &vk, &mods) == XR_CHORD_MOD && c.mods == (KBF_LCTRL | KBF_LALT));
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0x73, &vk, &mods) == XR_CHORD_SEND);
    CHECK(vk == 0x73 && mods == (KBF_LCTRL | KBF_LALT));
    CHECK(!c.active && c.mods == 0);
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0x73, &vk, &mods) == XR_CHORD_IGNORE);   // once
    // a second tap on a lit modifier puts it out
    xrHostChordStart(&c);
    xrHostChordKey(&c, KB_CODE_WIN, &vk, &mods);
    xrHostChordKey(&c, KB_CODE_WIN, &vk, &mods);
    CHECK(c.mods == 0);
    // letters: lower case is the key, a capital the key with Shift (the Compact keyboard's Shift sheet)
    CHECK(xrHostChordKey(&c, 'd', &vk, &mods) == XR_CHORD_SEND && vk == 0x44 && mods == 0);
    xrHostChordStart(&c);
    xrHostChordKey(&c, KB_CODE_CTRL, &vk, &mods);
    CHECK(xrHostChordKey(&c, 'T', &vk, &mods) == XR_CHORD_SEND && vk == 0x54 && mods == (KBF_LCTRL | KBF_LSHIFT));
    // digits, the named keys, punctuation, a shifted key of another layout
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, '5', &vk, &mods) == XR_CHORD_SEND && vk == '5');
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, 9, &vk, &mods) == XR_CHORD_SEND && vk == 0x09);
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, ';', &vk, &mods) == XR_CHORD_SEND && vk == 0xBA);
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, KB_CODE_SVK + '1', &vk, &mods) == XR_CHORD_SEND && vk == '1' && mods == KBF_LSHIFT);
    // the Full keyboard's modifier keys by their VK, either side
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0xA3, &vk, &mods) == XR_CHORD_MOD && c.mods == KBF_RCTRL);
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0xA0, &vk, &mods) == XR_CHORD_MOD && c.mods == (KBF_RCTRL | KBF_LSHIFT));
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0x1B, &vk, &mods) == XR_CHORD_SEND && vk == 0x1B
          && mods == (KBF_RCTRL | KBF_LSHIFT));
    // a media key is a chord of one
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, KB_CODE_VK + 0xAF, &vk, &mods) == XR_CHORD_SEND && vk == 0xAF && mods == 0);
    // the sheet keys are the keyboard's, the chord waits; characters with no key of their own are ignored
    xrHostChordStart(&c);
    CHECK(xrHostChordKey(&c, KB_CODE_SHIFT, &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(xrHostChordKey(&c, KB_CODE_SYMBOLS, &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(xrHostChordKey(&c, KB_CODE_FN, &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(xrHostChordKey(&c, -9, &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(xrHostChordKey(&c, '!', &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(xrHostChordKey(&c, 0, &vk, &mods) == XR_CHORD_IGNORE);
    CHECK(c.active);
    // Hide ends it without sending
    xrHostChordKey(&c, KB_CODE_CTRL, &vk, &mods);
    CHECK(xrHostChordKey(&c, KB_CODE_HIDE, &vk, &mods) == XR_CHORD_CANCEL && !c.active && c.mods == 0);
}

// The PC page (lane wave4-apollo): the header button, the clipboard cells and the host's commands
static void testPcPage(void) {
    float page[4], close[4], cell[4];
    xrHostPageRect(page);
    xrHostCloseRect(close);
    CHECK(page[0] >= 0 && page[1] >= 0 && page[2] <= 1 && page[3] <= 1 && page[2] > page[0] && page[3] > page[1]);
    // beside Close, clear of it, of PC VR (main's) and of every cell
    CHECK(page[2] <= close[0] || close[2] <= page[0] || page[3] <= close[1] || close[3] <= page[1]);
    float pcvr[4];
    xrHostPcvrRect(pcvr);
    CHECK(page[2] <= pcvr[0] || pcvr[2] <= page[0] || page[3] <= pcvr[1] || pcvr[3] <= page[1]);
    CHECK(XR_HOST_ZONE_PAGE != XR_HOST_ZONE_PCVR && XR_HOST_ZONE_PAGE != XR_HOST_ZONE_CLOSE
          && XR_HOST_ZONE_PAGE != XR_HOST_ZONE_CONFIRM_SEND && XR_HOST_ZONE_PAGE != XR_HOST_ZONE_CONFIRM_CANCEL);
    // the header is the same on both pages: PC VR is a zone on each, and hidden by a question
    float qu = (pcvr[0] + pcvr[2]) * 0.5f, qv = (pcvr[1] + pcvr[3]) * 0.5f;
    CHECK(xrHostZoneAtPage(qu, qv, XR_HOST_PAGE_MAIN, 0, 3, 0) == XR_HOST_ZONE_PCVR);
    CHECK(xrHostZoneAtPage(qu, qv, XR_HOST_PAGE_PC, 0, 3, 0) == XR_HOST_ZONE_PCVR);
    CHECK(xrHostZoneAtPage(qu, qv, XR_HOST_PAGE_PC, 0, 3, 1) == XR_HOST_ZONE_NONE);
    for (int i = 0; i < XR_HOST_CELLS; i++) {
        xrHostCellRect(i, cell);
        CHECK(page[2] <= cell[0] || cell[2] <= page[0] || page[3] <= cell[1] || cell[3] <= page[1]);
    }
    float pu = (page[0] + page[2]) * 0.5f, pv = (page[1] + page[3]) * 0.5f;

    // cell kinds: two clipboard entries, then one cell per command, at most 8
    CHECK(xrHostPcCellKind(XR_HOST_PC_PASTE, 0) == XR_HOST_CELL_PASTE);
    CHECK(xrHostPcCellKind(XR_HOST_PC_COPY, 0) == XR_HOST_CELL_COPY);
    CHECK(xrHostPcCellKind(2, 3) == XR_HOST_CELL_NONE && xrHostPcCellKind(4, 3) == XR_HOST_CELL_NONE);
    CHECK(xrHostPcCellKind(XR_HOST_PC_CMD_FIRST, 0) == XR_HOST_CELL_NONE);
    CHECK(xrHostPcCellKind(XR_HOST_PC_CMD_FIRST, 1) == XR_HOST_CELL_CMD);
    CHECK(xrHostPcCellKind(XR_HOST_PC_CMD_FIRST + 1, 1) == XR_HOST_CELL_NONE);
    CHECK(xrHostPcCellKind(XR_HOST_PC_CMD_FIRST + 7, 8) == XR_HOST_CELL_CMD);
    CHECK(xrHostPcCellKind(XR_HOST_PC_CMD_FIRST + 8, 12) == XR_HOST_CELL_NONE);   // 8 slots, however many were listed
    CHECK(xrHostPcCellKind(-1, 3) == XR_HOST_CELL_NONE && xrHostPcCellKind(XR_HOST_CELLS, 3) == XR_HOST_CELL_NONE);
    // the clipboard cells and the command cells do not share a cell, and the main page's cells are untouched
    CHECK(XR_HOST_PC_CMD_FIRST > XR_HOST_PC_COPY && XR_HOST_PC_CMD_FIRST + XR_HOST_CMD_SLOTS <= XR_HOST_CELLS);
    CHECK(xrHostCellKind(XR_HOST_CELL_SEND_KEYS, 0) == XR_HOST_CELL_KEYS);

    // zones: the page button on both pages; each page's own cells; Close; a question hides the rest
    CHECK(xrHostZoneAtPage(pu, pv, XR_HOST_PAGE_MAIN, 0, 3, 0) == XR_HOST_ZONE_PAGE);
    CHECK(xrHostZoneAtPage(pu, pv, XR_HOST_PAGE_PC, 0, 3, 0) == XR_HOST_ZONE_PAGE);
    CHECK(xrHostZoneAtPage(pu, pv, XR_HOST_PAGE_PC, 0, 3, 1) == XR_HOST_ZONE_NONE);
    xrHostCellRect(XR_HOST_PC_CMD_FIRST + 2, cell);
    float cu = (cell[0] + cell[2]) * 0.5f, cv = (cell[1] + cell[3]) * 0.5f;
    CHECK(xrHostZoneAtPage(cu, cv, XR_HOST_PAGE_PC, 0, 3, 0) == XR_HOST_PC_CMD_FIRST + 2);
    CHECK(xrHostZoneAtPage(cu, cv, XR_HOST_PAGE_PC, 0, 2, 0) == XR_HOST_ZONE_NONE);       // only 2 were listed
    CHECK(xrHostZoneAtPage(cu, cv, XR_HOST_PAGE_MAIN, 0, 3, 0) == XR_HOST_PC_CMD_FIRST + 2);   // a quick action there
    xrHostCellRect(XR_HOST_CELL_SEND_KEYS, cell);
    CHECK(xrHostZoneAtPage((cell[0] + cell[2]) * 0.5f, (cell[1] + cell[3]) * 0.5f, XR_HOST_PAGE_PC, 0, 3, 0)
          == XR_HOST_ZONE_NONE);
    CHECK(xrHostZoneAtPage((cell[0] + cell[2]) * 0.5f, (cell[1] + cell[3]) * 0.5f, XR_HOST_PAGE_MAIN, 0, 3, 0)
          == XR_HOST_CELL_SEND_KEYS);
    CHECK(xrHostZoneAtPage((close[0] + close[2]) * 0.5f, (close[1] + close[3]) * 0.5f, XR_HOST_PAGE_PC, 0, 3, 0)
          == XR_HOST_ZONE_CLOSE);
    float send[4];
    xrHostConfirmSendRect(send);
    CHECK(xrHostZoneAtPage((send[0] + send[2]) * 0.5f, (send[1] + send[3]) * 0.5f, XR_HOST_PAGE_PC, 0, 3, 1)
          == XR_HOST_ZONE_CONFIRM_SEND);
    // the main page's answer is xrHostZoneAt's
    for (int cellIx = 0; cellIx < XR_HOST_CELLS; cellIx++) {
        xrHostCellRect(cellIx, cell);
        float u = (cell[0] + cell[2]) * 0.5f, v = (cell[1] + cell[3]) * 0.5f;
        CHECK(xrHostZoneAtPage(u, v, XR_HOST_PAGE_MAIN, 3, 0, 0) == xrHostZoneAt(u, v, 3, 0));
    }
}

static void testCompose(void) {
    XrHostCompose c;
    xrHostComposeStart(&c);
    CHECK(c.active && c.len == 0 && c.text[0] == '\0');
    CHECK(xrHostComposeKey(&c, 'h') == XR_COMPOSE_EDITED && xrHostComposeKey(&c, 'i') == XR_COMPOSE_EDITED);
    CHECK(strcmp(c.text, "hi") == 0 && c.len == 2);
    CHECK(xrHostComposeKey(&c, ' ') == XR_COMPOSE_EDITED && xrHostComposeKey(&c, 0xE9) == XR_COMPOSE_EDITED
          && xrHostComposeKey(&c, 0x20AC) == XR_COMPOSE_EDITED);
    CHECK(strcmp(c.text, "hi \xC3\xA9\xE2\x82\xAC") == 0 && c.len == 8);
    // Backspace takes a whole character, whatever its length
    CHECK(xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED && strcmp(c.text, "hi \xC3\xA9") == 0);
    CHECK(xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED && strcmp(c.text, "hi ") == 0);
    CHECK(xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED && xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED
          && xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED && c.len == 0 && c.text[0] == '\0');
    CHECK(xrHostComposeKey(&c, 8) == XR_COMPOSE_EDITED && c.len == 0);   // nothing left to take
    // not text: left to the keyboard
    CHECK(xrHostComposeKey(&c, 9) == XR_COMPOSE_IGNORE && xrHostComposeKey(&c, 27) == XR_COMPOSE_IGNORE
          && xrHostComposeKey(&c, KB_CODE_SHIFT) == XR_COMPOSE_IGNORE && xrHostComposeKey(&c, KB_CODE_HIDE) == XR_COMPOSE_IGNORE
          && xrHostComposeKey(&c, KB_CODE_VK + 0x41) == XR_COMPOSE_IGNORE && xrHostComposeKey(&c, 0xD800) == XR_COMPOSE_IGNORE
          && xrHostComposeKey(&c, 0) == XR_COMPOSE_IGNORE);
    CHECK(c.len == 0);
    // Enter is the end, and leaves the text as it was
    xrHostComposeKey(&c, 'o');
    xrHostComposeKey(&c, 'k');
    CHECK(xrHostComposeKey(&c, 13) == XR_COMPOSE_SEND && strcmp(c.text, "ok") == 0 && c.active);
    // the field has a size: a character that would not fit is dropped, never half written
    for (int i = 0; i < XR_COMPOSE_MAX + 20; i++) xrHostComposeKey(&c, 'a');
    CHECK(c.len == XR_COMPOSE_MAX - 1 && c.text[c.len] == '\0');
    CHECK(xrHostComposeKey(&c, 0x20AC) == XR_COMPOSE_EDITED && c.len == XR_COMPOSE_MAX - 1);
    xrHostComposeStop(&c);
    CHECK(!c.active && c.len == 0);
    // stopped: nothing is the field's
    CHECK(xrHostComposeKey(&c, 'a') == XR_COMPOSE_IGNORE && c.len == 0);

    // the tail the space bar shows
    char out[64];
    xrHostComposeTail("", 5, out, sizeof(out));
    CHECK(strcmp(out, "") == 0);
    xrHostComposeTail("short", 5, out, sizeof(out));
    CHECK(strcmp(out, "short") == 0);
    xrHostComposeTail("a longer line", 6, out, sizeof(out));
    CHECK(strcmp(out, "\xE2\x80\xA6" "r line") == 0);
    xrHostComposeTail("caf\xC3\xA9 \xE2\x82\xAC\xE2\x82\xAC", 4, out, sizeof(out));
    CHECK(strcmp(out, "\xE2\x80\xA6" "\xC3\xA9 \xE2\x82\xAC\xE2\x82\xAC") == 0);
    xrHostComposeTail("exactly", 7, out, sizeof(out));
    CHECK(strcmp(out, "exactly") == 0);
    xrHostComposeTail("a longer line", 6, out, 5);   // a small buffer is cut, and ends
    CHECK(strlen(out) == 4);
}

int main(void) {
    testPcPage();
    testCompose();
    testQuick();
    testZones();
    testParse();
    testFile();
    testRun();
    testChord();
    return checksDone("xr_hostkeys");
}
