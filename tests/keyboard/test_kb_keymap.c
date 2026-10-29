// Moonlight XR Native, tests/keyboard: the AZERTY and QWERTZ key maps (PLAN 2.10) on both keyboards.
// The keys send the US keyboard's key positions and the PC's Windows layout turns them into
// characters, so what is checked is that a PC on the French (AZERTY) or German (QWERTZ) layout
// types what each key says. The hosts below are written out as tables, independent of the keyboard's
// own, and the Compact keys are run through VrKeyboard (stream/vr_keyboard.c) the way the headset
// does, into a host that reads the events with them. Also: every map is as full as QWERTY, no
// duplicate labels on a sheet, every label has a glyph in the font (the accents and the sharp s
// included), and the panels draw with the right hit regions. GPLv3.
#include <Limelight.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "../../stream/vr_keyboard.h"
#include "panels.h"
#include "settings.h"
#include "text.h"
#include "xr_kbfull.h"
#include "xr_keys.h"

// ---- the PC: what the Windows layouts make of a US key position ------------------------------

typedef struct { int vk; const char* plain; const char* shift; } HostKey;

static const HostKey HOST_US[] = {
    {0xC0,"`","~"},{0x31,"1","!"},{0x32,"2","@"},{0x33,"3","#"},{0x34,"4","$"},{0x35,"5","%"},
    {0x36,"6","^"},{0x37,"7","&"},{0x38,"8","*"},{0x39,"9","("},{0x30,"0",")"},{0xBD,"-","_"},
    {0xBB,"=","+"},{0xDB,"[","{"},{0xDD,"]","}"},{0xDC,"\\","|"},{0xBA,";",":"},{0xDE,"'","\""},
    {0xBC,",","<"},{0xBE,".",">"},{0xBF,"/","?"},
};
// French (France), AZERTY. The ^ and the diaeresis are dead keys: they are listed as the key says.
static const HostKey HOST_FR[] = {
    {0xC0,"\xC2\xB2",""},{0x31,"&","1"},{0x32,"\xC3\xA9","2"},{0x33,"\"","3"},{0x34,"'","4"},
    {0x35,"(","5"},{0x36,"-","6"},{0x37,"\xC3\xA8","7"},{0x38,"_","8"},{0x39,"\xC3\xA7","9"},
    {0x30,"\xC3\xA0","0"},{0xBD,")","\xC2\xB0"},{0xBB,"=","+"},
    {0x51,"a","A"},{0x57,"z","Z"},{0xDB,"^","\xC2\xA8"},{0xDD,"$","\xC2\xA3"},{0xDC,"*","\xC2\xB5"},
    {0x41,"q","Q"},{0xBA,"m","M"},{0xDE,"\xC3\xB9","%"},
    {0x5A,"w","W"},{0x4D,",","?"},{0xBC,";","."},{0xBE,":","/"},{0xBF,"!","\xC2\xA7"},{0xE2,"<",">"},
};
// German (Germany), QWERTZ. ^ and the acute are dead keys.
static const HostKey HOST_DE[] = {
    {0xC0,"^","\xC2\xB0"},{0x31,"1","!"},{0x32,"2","\""},{0x33,"3","\xC2\xA7"},{0x34,"4","$"},
    {0x35,"5","%"},{0x36,"6","&"},{0x37,"7","/"},{0x38,"8","("},{0x39,"9",")"},{0x30,"0","="},
    {0xBD,"\xC3\x9F","?"},{0xBB,"\xC2\xB4","`"},
    {0x59,"z","Z"},{0x5A,"y","Y"},{0xDB,"\xC3\xBC","\xC3\x9C"},{0xDD,"+","*"},{0xDC,"#","'"},
    {0xBA,"\xC3\xB6","\xC3\x96"},{0xDE,"\xC3\xA4","\xC3\x84"},
    {0xBC,",",";"},{0xBE,".",":"},{0xBF,"-","_"},{0xE2,"<",">"},
};

