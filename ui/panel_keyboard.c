// The in-world keyboard: four sheets (lower, upper, symbols, Fn) sharing one
// set of key rectangles, a held Ctrl, Alt and Win shown lit. Layout, labels
// and codes are XrPanels.java's KB_ tables. GPLv3.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "panels_int.h"
#include "xr_kbfull.h"

#define VK(c) (KB_CODE_VK + (c))
#define NKEYS 44

static const float ROW_WIDTHS[5][10] = {
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 1.5f, 1, 1, 1, 1, 1, 1, 1, 1.5f, 0 },
    { 1.5f, 1, 4, 1, 1.5f, 1, 0, 0, 0, 0 },
};
static const int ROW_KEYS[5] = { 10, 10, 9, 9, 6 };
static const float ROW_INDENT[5] = { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f };
#define ROW_UNITS 10.0f
#define PAD_U 0.012f
#define PAD_V 0.030f
#define GAP_U 0.005f
#define GAP_V 0.014f

// A label is the character(s) typed, or "@shift" style tokens resolved to
// words below. NULL = blank.
typedef struct { const char* label; int code; } Key;

static const Key LOWER[NKEYS] = {
    {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},
    {"q",'q'},{"w",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"y",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'},
    {"a",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},
    {"@shift",KB_CODE_SHIFT},{"z",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},{"m",'m'},{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key UPPER[NKEYS] = {
    {"!",'!'},{"@",'@'},{"#",'#'},{"$",'$'},{"%",'%'},{"^",'^'},{"&",'&'},{"*",'*'},{"(",'('},{")",')'},
    {"Q",'Q'},{"W",'W'},{"E",'E'},{"R",'R'},{"T",'T'},{"Y",'Y'},{"U",'U'},{"I",'I'},{"O",'O'},{"P",'P'},
    {"A",'A'},{"S",'S'},{"D",'D'},{"F",'F'},{"G",'G'},{"H",'H'},{"J",'J'},{"K",'K'},{"L",'L'},
    {"@shift",KB_CODE_SHIFT},{"Z",'Z'},{"X",'X'},{"C",'C'},{"V",'V'},{"B",'B'},{"N",'N'},{"M",'M'},{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key SYMBOLS[NKEYS] = {
    {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},
    {"@",'@'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"-",'-'},{"+",'+'},{"(",'('},{")",')'},
    {"!",'!'},{"\"",'"'},{"'",'\''},{":",':'},{";",';'},{"/",'/'},{"?",'?'},{"_",'_'},{"=",'='},
    {"Fn",KB_CODE_FN},{"<",'<'},{">",'>'},{"[",'['},{"]",']'},{"{",'{'},{"}",'}'},{"\\",'\\'},{"Del",8},
    {"ABC",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
// Arrows are drawn as shapes (Noto Sans has no arrow glyphs): "@up" "@down" "@left" "@right"
static const Key FN[NKEYS] = {
    {"Esc",VK(0x1B)},{"F1",VK(0x70)},{"F2",VK(0x71)},{"F3",VK(0x72)},{"F4",VK(0x73)},{"F5",VK(0x74)},
    {"F6",VK(0x75)},{"Ins",VK(0x2D)},{"Home",VK(0x24)},{"PgUp",VK(0x21)},
    {"Tab",9},{"F7",VK(0x76)},{"F8",VK(0x77)},{"F9",VK(0x78)},{"F10",VK(0x79)},{"F11",VK(0x7A)},
    {"F12",VK(0x7B)},{"Del",VK(0x2E)},{"End",VK(0x23)},{"PgDn",VK(0x22)},
    {NULL,0},{NULL,0},{NULL,0},{NULL,0},{NULL,0},{NULL,0},{"@up",VK(0x26)},{NULL,0},{NULL,0},
    {"?123",KB_CODE_FN},{"@ctrl",KB_CODE_CTRL},{"@win",KB_CODE_WIN},{"@alt",KB_CODE_ALT},{NULL,0},
    {"@left",VK(0x25)},{"@down",VK(0x28)},{"@right",VK(0x27)},{"Bksp",8},
    {"ABC",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
// ---- AZERTY and QWERTZ -------------------------------------------------------------------
//
// The code under a key is the US key it sits on, not the character it says:
// the PC's own layout (French or German) turns that key into the character,
// as it does for a real keyboard. P is that key pressed, S with Shift held
// round it (the digits of a French number row, the capitals of the umlauts),
// SL the same for a key that is a letter, so the Shift sheet lasts one key.
// Letters that sit where the US keyboard has a letter keep its code (the "a"
// of AZERTY is the Q key). Characters with no key worth a place on the sheet
// are typed as text, as on QWERTY.
#define KP(l, vk) { l, KB_CODE_VK + (vk) }
#define KS(l, vk) { l, KB_CODE_SVK + (vk) }
#define KSL(l, vk) { l, KB_CODE_SVKL + (vk) }

static const Key AZ_LOWER[NKEYS] = {
    KP("&",'1'),KP("é",'2'),KP("\"",'3'),KP("'",'4'),KP("(",'5'),KP("-",'6'),KP("è",'7'),KP("_",'8'),KP("ç",'9'),KP("à",'0'),
    {"a",'q'},{"z",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"y",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'},
    {"q",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},
    {"@shift",KB_CODE_SHIFT},{"w",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},KP("m",0xBA),{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key AZ_UPPER[NKEYS] = {
    KS("1",'1'),KS("2",'2'),KS("3",'3'),KS("4",'4'),KS("5",'5'),KS("6",'6'),KS("7",'7'),KS("8",'8'),KS("9",'9'),KS("0",'0'),
    {"A",'Q'},{"Z",'W'},{"E",'E'},{"R",'R'},{"T",'T'},{"Y",'Y'},{"U",'U'},{"I",'I'},{"O",'O'},{"P",'P'},
    {"Q",'A'},{"S",'S'},{"D",'D'},{"F",'F'},{"G",'G'},{"H",'H'},{"J",'J'},{"K",'K'},{"L",'L'},
    {"@shift",KB_CODE_SHIFT},{"W",'Z'},{"X",'X'},{"C",'C'},{"V",'V'},{"B",'B'},{"N",'N'},KSL("M",0xBA),{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key AZ_SYMBOLS[NKEYS] = {
    KS("1",'1'),KS("2",'2'),KS("3",'3'),KS("4",'4'),KS("5",'5'),KS("6",'6'),KS("7",'7'),KS("8",'8'),KS("9",'9'),KS("0",'0'),
    {"@",'@'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"-",'-'},{"+",'+'},{"(",'('},{")",')'},
    {"!",'!'},{"\"",'"'},{"'",'\''},{":",':'},{";",';'},{"/",'/'},{"?",'?'},KP("ù",0xDE),{"=",'='},
    {"Fn",KB_CODE_FN},{"<",'<'},{">",'>'},{"[",'['},{"]",']'},{"{",'{'},{"}",'}'},{"\\",'\\'},{"Del",8},
    {"ABC",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};

static const Key QZ_LOWER[NKEYS] = {
    {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},
    {"q",'q'},{"w",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"z",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'},
    {"a",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},
    {"@shift",KB_CODE_SHIFT},{"y",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},{"m",'m'},{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key QZ_UPPER[NKEYS] = {
    KS("!",'1'),KS("\"",'2'),KS("§",'3'),KS("$",'4'),KS("%",'5'),KS("&",'6'),KS("/",'7'),KS("(",'8'),KS(")",'9'),KS("=",'0'),
    {"Q",'Q'},{"W",'W'},{"E",'E'},{"R",'R'},{"T",'T'},{"Z",'Y'},{"U",'U'},{"I",'I'},{"O",'O'},{"P",'P'},
    {"A",'A'},{"S",'S'},{"D",'D'},{"F",'F'},{"G",'G'},{"H",'H'},{"J",'J'},{"K",'K'},{"L",'L'},
    {"@shift",KB_CODE_SHIFT},{"Y",'Z'},{"X",'X'},{"C",'C'},{"V",'V'},{"B",'B'},{"N",'N'},{"M",'M'},{"Del",8},
    {"?123",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
// The umlauts and the sharp s have the room of the seven bracket keys, which
// move to the Fn sheet: a German keyboard types them with AltGr
static const Key QZ_SYMBOLS[NKEYS] = {
    {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},
    {"@",'@'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"-",'-'},{"+",'+'},{"(",'('},{")",')'},
    {"!",'!'},{"\"",'"'},{"'",'\''},{":",':'},{";",';'},{"/",'/'},{"?",'?'},{"_",'_'},{"=",'='},
    {"Fn",KB_CODE_FN},KS("Ä",0xDE),KS("Ö",0xBA),KS("Ü",0xDB),KP("ä",0xDE),KP("ö",0xBA),KP("ü",0xDB),KP("ß",0xBD),{"Del",8},
    {"ABC",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};
static const Key QZ_FN[NKEYS] = {
    {"Esc",VK(0x1B)},{"F1",VK(0x70)},{"F2",VK(0x71)},{"F3",VK(0x72)},{"F4",VK(0x73)},{"F5",VK(0x74)},
    {"F6",VK(0x75)},{"Ins",VK(0x2D)},{"Home",VK(0x24)},{"PgUp",VK(0x21)},
    {"Tab",9},{"F7",VK(0x76)},{"F8",VK(0x77)},{"F9",VK(0x78)},{"F10",VK(0x79)},{"F11",VK(0x7A)},
    {"F12",VK(0x7B)},{"Del",VK(0x2E)},{"End",VK(0x23)},{"PgDn",VK(0x22)},
    {"<",'<'},{">",'>'},{"[",'['},{"]",']'},{"{",'{'},{"}",'}'},{"@up",VK(0x26)},{"\\",'\\'},{"|",'|'},
    {"?123",KB_CODE_FN},{"@ctrl",KB_CODE_CTRL},{"@win",KB_CODE_WIN},{"@alt",KB_CODE_ALT},{NULL,0},
    {"@left",VK(0x25)},{"@down",VK(0x28)},{"@right",VK(0x27)},{"Bksp",8},
    {"ABC",KB_CODE_SYMBOLS},{",",','},{"@space",32},{".",'.'},{"@enter",13},{"@hide",KB_CODE_HIDE},
};

static const Key* const SHEETS_BY_MAP[KB_MAP_COUNT][KB_STATE_COUNT] = {
    { LOWER, UPPER, SYMBOLS, FN },
    { AZ_LOWER, AZ_UPPER, AZ_SYMBOLS, FN },
    { QZ_LOWER, QZ_UPPER, QZ_SYMBOLS, QZ_FN },
};

static int mapOf(int map) { return (unsigned)map < KB_MAP_COUNT ? map : KB_MAP_QWERTY; }

static const char* resolve(const char* l) {
    if (!l || l[0] != '@') return l;
    if (!strcmp(l, "@shift")) return ui_str(UIS_KEY_SHIFT);
    if (!strcmp(l, "@space")) return ui_str(UIS_KEY_SPACE);
    if (!strcmp(l, "@enter")) return ui_str(UIS_KEY_ENTER);
    if (!strcmp(l, "@hide")) return ui_str(UIS_KEY_HIDE);
    if (!strcmp(l, "@ctrl")) return ui_str(UIS_KEY_CTRL);
    if (!strcmp(l, "@alt")) return ui_str(UIS_KEY_ALT);
    if (!strcmp(l, "@win")) return ui_str(UIS_KEY_WIN);
    return l;   // arrows stay as tokens
}

int ui_kb_key_count(void) { return NKEYS + KB_MEDIA_KEYS; }

// The media keys row's tokens, drawn as shapes (Noto Sans has none of the glyphs)
static const char* const MEDIA_LABEL[KB_MEDIA_KEYS] = { "@prev", "@play", "@next", "@mute", "@voldown", "@volup" };

void ui_kb_key_rect(int index, float r[4]) {
    if (index >= NKEYS) {
        kbMediaRect(index - NKEYS, r);
        return;
    }
    int row = 0, k = index;
    while (row < 5 && k >= ROW_KEYS[row]) { k -= ROW_KEYS[row]; row++; }
    float unit = (1.0f - 2.0f * PAD_U) / ROW_UNITS;
    float rowh = (1.0f - 2.0f * PAD_V) / 5.0f;
    float x = ROW_INDENT[row];
    for (int i = 0; i < k; i++) x += ROW_WIDTHS[row][i];
    float w = ROW_WIDTHS[row][k];
    r[0] = PAD_U + x * unit + GAP_U * 0.5f;
    r[1] = kbMainV(PAD_V + (float)row * rowh + GAP_V * 0.5f);
    r[2] = PAD_U + (x + w) * unit - GAP_U * 0.5f;
    r[3] = kbMainV(PAD_V + (float)(row + 1) * rowh - GAP_V * 0.5f);
}

// The half key of room at the right of the home row, on every sheet
void ui_kb_layout_chip_rect(float r[4]) {
    float key[4];
    ui_kb_key_rect(20, key);
    float unit = (1.0f - 2.0f * PAD_U) / ROW_UNITS;
    r[0] = PAD_U + 9.5f * unit + GAP_U * 0.5f + 0.004f;
    r[1] = key[1];
    r[2] = PAD_U + 10.0f * unit - GAP_U * 0.5f;
    r[3] = key[3];
}

int ui_kb_map_key_code(int map, int sheet, int i) {
    if ((unsigned)sheet < KB_STATE_COUNT && i >= NKEYS && i < NKEYS + KB_MEDIA_KEYS) {
        return kbMediaCode(i - NKEYS);   // the same under every sheet and key map
    }
    return (unsigned)sheet < KB_STATE_COUNT && i >= 0 && i < NKEYS ? SHEETS_BY_MAP[mapOf(map)][sheet][i].code : 0;
}

const char* ui_kb_map_key_label(int map, int sheet, int i) {
    if ((unsigned)sheet < KB_STATE_COUNT && i >= NKEYS && i < NKEYS + KB_MEDIA_KEYS) return MEDIA_LABEL[i - NKEYS];
    if ((unsigned)sheet >= KB_STATE_COUNT || i < 0 || i >= NKEYS) return NULL;
    return resolve(SHEETS_BY_MAP[mapOf(map)][sheet][i].label);
}

int ui_kb_key_code(int sheet, int i) { return ui_kb_map_key_code(KB_MAP_QWERTY, sheet, i); }

const char* ui_kb_key_label(int sheet, int i) { return ui_kb_map_key_label(KB_MAP_QWERTY, sheet, i); }

static void arrow(Dc* dc, const char* token, float cx, float cy, uint32_t ink) {
    float dx = 0, dy = 0;
    if (!strcmp(token, "@up")) dy = -1;
    else if (!strcmp(token, "@down")) dy = 1;
    else if (!strcmp(token, "@left")) dx = -1;
    else dx = 1;
    const float L = 13.0f, H = 9.0f;   // half length, half head
    float x0 = cx - dx * L, y0 = cy - dy * L, x1 = cx + dx * L, y1 = cy + dy * L;
    dc_line(dc, x0, y0, x1, y1, 3.5f, ink);
    float px = -dy, py = dx;   // perpendicular
    float head[6] = { x1 - dx * H + px * H, y1 - dy * H + py * H, x1, y1,
                      x1 - dx * H - px * H, y1 - dy * H - py * H };
    dc_polyline(dc, head, 3, 3.5f, ink, 0);
}

// A media key's shape, centred on (cx, cy): previous and next are a bar and a
// triangle, play/pause a triangle and two bars, the volume keys a speaker with
// a cross, a minus or a plus beside it
static void mediaIcon(Dc* dc, const char* token, float cx, float cy, uint32_t ink) {
    const float s = 12.0f;
    if (!strcmp(token, "@prev") || !strcmp(token, "@next")) {
        float d = !strcmp(token, "@next") ? 1.0f : -1.0f;
        float tri[6] = { cx - d * s, cy - s, cx - d * s, cy + s, cx + d * s * 0.5f, cy };
        dc_fill_poly(dc, tri, 3, ink);
        dc_line(dc, cx + d * s * 0.9f, cy - s, cx + d * s * 0.9f, cy + s, 3.5f, ink);
    } else if (!strcmp(token, "@play")) {
        float tri[6] = { cx - s * 1.3f, cy - s, cx - s * 1.3f, cy + s, cx - s * 0.1f, cy };
        dc_fill_poly(dc, tri, 3, ink);
        dc_line(dc, cx + s * 0.55f, cy - s, cx + s * 0.55f, cy + s, 4.0f, ink);
        dc_line(dc, cx + s * 1.25f, cy - s, cx + s * 1.25f, cy + s, 4.0f, ink);
    } else {
        float body[8] = { cx - s * 1.4f, cy - s * 0.4f, cx - s * 0.7f, cy - s * 0.4f,
                          cx - s * 0.7f, cy + s * 0.4f, cx - s * 1.4f, cy + s * 0.4f };
        dc_fill_poly(dc, body, 4, ink);
        float cone[8] = { cx - s * 0.7f, cy - s * 0.4f, cx, cy - s * 0.95f, cx, cy + s * 0.95f,
                          cx - s * 0.7f, cy + s * 0.4f };
        dc_fill_poly(dc, cone, 4, ink);
        float mx = cx + s * 1.0f;
        if (!strcmp(token, "@mute")) {
            dc_line(dc, mx - s * 0.5f, cy - s * 0.5f, mx + s * 0.5f, cy + s * 0.5f, 3.5f, ink);
            dc_line(dc, mx - s * 0.5f, cy + s * 0.5f, mx + s * 0.5f, cy - s * 0.5f, 3.5f, ink);
        } else {
            dc_line(dc, mx - s * 0.6f, cy, mx + s * 0.6f, cy, 3.5f, ink);
            if (!strcmp(token, "@volup")) dc_line(dc, mx, cy - s * 0.6f, mx, cy + s * 0.6f, 3.5f, ink);
        }
    }
}

static int isMedia(const char* token) {
    return token[0] == '@' && (!strcmp(token, "@prev") || !strcmp(token, "@play") || !strcmp(token, "@next")
                               || !strcmp(token, "@mute") || !strcmp(token, "@voldown") || !strcmp(token, "@volup"));
}

// What "Send keys" has so far, for the space bar: the modifiers lit, or the prompt
static void chordText(const UiView* v, char* out, size_t cap) {
    static const struct { int bit; UiStr name; } M[4] = {
        { KBF_LSHIFT, UIS_KEY_SHIFT }, { KBF_LCTRL, UIS_KEY_CTRL }, { KBF_LALT, UIS_KEY_ALT }, { KBF_LWIN, UIS_KEY_WIN } };
    out[0] = 0;
    for (int i = 0; i < 4; i++) {
        if (v->kb_chord_mods & M[i].bit) {
            strncat(out, ui_str(M[i].name), cap - strlen(out) - 1);
            strncat(out, "+", cap - strlen(out) - 1);
        }
    }
    if (!out[0]) snprintf(out, cap, "%s", ui_str(UIS_HOST_SEND_KEYS_SHORT));
}

// A lit pill at the left end of a key, for what is held or being typed
static void keyPill(Dc* dc, float x0, float y0, float y1, const char* text) {
    float tw = dc_text_width(dc, UI_WEIGHT_REGULAR, 20.0f, text) + 20.0f;
    float th = (y1 - y0) - 16.0f;
    dc_fill_rrect(dc, x0 + 8, y0 + 8, x0 + 8 + tw, y0 + 8 + th, 8.0f, 0xEEFFFFFFu);
    dc_text_left_mid(dc, UI_WEIGHT_REGULAR, 20.0f, x0 + 18, y0 + 8 + th * 0.5f, 0xFF141416u, text);
}

// ---- the Full keyboard ------------------------------------------------------------------

static const char* fullText(const char* l) {
    if (!strcmp(l, "@shift")) return ui_str(UIS_KEY_SHIFT);
    if (!strcmp(l, "@space")) return ui_str(UIS_KEY_SPACE);
    if (!strcmp(l, "@ctrl")) return ui_str(UIS_KEY_CTRL);
    if (!strcmp(l, "@alt")) return ui_str(UIS_KEY_ALT);
    if (!strcmp(l, "@win")) return ui_str(UIS_KEY_WIN);
    if (!strcmp(l, "@layout")) return ui_str(UIS_NATIVE_KEY_COMPACT);
    if (!strcmp(l, "Enter")) return ui_str(UIS_KEY_ENTER);
    return l;
}

static void panelFull(Dc* dc) {
    const UiView* v = dc->v;
    int map = mapOf(v->kb_map);
    int down = v->kb_oneshot | v->kb_locked;
    dc_sheet(dc, 32.0f, 0xF0141416u);
    for (int i = 0; i < kbfKeyCountFor(map); i++) {
        const KbfKey* k = kbfKey(i);
        float r[4];
        kbfKeyRectFor(map, i, r);
        float x0 = r[0] * dc->nw, y0 = r[1] * dc->nh, x1 = r[2] * dc->nw, y1 = r[3] * dc->nh;
        int bit = k->kind == KBF_MOD ? k->modBit : 0;
        int locked = bit && (v->kb_locked & bit);
        int lit = (bit && (down & bit)) || (k->kind == KBF_CAPS && v->kb_caps);
        int hot = dc_hover(dc, UI_HIT_KEY, i, -1);
        uint32_t fill = locked ? 0xEE9CC9FFu : lit ? 0xEEFFFFFFu : hot ? 0x44FFFFFFu : 0x28FFFFFFu;
        uint32_t line = locked ? 0xFFCFE4FFu : lit ? 0xFFFFFFFFu : hot ? 0x90FFFFFFu : 0x50FFFFFFu;
        dc_fill_rrect(dc, x0, y0, x1, y1, 8.0f, fill);
        dc_stroke_rrect(dc, x0, y0, x1, y1, 8.0f, 2.0f, line);
        uint32_t ink = lit ? 0xFF141416u : 0xFFFFFFFFu;
        const char* label = kbfLabelFor(map, i, down, v->kb_caps);
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        if (!strcmp(label, "@up") || !strcmp(label, "@down") || !strcmp(label, "@left")
                || !strcmp(label, "@right")) {
            arrow(dc, label, cx, cy, ink);
        } else if (isMedia(label)) {
            mediaIcon(dc, label, cx, cy, ink);
        } else {
            const char* text = fullText(label);
            float size = ui_utf8_count(text, -1) == 1 ? 30.0f : 20.0f;
            // smaller before it is cut: "PgDn" and "Pause" are a key wide
            while (size > 13.0f && dc_text_width(dc, UI_WEIGHT_REGULAR, size, text) > x1 - x0 - 8.0f) {
                size -= 1.0f;
            }
            dc_text_fit_center(dc, UI_WEIGHT_REGULAR, size, cx, cy, x1 - x0 - 8.0f, ink, text);
        }
        if (locked) {
            // locked: blue cap, an underline and a dot, where lit is white and plain
            dc_line(dc, x0 + 12.0f, y1 - 9.0f, x1 - 12.0f, y1 - 9.0f, 4.0f, ink);
            dc_fill_circle(dc, x1 - 10.0f, y0 + 10.0f, 4.0f, ink);
        } else if (k->kind == KBF_CAPS && v->kb_caps) {
            dc_fill_circle(dc, x1 - 10.0f, y0 + 10.0f, 5.0f, 0xFF2EB82Eu);
        }
        if (v->kb_chord && k->vk == 0x20) {
            char said[64];
            chordText(v, said, sizeof said);
            keyPill(dc, x0, y0, y1, said);
        }
        dc_hit_uv(dc, UI_HIT_KEY, i, kbfKeyCode(i), r[0], r[1], r[2], r[3], 1);
    }
}

void panel_keyboard(Dc* dc) {
    const UiView* v = dc->v;
    if (v->kb_layout == KB_LAYOUT_FULL) {
        panelFull(dc);
        return;
    }
    int sheet = v->kb_sheet < 0 || v->kb_sheet >= KB_STATE_COUNT ? KB_STATE_LOWER : v->kb_sheet;
    const Key* keys = SHEETS_BY_MAP[mapOf(v->kb_map)][sheet];
    int mods = v->kb_mods;

    dc_sheet(dc, 32.0f, 0xF0141416u);

    // Whether this sheet has the modifier keys, which then show what is held themselves
    int modifier_keys = 0;
    for (int i = 0; i < NKEYS; i++) modifier_keys |= kbModifierBit(keys[i].code) != 0;
    char held[48] = "";
    if (!modifier_keys) {
        static const int BITS[3] = { KB_MOD_CTRL, KB_MOD_ALT, KB_MOD_WIN };
        static const UiStr NAMES[3] = { UIS_KEY_CTRL, UIS_KEY_ALT, UIS_KEY_WIN };
        for (int i = 0; i < 3; i++) {
            if (mods & BITS[i]) {
                if (held[0]) strncat(held, " ", sizeof held - strlen(held) - 1);
                strncat(held, ui_str(NAMES[i]), sizeof held - strlen(held) - 1);
            }
        }
    }

    for (int i = 0; i < NKEYS; i++) {
        const char* label = resolve(keys[i].label);
        float r[4];
        ui_kb_key_rect(i, r);
        if (!label) continue;
        float x0 = r[0] * dc->nw, y0 = r[1] * dc->nh, x1 = r[2] * dc->nw, y1 = r[3] * dc->nh;
        int code = keys[i].code;
        int lit = (mods & kbModifierBit(code)) != 0;
        // Native (PLAN 4b.13): a modifier held down to lock is drawn as the
        // Full keyboard draws its locked ones, in blue with an underline
        int locked = (kbModifierBit(code) && (v->kb_locked & kbModifierBit(code)))
                  || (code == KB_CODE_SHIFT && (v->kb_locked & KB_STICKY_SHIFT));
        if (locked) lit = 1;
        int hot = dc_hover(dc, UI_HIT_KEY, i, -1);
        dc_fill_rrect(dc, x0, y0, x1, y1, 10.0f,
                      locked ? 0xEE9CC9FFu : lit ? 0xEEFFFFFFu : hot ? 0x44FFFFFFu : 0x28FFFFFFu);
        dc_stroke_rrect(dc, x0, y0, x1, y1, 10.0f, 2.0f,
                        locked ? 0xFFCFE4FFu : lit ? 0xFFFFFFFFu : hot ? 0x90FFFFFFu : 0x50FFFFFFu);
        uint32_t ink = lit ? 0xFF141416u : 0xFFFFFFFFu;
        if (label[0] == '@' && isMedia(label)) {
            mediaIcon(dc, label, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, ink);
        } else if (label[0] == '@') {
            arrow(dc, label, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, ink);
        } else {
            // a single character is what the key types, so it gets the room
            float size = ui_utf8_count(label, -1) == 1 ? 34.0f : 22.0f;
            dc_text_center(dc, UI_WEIGHT_REGULAR, size, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, ink, label);
        }
        if (locked) {
            dc_line(dc, x0 + 12.0f, y1 - 9.0f, x1 - 12.0f, y1 - 9.0f, 4.0f, ink);
            dc_fill_circle(dc, x1 - 10.0f, y0 + 10.0f, 4.0f, ink);
        }
        if (code == 32 && v->kb_chord) {
            // Send keys is waiting for a combo: what it has so far
            char said[64];
            chordText(v, said, sizeof said);
            keyPill(dc, x0, y0, y1, said);
        } else if (held[0] && code == 32) {
            // the held modifiers, lit at the left end of the space bar
            keyPill(dc, x0, y0, y1, held);
        }
        // a blank is no key; every other key is a hit
        dc_hit_uv(dc, UI_HIT_KEY, i, code, r[0], r[1], r[2], r[3], 1);
    }
    // the media keys row, the same under every sheet and key map
    for (int m = 0; m < KB_MEDIA_KEYS; m++) {
        float r[4];
        kbMediaRect(m, r);
        float x0 = r[0] * dc->nw, y0 = r[1] * dc->nh, x1 = r[2] * dc->nw, y1 = r[3] * dc->nh;
        int hot = dc_hover(dc, UI_HIT_KEY, NKEYS + m, -1);
        dc_fill_rrect(dc, x0, y0, x1, y1, 10.0f, hot ? 0x44FFFFFFu : 0x28FFFFFFu);
        dc_stroke_rrect(dc, x0, y0, x1, y1, 10.0f, 2.0f, hot ? 0x90FFFFFFu : 0x50FFFFFFu);
        mediaIcon(dc, MEDIA_LABEL[m], (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0xFFFFFFFFu);
        dc_hit_uv(dc, UI_HIT_KEY, NKEYS + m, kbMediaCode(m), r[0], r[1], r[2], r[3], 1);
    }
    if (v->kb_chip) {
        float r[4];
        ui_kb_layout_chip_rect(r);
        float x0 = r[0] * dc->nw, y0 = r[1] * dc->nh, x1 = r[2] * dc->nw, y1 = r[3] * dc->nh;
        int hot = dc_hover(dc, UI_HIT_BUTTON, UI_BTN_KB_LAYOUT, -1);
        dc_fill_rrect(dc, x0, y0, x1, y1, 10.0f, hot ? 0x44FFFFFFu : 0x14FFFFFFu);
        dc_stroke_rrect(dc, x0, y0, x1, y1, 10.0f, 2.0f, hot ? 0x90FFFFFFu : 0x40FFFFFFu);
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 20.0f, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f,
                           x1 - x0 - 6.0f, 0xCCFFFFFFu, ui_str(UIS_NATIVE_KEY_FULL));
        dc_hit_uv(dc, UI_HIT_BUTTON, UI_BTN_KB_LAYOUT, 0, r[0], r[1], r[2], r[3], 1);
    }
}
