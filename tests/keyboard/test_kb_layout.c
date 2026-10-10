// Moonlight XR Native, tests/keyboard: the Full keyboard's layout (xr_kbfull.c)
// is a tenkeyless one with the right Windows virtual keys. GPLv3.
#include <string.h>

#include "check.h"
#include "xr_kbfull.h"

// What a TKL has, in the order of its rows: vk, extended
typedef struct { int vk, ext; const char* name; } Want;
static const Want WANT[] = {
    // row 0
    {0x1B,0,"Esc"},{0x70,0,"F1"},{0x71,0,"F2"},{0x72,0,"F3"},{0x73,0,"F4"},{0x74,0,"F5"},
    {0x75,0,"F6"},{0x76,0,"F7"},{0x77,0,"F8"},{0x78,0,"F9"},{0x79,0,"F10"},{0x7A,0,"F11"},
    {0x7B,0,"F12"},{0x2C,1,"PrtSc"},{0x91,0,"ScrLk"},{0x13,0,"Pause"},
    // row 1
    {0xC0,0,"`"},{0x31,0,"1"},{0x32,0,"2"},{0x33,0,"3"},{0x34,0,"4"},{0x35,0,"5"},{0x36,0,"6"},
    {0x37,0,"7"},{0x38,0,"8"},{0x39,0,"9"},{0x30,0,"0"},{0xBD,0,"-"},{0xBB,0,"="},{0x08,0,"Backspace"},
    {0x2D,1,"Ins"},{0x24,1,"Home"},{0x21,1,"PgUp"},
    // row 2
    {0x09,0,"Tab"},{0x51,0,"Q"},{0x57,0,"W"},{0x45,0,"E"},{0x52,0,"R"},{0x54,0,"T"},{0x59,0,"Y"},
    {0x55,0,"U"},{0x49,0,"I"},{0x4F,0,"O"},{0x50,0,"P"},{0xDB,0,"["},{0xDD,0,"]"},{0xDC,0,"\\"},
    {0x2E,1,"Del"},{0x23,1,"End"},{0x22,1,"PgDn"},
    // row 3
    {0x14,0,"Caps"},{0x41,0,"A"},{0x53,0,"S"},{0x44,0,"D"},{0x46,0,"F"},{0x47,0,"G"},{0x48,0,"H"},
    {0x4A,0,"J"},{0x4B,0,"K"},{0x4C,0,"L"},{0xBA,0,";"},{0xDE,0,"'"},{0x0D,0,"Enter"},
    // row 4
    {0xA0,0,"LShift"},{0x5A,0,"Z"},{0x58,0,"X"},{0x43,0,"C"},{0x56,0,"V"},{0x42,0,"B"},{0x4E,0,"N"},
    {0x4D,0,"M"},{0xBC,0,","},{0xBE,0,"."},{0xBF,0,"/"},{0xA1,0,"RShift"},{0x26,1,"Up"},
    // row 5
    {0xA2,0,"LCtrl"},{0x5B,1,"LWin"},{0xA4,0,"LAlt"},{0x20,0,"Space"},{0xA5,1,"RAlt"},
    {0x5C,1,"RWin"},{0x5D,1,"Menu"},{0xA3,1,"RCtrl"},{0x25,1,"Left"},{0x28,1,"Down"},{0x27,1,"Right"},
};
#define WANTS ((int)(sizeof(WANT) / sizeof(WANT[0])))

static const char* nameOf(int vk) {
    for (int i = 0; i < WANTS; i++) if (WANT[i].vk == vk) return WANT[i].name;
    return "?";
}