// plain and shifted character of a US key position on the host layout of a map; 0 if the key
// is not one that types a character (arrows, F keys)
static int hostChar(int map, int vk, const char** plain, const char** shift) {
    static char letters[2][2];
    const HostKey* t = HOST_US;
    int n = (int)(sizeof(HOST_US) / sizeof(HOST_US[0]));
    if (map == KB_MAP_AZERTY) { t = HOST_FR; n = (int)(sizeof(HOST_FR) / sizeof(HOST_FR[0])); }
    if (map == KB_MAP_QWERTZ) { t = HOST_DE; n = (int)(sizeof(HOST_DE) / sizeof(HOST_DE[0])); }
    for (int i = 0; i < n; i++) {
        if (t[i].vk == vk) { *plain = t[i].plain; *shift = t[i].shift; return 1; }
    }
    // the letters the layouts leave where the US keyboard has them
    if (vk >= 0x41 && vk <= 0x5A) {
        letters[0][0] = (char)(vk + 32);
        letters[1][0] = (char)vk;
        *plain = letters[0];
        *shift = letters[1];
        return 1;
    }
    return 0;
}

// ---- the keyboard's presses as the host reads them -------------------------------------------

typedef struct { int text; int vk; int action; int mods; char s[8]; } Ev;
static Ev g_ev[16];
static int g_n;

static void logKey(void* user, short code, char action, char mods) {
    (void)user;
    if (g_n < 16) {
        g_ev[g_n].text = 0;
        g_ev[g_n].vk = (unsigned short)code;
        g_ev[g_n].action = action;
        g_ev[g_n].mods = mods;
        g_ev[g_n].s[0] = 0;
        g_n++;
    }
}
static void logText(void* user, const char* s) {
    (void)user;
    if (g_n < 16) {
        g_ev[g_n].text = 1;
        g_ev[g_n].s[0] = 0;
        strncat(g_ev[g_n].s, s, sizeof g_ev[g_n].s - 1);
        g_n++;
    }
}

static int isModifierVk(int vk) {
    return vk == 0x10 || vk == 0x11 || vk == 0x12 || (vk >= 0xA0 && vk <= 0xA5) || vk == 0x5B || vk == 0x5C;
}

// What the host types for a code on a map's keyboard: the text, or the character the layout gives
// the key (shifted when Shift went with it). NULL if it types no character (a control key).
static const char* typed(int map, int code) {
    g_n = 0;
    VrKeyboard k;
    VrKeyboardSink sink = { NULL, logKey, logText };
    vrkb_init(&k, sink);
    vrkb_type(&k, code, 0);
    for (int i = 0; i < g_n; i++) {
        if (g_ev[i].text) return g_ev[i].s;
        if (g_ev[i].action == KEY_ACTION_DOWN && !isModifierVk(g_ev[i].vk)) {
            const char *plain, *shift;
            if (!hostChar(map, g_ev[i].vk, &plain, &shift)) return NULL;
            return (g_ev[i].mods & MODIFIER_SHIFT) ? shift : plain;
        }
    }
    return NULL;
}

// ---- helpers -----------------------------------------------------------------------------------

static UiFont* g_font;

static void glyphs(const char* label, const char* where) {
    if (!label || label[0] == '@') return;
    const char* end = label + strlen(label);
    for (const char* p = label; p < end;) {
        uint32_t cp;
        p += ui_utf8_decode(p, end, &cp);
        int has = ui_font_has(g_font, UI_WEIGHT_REGULAR, cp);
        CHECK(has);
        if (!has) fprintf(stderr, "no glyph for U+%04X in %s \"%s\"\n", cp, where, label);
    }
}

static int printable(int code) {
    // text and the keys of the digit and letter rows; the control keys do not type a character
    return code > 0 && code != 8 && code != 9 && code != 13 && code != 32;
}

static const char* MAP_NAMES[KB_MAP_COUNT] = { "qwerty", "azerty", "qwertz" };

// ---- Compact -------------------------------------------------------------------------------------

