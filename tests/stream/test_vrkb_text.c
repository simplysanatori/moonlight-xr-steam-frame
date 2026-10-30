// Moonlight XR Native (lane wave4-apollo): vrkb_type_text, the text path "Paste from PC" types a
// clipboard through: pieces cut between characters, line ends and tabs as their keys, nothing but
// text otherwise. GPLv3.
#include <Limelight.h>
#include <stdio.h>
#include <string.h>

#include "../../stream/vr_keyboard.h"
#include "../xrcore/check.h"

static char g_log[4096];

static void key(void* user, short code, char action, char mods) {
    (void)user;
    char b[48];
    snprintf(b, sizeof(b), "%s%x/%d ", action == KEY_ACTION_DOWN ? "+" : "-", (unsigned short)code, mods);
    strncat(g_log, b, sizeof(g_log) - strlen(g_log) - 1);
}

static void text(void* user, const char* s) {
    (void)user;
    strncat(g_log, "t:", sizeof(g_log) - strlen(g_log) - 1);
    strncat(g_log, s, sizeof(g_log) - strlen(g_log) - 1);
    strncat(g_log, " ", sizeof(g_log) - strlen(g_log) - 1);
}

static VrKeyboard g_kb;

static size_t type(const char* s, size_t max) {
    g_log[0] = '\0';
    return vrkb_type_text(&g_kb, s, max);
}

int main(void) {
    VrKeyboardSink sink = { NULL, key, text };
    vrkb_init(&g_kb, sink);

    // plain text goes whole, as one piece
    CHECK(type("hello world", 4096) == 11 && strcmp(g_log, "t:hello world ") == 0);
    CHECK(type("", 4096) == 0 && g_log[0] == '\0');
    CHECK(type(NULL, 4096) == 0 && g_log[0] == '\0');
    // line ends and tabs are keys, and split the text round them
    CHECK(type("a\nb", 4096) == 3 && strcmp(g_log, "t:a +d/0 -d/0 t:b ") == 0);
    CHECK(type("a\r\nb", 4096) == 4 && strcmp(g_log, "t:a +d/0 -d/0 t:b ") == 0);   // CR LF is one Enter
    CHECK(type("a\rb", 4096) == 3 && strcmp(g_log, "t:a +d/0 -d/0 t:b ") == 0);     // a lone CR too
    CHECK(type("\n\n", 4096) == 2 && strcmp(g_log, "+d/0 -d/0 +d/0 -d/0 ") == 0);
    CHECK(type("x\ty", 4096) == 3 && strcmp(g_log, "t:x +9/0 -9/0 t:y ") == 0);
    // other control characters are left out, and a byte that is not UTF-8
    CHECK(type("a\x01" "b\x1b" "c", 4096) == 5 && strcmp(g_log, "t:abc ") == 0);
    CHECK(type("a\xff" "b", 4096) == 3 && strcmp(g_log, "t:ab ") == 0);
    // multi-byte characters stay whole
    CHECK(type("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", 4096) == 14
          && strcmp(g_log, "t:caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 ") == 0);
    // a character cut off at the end is dropped, not sent half
    CHECK(type("ab\xE2\x82", 4096) == 4 && strcmp(g_log, "t:ab ") == 0);

    // pieces of at most VRKB_TEXT_CHUNK bytes, cut between characters
    char big[300];
    memset(big, 'x', sizeof(big));
    big[200] = '\0';
    CHECK(type(big, 4096) == 200);
    int pieces = 0, longest = 0, total = 0;
    for (char* p = strstr(g_log, "t:"); p != NULL; p = strstr(p + 2, "t:")) {
        char* end = strchr(p, ' ');
        int len = (int)(end - p) - 2;
        pieces++;
        total += len;
        if (len > longest) longest = len;
    }
    CHECK(pieces == 4 && longest == VRKB_TEXT_CHUNK && total == 200);
    char wide[200];
    for (int i = 0; i < 60; i++) {
        wide[i * 3] = (char)0xE2;
        wide[i * 3 + 1] = (char)0x82;
        wide[i * 3 + 2] = (char)0xAC;
    }
    wide[180] = '\0';
    CHECK(type(wide, 4096) == 180);
    // 64 / 3 -> 21 characters (63 bytes) to a piece: 60 characters in 3 pieces, none cut in the middle
    pieces = 0;
    for (char* p = strstr(g_log, "t:"); p != NULL; p = strstr(p + 2, "t:")) {
        char* end = strchr(p, ' ');
        CHECK(((int)(end - p) - 2) % 3 == 0);
        pieces++;
    }
    CHECK(pieces == 3);

    // a limit stops before the character that would pass it, and says how far it got
    CHECK(type("hello world", 5) == 5 && strcmp(g_log, "t:hello ") == 0);
    CHECK(type("ab\xC3\xA9", 3) == 2 && strcmp(g_log, "t:ab ") == 0);          // the \xC3\xA9 would reach 4
    CHECK(type("ab\ncd", 3) == 3 && strcmp(g_log, "t:ab +d/0 -d/0 ") == 0);
    CHECK(type("abc", 0) == 0 && g_log[0] == '\0');

    // nothing is left held on the host
    CHECK(g_kb.held == 0 && g_kb.heldKey == 0);
    return checksDone("vrkb_text");
}