int main(void) {
    // 87 keys of a tenkeyless, and the layout key
    CHECK(WANTS == 87);
    CHECK(KBF_KEY_COUNT == 88);
    int layoutKeys = 0, w = 0;
    for (int i = 0; i < KBF_KEY_COUNT; i++) {
        const KbfKey* k = kbfKey(i);
        CHECK(k != NULL && k->label != NULL);
        if (k->kind == KBF_LAYOUT) {
            layoutKeys++;
            CHECK(kbfKeyCode(i) == KB_CODE_LAYOUT);
            continue;
        }
        // present once, with the right extended flag, in the row order a TKL has
        CHECK(w < WANTS && k->vk == WANT[w].vk);
        CHECK(w < WANTS && k->ext == WANT[w].ext);
        w++;
        CHECK(kbfKeyCode(i) == KB_CODE_VK + k->vk);
        CHECK(kbfFindVk(k->vk) == i);
        for (int j = i + 1; j < KBF_KEY_COUNT; j++) CHECK(kbfKey(j)->vk != k->vk || kbfKey(j)->kind == KBF_LAYOUT);
    }
    CHECK(layoutKeys == 1);
    for (int i = 0; i < WANTS; i++) {
        int at = kbfFindVk(WANT[i].vk);
        CHECK(at >= 0);
        if (at < 0) fprintf(stderr, "missing %s\n", WANT[i].name);
    }
    CHECK(strcmp(nameOf(0x2C), "PrtSc") == 0);

    // Modifiers: both sides, each its own bit and its own VK
    struct { int vk, bit; } mods[] = {
        {0xA0,KBF_LSHIFT},{0xA1,KBF_RSHIFT},{0xA2,KBF_LCTRL},{0xA3,KBF_RCTRL},
        {0xA4,KBF_LALT},{0xA5,KBF_RALT},{0x5B,KBF_LWIN},{0x5C,KBF_RWIN},
    };
    int seen = 0;
    for (int m = 0; m < 8; m++) {
        int at = kbfFindVk(mods[m].vk);
        CHECK(at >= 0 && kbfKey(at)->kind == KBF_MOD && kbfKey(at)->modBit == mods[m].bit);
        seen |= mods[m].bit;
    }
    CHECK(seen == 0xFF);
    // the old Compact bits are the left side's
    CHECK(KBF_LCTRL == KB_MOD_CTRL && KBF_LALT == KB_MOD_ALT && KBF_LWIN == KB_MOD_WIN);
    CHECK(kbfKey(kbfFindVk(0x14))->kind == KBF_CAPS);
    CHECK(kbfModifierByte(KBF_RSHIFT) == 1 && kbfModifierByte(KBF_RCTRL | KBF_LALT) == 6
          && kbfModifierByte(KBF_RWIN) == 8);

    // Geometry: inside the panel, no overlaps, TKL proportions
    for (int i = 0; i < KBF_KEY_COUNT; i++) {
        float a[4];
        kbfKeyRect(i, a);
        CHECK(a[0] >= 0 && a[1] >= 0 && a[2] <= 1 && a[3] <= 1 && a[2] > a[0] && a[3] > a[1]);
        for (int j = i + 1; j < KBF_KEY_COUNT; j++) {
            float b[4];
            kbfKeyRect(j, b);
            CHECK(a[2] <= b[0] || b[2] <= a[0] || a[3] <= b[1] || b[3] <= a[1]);
        }
    }
    float one[4], bs[4], tab[4], caps[4], enter[4], lsh[4], rsh[4], sp[4], q[4];
    kbfKeyRect(kbfFindVk(0x41), one);
    kbfKeyRect(kbfFindVk(0x08), bs);
    kbfKeyRect(kbfFindVk(0x09), tab);
    kbfKeyRect(kbfFindVk(0x14), caps);
    kbfKeyRect(kbfFindVk(0x0D), enter);
    kbfKeyRect(kbfFindVk(0xA0), lsh);
    kbfKeyRect(kbfFindVk(0xA1), rsh);
    kbfKeyRect(kbfFindVk(0x20), sp);
    kbfKeyRect(kbfFindVk(0x51), q);
    // widths in keys: the gap between keys is added back to each side of the ratio
    const float gap = 0.004f;
    float u = one[2] - one[0] + gap;
    bs[2] += gap;
    tab[2] += gap; caps[2] += gap; enter[2] += gap; lsh[2] += gap; rsh[2] += gap; sp[2] += gap;
    CHECK_NEAR((bs[2] - bs[0]) / u, 2.0, 0.1);
    CHECK_NEAR((tab[2] - tab[0]) / u, 1.5, 0.1);
    CHECK_NEAR((caps[2] - caps[0]) / u, 1.75, 0.1);
    CHECK_NEAR((enter[2] - enter[0]) / u, 2.25, 0.1);
    CHECK_NEAR((lsh[2] - lsh[0]) / u, 2.25, 0.1);
    CHECK_NEAR((rsh[2] - rsh[0]) / u, 2.75, 0.1);
    CHECK_NEAR((sp[2] - sp[0]) / u, 6.25, 0.1);
    // staggered like a real one: Q is half a key right of Tab's end, A a quarter more
    CHECK(q[0] > one[0] - 0.01f - 1.0f * u && q[0] < one[0]);
    // inverted T: Up above Down, Left and Right either side of it
    float up[4], down[4], left[4], right[4];
    kbfKeyRect(kbfFindVk(0x26), up);
    kbfKeyRect(kbfFindVk(0x28), down);
    kbfKeyRect(kbfFindVk(0x25), left);
    kbfKeyRect(kbfFindVk(0x27), right);
    CHECK(up[0] == down[0] && up[1] < down[1]);
    CHECK(left[1] == down[1] && right[1] == down[1] && left[2] <= down[0] + 1e-4f && right[0] >= down[2] - 1e-4f);
    // rows: the function row apart from the numbers, then five rows
    float esc[4], one1[4];
    kbfKeyRect(kbfFindVk(0x1B), esc);
    kbfKeyRect(kbfFindVk(0x31), one1);
    CHECK(one1[1] - esc[3] > 0.3f * (esc[3] - esc[1]));

    // Labels: the shifted symbol under Shift, a letter's capital under Shift or Caps (not both)
    int k1 = kbfFindVk(0x31), kq = kbfFindVk(0x51), ksemi = kbfFindVk(0xBA), kf1 = kbfFindVk(0x70);
    CHECK(strcmp(kbfLabel(k1, 0, 0), "1") == 0 && strcmp(kbfLabel(k1, KBF_LSHIFT, 0), "!") == 0);
    CHECK(strcmp(kbfLabel(k1, KBF_RSHIFT, 0), "!") == 0);
    CHECK(strcmp(kbfLabel(k1, 0, 1), "1") == 0);                       // Caps leaves numbers alone
    CHECK(strcmp(kbfLabel(kq, 0, 0), "q") == 0 && strcmp(kbfLabel(kq, KBF_LSHIFT, 0), "Q") == 0);
    CHECK(strcmp(kbfLabel(kq, 0, 1), "Q") == 0 && strcmp(kbfLabel(kq, KBF_LSHIFT, 1), "q") == 0);
    CHECK(strcmp(kbfLabel(ksemi, KBF_LSHIFT, 0), ":") == 0);
    CHECK(strcmp(kbfLabel(kf1, KBF_LSHIFT, 1), "F1") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0xC0), KBF_LSHIFT, 0), "~") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0xDE), KBF_LSHIFT, 0), "\"") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0xBC), KBF_LSHIFT, 0), "<") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0xBF), KBF_LSHIFT, 0), "?") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0xDC), KBF_LSHIFT, 0), "|") == 0);
    CHECK(strcmp(kbfLabel(kbfFindVk(0x39), KBF_LSHIFT, 0), "(") == 0 && strcmp(kbfLabel(kbfFindVk(0x30), KBF_LSHIFT, 0), ")") == 0);
    return checksDone("kb layout");
}