static void testCompact(int map) {
    const char* name = MAP_NAMES[map];
    int labelled[KB_STATE_COUNT];
    for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
        labelled[sheet] = 0;
        for (int i = 0; i < ui_kb_key_count(); i++) {
            const char* a = ui_kb_map_key_label(map, sheet, i);
            int code = ui_kb_map_key_code(map, sheet, i);
            CHECK((a == NULL) == (code == 0));
            if (!a) continue;
            labelled[sheet]++;
            glyphs(a, name);
            // no two keys of a sheet say the same
            for (int j = i + 1; j < ui_kb_key_count(); j++) {
                const char* b = ui_kb_map_key_label(map, sheet, j);
                CHECK(b == NULL || strcmp(a, b) != 0);
                if (b != NULL && strcmp(a, b) == 0) fprintf(stderr, "%s sheet %d: \"%s\" twice\n", name, sheet, a);
            }
            // the host types what the key says (the F keys, arrows and the like type no character)
            if (!printable(code)) continue;
            const char* got = typed(map, code);
            if (got == NULL) {
                CHECK(code >= KB_CODE_VK && code < KB_CODE_SVK);
                continue;
            }
            CHECK(strcmp(got, a) == 0);
            if (strcmp(got, a) != 0) {
                fprintf(stderr, "%s sheet %d key %d \"%s\" (code %d) types \"%s\"\n", name, sheet, i, a, code, got);
            }
        }
    }
    // the same keys on every map: the three letter and symbol sheets are full, the Fn sheet has
    // at least what QWERTY has
    CHECK(labelled[KB_STATE_LOWER] == 44 && labelled[KB_STATE_UPPER] == 44 && labelled[KB_STATE_SYMBOLS] == 44);
    CHECK(labelled[KB_STATE_FN] >= 35);
    // the controls stay where they are, so the sheets flip and hide as they did
    for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
        for (int i = 0; i < ui_kb_key_count(); i++) {
            int code = ui_kb_map_key_code(map, sheet, i);
            int base = ui_kb_map_key_code(KB_MAP_QWERTY, sheet, i);
            if (base < 0 || base == 8 || base == 9 || base == 13 || base == 32) CHECK(code == base);
        }
    }
    // every letter of the alphabet is on the letter sheets of the map, once
    for (int sheet = KB_STATE_LOWER; sheet <= KB_STATE_UPPER; sheet++) {
        for (int c = 0; c < 26; c++) {
            char want[2] = { (char)((sheet == KB_STATE_LOWER ? 'a' : 'A') + c), 0 };
            int found = 0;
            for (int i = 0; i < ui_kb_key_count(); i++) {
                const char* l = ui_kb_map_key_label(map, sheet, i);
                found += l != NULL && strcmp(l, want) == 0;
            }
            CHECK(found == 1);
            if (found != 1) fprintf(stderr, "%s sheet %d: %s found %d times\n", name, sheet, want, found);
        }
    }
}

// The characters a person of that language expects to reach on the Compact keyboard
static int onCompact(int map, const char* utf8) {
    for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++)
        for (int i = 0; i < ui_kb_key_count(); i++) {
            const char* l = ui_kb_map_key_label(map, sheet, i);
            if (l && strcmp(l, utf8) == 0) return 1;
        }
    return 0;
}

static void testCompactReach(void) {
    static const char* FR[] = { "\xC3\xA9", "\xC3\xA8", "\xC3\xA0", "\xC3\xA7", "\xC3\xB9", "m", "a", "z", "q", "w", ",", ";", ":", "!", "?", "=" , "&", "\"", "'", "(", ")", "-", "_" };
    static const char* DE[] = { "\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xC3\x84", "\xC3\x96", "\xC3\x9C", "z", "y", "\xC2\xA7", "=", "/", "(", ")", "\"", "$", "&", "%", "[", "]", "{", "}", "\\", "<", ">", "|" };
    for (size_t i = 0; i < sizeof FR / sizeof FR[0]; i++) {
        CHECK(onCompact(KB_MAP_AZERTY, FR[i]));
        if (!onCompact(KB_MAP_AZERTY, FR[i])) fprintf(stderr, "AZERTY compact lacks %s\n", FR[i]);
    }
    for (size_t i = 0; i < sizeof DE / sizeof DE[0]; i++) {
        CHECK(onCompact(KB_MAP_QWERTZ, DE[i]));
        if (!onCompact(KB_MAP_QWERTZ, DE[i])) fprintf(stderr, "QWERTZ compact lacks %s\n", DE[i]);
    }
}

// Shift is one shot over the keys that are letters on the map, and stays over the rest
static void testShiftSheet(void) {
    // QWERTY as it was
    CHECK(kbPress(KB_STATE_UPPER, 0, 'A').sheet == KB_STATE_LOWER);
    CHECK(kbPress(KB_STATE_UPPER, 0, '!').sheet == KB_STATE_UPPER);
    // AZERTY's M (the US semicolon key) is a letter; its digits are not
    CHECK(kbPress(KB_STATE_UPPER, 0, KB_CODE_SVKL + 0xBA).sheet == KB_STATE_LOWER);
    CHECK(kbPress(KB_STATE_UPPER, 0, KB_CODE_SVK + '1').sheet == KB_STATE_UPPER);
    CHECK(kbPress(KB_STATE_UPPER, 0, KB_CODE_SVK + '1').code == KB_CODE_SVK + '1');
    // and the codes are real codes on the lower sheet too
    CHECK(kbPress(KB_STATE_LOWER, 0, KB_CODE_VK + 0xBA).sheet == KB_STATE_LOWER);
}

// ---- Full ----------------------------------------------------------------------------------------

static int startsWithAt(const char* s) { return s != NULL && s[0] == '@'; }

static void testFull(int map) {
    const char* name = MAP_NAMES[map];
    int count = kbfKeyCountFor(map);
    CHECK(count == (map == KB_MAP_QWERTY ? 88 : 89));
    CHECK(count <= KBF_KEY_COUNT_MAX);

    int keysWithVk = 0;
    for (int i = 0; i < count; i++) {
        const KbfKey* k = kbfKey(i);
        CHECK(k != NULL);
        if (!k) continue;
        const char* plain = kbfLabelFor(map, i, 0, 0);
        const char* shifted = kbfLabelFor(map, i, KBF_LSHIFT, 0);
        CHECK(plain != NULL && shifted != NULL);
        glyphs(plain, name);
        glyphs(shifted, name);
        // no two keys say the same, unshifted or shifted (the pairs of modifiers say the same word)
        if (!startsWithAt(plain)) {
            for (int j = i + 1; j < count; j++) {
                const char* b = kbfLabelFor(map, j, 0, 0);
                CHECK(strcmp(plain, b) != 0);
                if (strcmp(plain, b) == 0) fprintf(stderr, "%s full: \"%s\" twice\n", name, plain);
            }
        }
        if (k->kind == KBF_NORMAL && strcmp(shifted, plain) != 0) {
            for (int j = i + 1; j < count; j++) {
                const char* b = kbfLabelFor(map, j, KBF_LSHIFT, 0);
                if (kbfKey(j)->kind != KBF_NORMAL || strcmp(b, kbfLabelFor(map, j, 0, 0)) == 0) continue;
                CHECK(strcmp(shifted, b) != 0);
                if (strcmp(shifted, b) == 0) fprintf(stderr, "%s full: shifted \"%s\" twice\n", name, b);
            }
        }
        // the key sits at a US position and the PC's layout makes the character it says
        const char *hp, *hs;
        if (k->kind == KBF_NORMAL && hostChar(map, k->vk, &hp, &hs)) {
            keysWithVk++;
            CHECK(strcmp(plain, hp) == 0);
            if (strcmp(plain, hp) != 0) fprintf(stderr, "%s full vk %02X says \"%s\", PC types \"%s\"\n", name, k->vk, plain, hp);
            if (hs[0] != 0) {
                CHECK(strcmp(shifted, hs) == 0);
                if (strcmp(shifted, hs) != 0) fprintf(stderr, "%s full vk %02X shifted says \"%s\", PC types \"%s\"\n", name, k->vk, shifted, hs);
            }
            // Caps Lock capitalises the letters (and on a French keyboard the digit row gives digits)
            const char* caps = kbfLabelFor(map, i, 0, 1);
            if (hp[0] >= 'a' && hp[0] <= 'z' && hp[1] == 0) CHECK(strcmp(caps, hs) == 0);
        }
    }
    CHECK(keysWithVk >= 45);
    // geometry: inside the panel and no overlaps
    for (int i = 0; i < count; i++) {
        float a[4];
        kbfKeyRectFor(map, i, a);
        CHECK(a[0] >= 0 && a[1] >= 0 && a[2] <= 1 && a[3] <= 1 && a[2] > a[0] && a[3] > a[1]);
        for (int j = i + 1; j < count; j++) {
            float b[4];
            kbfKeyRectFor(map, j, b);
            CHECK(a[2] <= b[0] || b[2] <= a[0] || a[3] <= b[1] || b[3] <= a[1]);
        }
    }
    // the same keys in the same rows as QWERTY: every key but the left Shift stays where it was
    for (int i = 0; i < KBF_KEY_COUNT; i++) {
        float a[4], q[4];
        kbfKeyRectFor(map, i, a);
        kbfKeyRect(i, q);
        if (kbfKey(i)->kind == KBF_MOD && kbfKey(i)->modBit == KBF_LSHIFT) {
            CHECK(a[0] == q[0] && a[1] == q[1] && (map == KB_MAP_QWERTY ? a[2] == q[2] : a[2] < q[2]));
        } else {
            CHECK(a[0] == q[0] && a[1] == q[1] && a[2] == q[2] && a[3] == q[3]);
        }
    }
    if (map != KB_MAP_QWERTY) {
        // the extra key fills what the Shift gave up, between the Shift and Z
        const KbfKey* iso = kbfKey(KBF_ISO_KEY);
        CHECK(iso->vk == 0xE2 && iso->kind == KBF_NORMAL);
        CHECK(strcmp(kbfLabelFor(map, KBF_ISO_KEY, 0, 0), "<") == 0 && strcmp(kbfLabelFor(map, KBF_ISO_KEY, KBF_RSHIFT, 0), ">") == 0);
        float s[4], x[4], z[4];
        kbfKeyRectFor(map, kbfFindVk(0xA0), s);
        kbfKeyRectFor(map, KBF_ISO_KEY, x);
        kbfKeyRectFor(map, kbfFindVk(0x5A), z);
        CHECK(s[1] == x[1] && x[1] == z[1]);
        CHECK(x[0] > s[2] && z[0] > x[2]);
        CHECK(x[0] - s[2] < 0.01f && z[0] - x[2] < 0.01f);   // only the gap between keys
    } else {
        float none[4];
        kbfKeyRectFor(KB_MAP_QWERTY, KBF_ISO_KEY, none);
        CHECK(none[0] == 0 && none[2] == 0);
    }
}

// ---- the panels ------------------------------------------------------------------------------------

static uint8_t g_px[KB_TEX_W * KB_TEX_H * 4];

static int inkOf(void) {
    int n = 0;
    for (size_t i = 3; i < sizeof g_px; i += 4) n += g_px[i] > 0;
    return n;
}

static void testPanels(void) {
    Settings st;
    settings_defaults(&st);
    for (int map = 0; map < KB_MAP_COUNT; map++) {
        UiView v;
        ui_view_init(&v, &st);
        v.kb_map = map;
        UiHits hits;
        UiCanvas cv;
        int plainInk = 0;
        for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
            v.kb_sheet = sheet;
            ui_cv_init(&cv, g_px, KB_TEX_W, KB_TEX_H);
            ui_cv_clear(&cv);
            ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, &hits);
            int expect = 0;
            for (int i = 0; i < ui_kb_key_count(); i++) expect += ui_kb_map_key_label(map, sheet, i) != NULL;
            CHECK(hits.count == expect);
            for (int i = 0; i < hits.count; i++) {
                CHECK(hits.hit[i].b == ui_kb_map_key_code(map, sheet, hits.hit[i].a));
            }
            CHECK(inkOf() > KB_TEX_W * KB_TEX_H / 40);
            if (sheet == KB_STATE_LOWER) plainInk = inkOf();
        }
        CHECK(plainInk > 0);
        // Full, plain and with Shift and Caps
        v.kb_layout = KB_LAYOUT_FULL;
        int masks[3][2] = { { 0, 0 }, { KBF_LSHIFT, 0 }, { 0, 1 } };
        for (int m = 0; m < 3; m++) {
            v.kb_oneshot = masks[m][0];
            v.kb_caps = masks[m][1];
            ui_cv_init(&cv, g_px, KB_TEX_W, KB_TEX_H);
            ui_cv_clear(&cv);
            ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, &hits);
            CHECK(hits.count == kbfKeyCountFor(map));
            for (int i = 0; i < hits.count; i++) {
                CHECK(hits.hit[i].a >= 0 && hits.hit[i].a < kbfKeyCountFor(map));
                CHECK(hits.hit[i].u1 <= 1.0f && hits.hit[i].v1 <= 1.0f && hits.hit[i].u0 >= 0.0f);
            }
            CHECK(inkOf() > KB_TEX_W * KB_TEX_H / 40);
        }
    }
    // a map that is not one draws QWERTY
    UiView v;
    ui_view_init(&v, &st);
    v.kb_map = 99;
    UiHits hits;
    UiCanvas cv;
    ui_cv_init(&cv, g_px, KB_TEX_W, KB_TEX_H);
    ui_cv_clear(&cv);
    ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, &hits);
    CHECK(hits.count == 44);
    CHECK(ui_kb_map_key_code(-1, KB_STATE_LOWER, 10) == ui_kb_key_code(KB_STATE_LOWER, 10));
}

// ---- the setting -------------------------------------------------------------------------------------

static void testSetting(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(s.keyboard_keymap == KB_MAP_QWERTY);
    char* text = settings_to_text(&s);
    CHECK(strstr(text, "#keyboard_keymap = qwerty") != NULL);
    free(text);
    static const char* NAMES[KB_MAP_COUNT] = { "qwerty", "azerty", "qwertz" };
    for (int m = 0; m < KB_MAP_COUNT; m++) {
        char ini[128];
        snprintf(ini, sizeof ini, "[vr]\nkeyboard_keymap = %s\n", NAMES[m]);
        Settings back;
        settings_defaults(&back);
        SettingsLoadInfo info;
        settings_from_text(&back, ini, &info);
        CHECK(back.keyboard_keymap == m && info.bad == 0 && info.unknown == 0);
        if (m != 0) {
            text = settings_to_text(&back);
            char want[64];
            snprintf(want, sizeof want, "keyboard_keymap = %s", NAMES[m]);
            CHECK(strstr(text, want) != NULL);
            free(text);
        }
    }
    // a name nobody knows is not taken
    Settings bad;
    settings_defaults(&bad);
    SettingsLoadInfo info;
    settings_from_text(&bad, "[vr]\nkeyboard_keymap = dvorak\n", &info);
    CHECK(bad.keyboard_keymap == KB_MAP_QWERTY && info.bad == 1);
}

int main(void) {
    g_font = ui_font_open(MLXR_ASSETS_DIR "/fonts/NotoSans-Regular.ttf", MLXR_ASSETS_DIR "/fonts/NotoSans-Medium.ttf");
    CHECK(g_font != NULL);
    if (!g_font) return checksDone("kb keymap");

    // the characters that must draw, named outright
    static const char* MUST[] = { "\xC3\xA9", "\xC3\xA8", "\xC3\xA0", "\xC3\xA7", "\xC3\xB9", "\xC3\xBC", "\xC3\xB6",
                                  "\xC3\xA4", "\xC3\x9F", "\xC3\x9C", "\xC3\x96", "\xC3\x84", "\xC2\xA7", "\xC2\xB2",
                                  "\xC2\xB0", "\xC2\xA3", "\xC2\xB5", "\xC2\xA8", "\xC2\xB4" };
    for (size_t i = 0; i < sizeof MUST / sizeof MUST[0]; i++) glyphs(MUST[i], "the list of accents");

    for (int map = 0; map < KB_MAP_COUNT; map++) {
        testCompact(map);
        testFull(map);
    }
    testCompactReach();
    testShiftSheet();
    testPanels();
    testSetting();
    ui_font_close(g_font);
    return checksDone("kb keymap");
}
