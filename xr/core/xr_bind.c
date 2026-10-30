// Controller bindings: the defaults, bindings.ini and the resolved table. See
// xr_bind.h. GPLv3.
#include "xr_bind.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define B(s) XR_BIND_BIT(XR_BIND_SRC_##s)

// The shortcuts no binding may take (see xr_bind.h)
#define TOGGLE_MENU_GRIP (B(LEFT_VIEW) | B(LEFT_GRIP))
#define TOGGLE_STICKS (B(LEFT_STICK_CLICK) | B(RIGHT_STICK_CLICK))
#define TOGGLE_TRIGGERS_GRIPS (B(LEFT_TRIGGER) | B(RIGHT_TRIGGER) | B(LEFT_GRIP) | B(RIGHT_GRIP))
#define DISCONNECT_CHORD (B(LEFT_VIEW) | B(RIGHT_MENU) | B(LEFT_BUMPER) | B(RIGHT_BUMPER))
#define GRIPS (B(LEFT_GRIP) | B(RIGHT_GRIP))
#define WHOLE_STICKS (B(LEFT_STICK) | B(RIGHT_STICK))

// ---- names ---------------------------------------------------------------------------------

static const char* const SOURCE_NAMES[XR_BIND_SRC_COUNT] = {
    "left.trigger", "left.grip", "left.bumper", "left.stick", "left.stick_up",
    "left.stick_down", "left.stick_left", "left.stick_right", "left.stick_click", "left.view",
    "left.dpad_up", "left.dpad_down", "left.dpad_left", "left.dpad_right",
    "left.trigger_touch", "left.stick_touch",
    "right.trigger", "right.grip", "right.bumper", "right.stick", "right.stick_up",
    "right.stick_down", "right.stick_left", "right.stick_right", "right.stick_click",
    "right.a", "right.b", "right.x", "right.y", "right.menu",
    "right.trigger_touch", "right.stick_touch", "right.a_touch", "right.b_touch",
    "right.x_touch", "right.y_touch",
};

static const char* const KIND_NAMES[] = { "none", "key", "mouse", "wheel", "pad", "stick", "app" };
#define KIND_COUNT 7
static const char* const MOUSE_NAMES[] = { "left", "right", "middle", "x1", "x2" };
static const char* const WHEEL_NAMES[] = { "up", "down", "left", "right" };
static const char* const STICK_NAMES[] = { "scroll", "mouse", "wasd", "arrows", "dpad",
                                           "pad_left", "pad_right" };
static const char* const APP_NAMES[] = { "keyboard", "ray", "recentre", "cog", "3d", "gamepad",
                                         "bringhere", "host", "zoomin", "zoomout", "zoomreset" };
_Static_assert(sizeof(APP_NAMES) / sizeof(APP_NAMES[0]) == XR_BIND_APP_COUNT, "an app name a code");

typedef struct {
    const char* name;
    int code;
} NamedCode;

static const NamedCode PAD_NAMES[] = {
    { "a", PAD_A }, { "b", PAD_B }, { "x", PAD_X }, { "y", PAD_Y },
    { "lb", PAD_LB }, { "rb", PAD_RB }, { "lt", PAD_HELD_LT }, { "rt", PAD_HELD_RT },
    { "ls", PAD_LS_CLICK }, { "rs", PAD_RS_CLICK }, { "start", PAD_START }, { "back", PAD_BACK },
    { "guide", PAD_GUIDE }, { "dpad_up", PAD_UP }, { "dpad_down", PAD_DOWN },
    { "dpad_left", PAD_LEFT }, { "dpad_right", PAD_RIGHT },
};
#define PAD_NAME_COUNT ((int)(sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0])))

static const NamedCode MOD_NAMES[] = {
    { "ctrl", XR_BIND_MOD_CTRL }, { "control", XR_BIND_MOD_CTRL }, { "shift", XR_BIND_MOD_SHIFT },
    { "alt", XR_BIND_MOD_ALT }, { "win", XR_BIND_MOD_WIN }, { "meta", XR_BIND_MOD_WIN },
    { "super", XR_BIND_MOD_WIN },
};
#define MOD_NAME_COUNT ((int)(sizeof(MOD_NAMES) / sizeof(MOD_NAMES[0])))

// Windows virtual key codes by name, the first name of each code the one
// written. The same codes the evdev keyboard sends (stream/evdev_keyboard.c).
static const NamedCode KEY_NAMES[] = {
    { "A", 0x41 }, { "B", 0x42 }, { "C", 0x43 }, { "D", 0x44 }, { "E", 0x45 }, { "F", 0x46 },
    { "G", 0x47 }, { "H", 0x48 }, { "I", 0x49 }, { "J", 0x4A }, { "K", 0x4B }, { "L", 0x4C },
    { "M", 0x4D }, { "N", 0x4E }, { "O", 0x4F }, { "P", 0x50 }, { "Q", 0x51 }, { "R", 0x52 },
    { "S", 0x53 }, { "T", 0x54 }, { "U", 0x55 }, { "V", 0x56 }, { "W", 0x57 }, { "X", 0x58 },
    { "Y", 0x59 }, { "Z", 0x5A },
    { "0", 0x30 }, { "1", 0x31 }, { "2", 0x32 }, { "3", 0x33 }, { "4", 0x34 }, { "5", 0x35 },
    { "6", 0x36 }, { "7", 0x37 }, { "8", 0x38 }, { "9", 0x39 },
    { "F1", 0x70 }, { "F2", 0x71 }, { "F3", 0x72 }, { "F4", 0x73 }, { "F5", 0x74 },
    { "F6", 0x75 }, { "F7", 0x76 }, { "F8", 0x77 }, { "F9", 0x78 }, { "F10", 0x79 },
    { "F11", 0x7A }, { "F12", 0x7B }, { "F13", 0x7C }, { "F14", 0x7D }, { "F15", 0x7E },
    { "F16", 0x7F }, { "F17", 0x80 }, { "F18", 0x81 }, { "F19", 0x82 }, { "F20", 0x83 },
    { "F21", 0x84 }, { "F22", 0x85 }, { "F23", 0x86 }, { "F24", 0x87 },
    { "esc", 0x1B }, { "escape", 0x1B }, { "tab", 0x09 }, { "enter", 0x0D }, { "return", 0x0D },
    { "space", 0x20 }, { "backspace", 0x08 }, { "delete", 0x2E }, { "del", 0x2E },
    { "insert", 0x2D }, { "ins", 0x2D }, { "home", 0x24 }, { "end", 0x23 },
    { "pageup", 0x21 }, { "pgup", 0x21 }, { "pagedown", 0x22 }, { "pgdn", 0x22 },
    { "up", 0x26 }, { "down", 0x28 }, { "left", 0x25 }, { "right", 0x27 },
    { "shift", 0xA0 }, { "lshift", 0xA0 }, { "rshift", 0xA1 },
    { "ctrl", 0xA2 }, { "lctrl", 0xA2 }, { "rctrl", 0xA3 },
    { "alt", 0xA4 }, { "lalt", 0xA4 }, { "ralt", 0xA5 },
    { "win", 0x5B }, { "lwin", 0x5B }, { "rwin", 0x5C }, { "apps", 0x5D }, { "contextmenu", 0x5D },
    { "capslock", 0x14 }, { "numlock", 0x90 }, { "scrolllock", 0x91 },
    { "printscreen", 0x2C }, { "prtsc", 0x2C }, { "pause", 0x13 },
    { "minus", 0xBD }, { "equals", 0xBB }, { "lbracket", 0xDB }, { "rbracket", 0xDD },
    { "backslash", 0xDC }, { "semicolon", 0xBA }, { "quote", 0xDE }, { "apostrophe", 0xDE },
    { "grave", 0xC0 }, { "backtick", 0xC0 }, { "comma", 0xBC }, { "period", 0xBE },
    { "dot", 0xBE }, { "slash", 0xBF },
    { "num0", 0x60 }, { "num1", 0x61 }, { "num2", 0x62 }, { "num3", 0x63 }, { "num4", 0x64 },
    { "num5", 0x65 }, { "num6", 0x66 }, { "num7", 0x67 }, { "num8", 0x68 }, { "num9", 0x69 },
    { "multiply", 0x6A }, { "add", 0x6B }, { "subtract", 0x6D }, { "decimal", 0x6E },
    { "divide", 0x6F },
    { "volumeup", 0xAF }, { "volumedown", 0xAE }, { "mute", 0xAD },
    { "nexttrack", 0xB0 }, { "prevtrack", 0xB1 }, { "stop", 0xB2 }, { "playpause", 0xB3 },
    { "browserback", 0xA6 }, { "browserforward", 0xA7 },
};
#define KEY_NAME_COUNT ((int)(sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0])))

static int lowerChar(int c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

// Case blind: a's first n bytes against the whole of b
static int ieqn(const char* a, size_t n, const char* b) {
    size_t i = 0;
    for (; i < n; i++) {
        if (b[i] == '\0' || lowerChar((unsigned char)a[i]) != lowerChar((unsigned char)b[i])) {
            return 0;
        }
    }
    return b[i] == '\0';
}

static int ieq(const char* a, const char* b) {
    return ieqn(a, strlen(a), b);
}

static int isSpace(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static int findName(const char* const* names, int count, const char* s, size_t n) {
    for (int i = 0; i < count; i++) {
        if (ieqn(s, n, names[i])) {
            return i;
        }
    }
    return -1;
}

static int findCode(const NamedCode* table, int count, const char* s, size_t n) {
    for (int i = 0; i < count; i++) {
        if (ieqn(s, n, table[i].name)) {
            return i;
        }
    }
    return -1;
}

static int bitCount(uint64_t m) {
    int n = 0;
    for (; m; m &= m - 1) {
        n++;
    }
    return n;
}

const char* xrBindSourceName(int src) {
    return src >= 0 && src < XR_BIND_SRC_COUNT ? SOURCE_NAMES[src] : NULL;
}

int xrBindSourceFind(const char* name) {
    return name != NULL ? findName(SOURCE_NAMES, XR_BIND_SRC_COUNT, name, strlen(name)) : -1;
}

int xrBindSourceIsStick(int src) {
    return src == XR_BIND_SRC_LEFT_STICK || src == XR_BIND_SRC_RIGHT_STICK;
}

// A VK as 0x41 or vk65
static int parseVk(const char* s, size_t n) {
    int base;
    size_t i;
    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    }
    else if (n > 2 && lowerChar((unsigned char)s[0]) == 'v' && lowerChar((unsigned char)s[1]) == 'k') {
        base = 10;
        i = 2;
    }
    else {
        return 0;
    }
    int v = 0;
    for (; i < n; i++) {
        int c = lowerChar((unsigned char)s[i]);
        int d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        }
        else if (base == 16 && c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        }
        else {
            return 0;
        }
        v = v * base + d;
        if (v > 0xFF) {
            return 0;
        }
    }
    return v >= 1 && v <= 0xFE ? v : 0;
}

static int keyFindN(const char* s, size_t n) {
    int i = findCode(KEY_NAMES, KEY_NAME_COUNT, s, n);
    return i >= 0 ? KEY_NAMES[i].code : parseVk(s, n);
}

int xrBindKeyFind(const char* name) {
    return name != NULL ? keyFindN(name, strlen(name)) : 0;
}

const char* xrBindKeyName(int vk) {
    for (int i = 0; i < KEY_NAME_COUNT; i++) {
        if (KEY_NAMES[i].code == vk) {
            return KEY_NAMES[i].name;
        }
    }
    return NULL;
}

int xrBindHostButton(int mouseCode) {
    // Moonlight's BUTTON_LEFT 1, MIDDLE 2, RIGHT 3, X1 4, X2 5
    static const int MAP[XR_BIND_MOUSE_BUTTONS] = { 1, 3, 2, 4, 5 };
    return mouseCode >= 0 && mouseCode < XR_BIND_MOUSE_BUTTONS ? MAP[mouseCode] : 0;
}

// ---- actions -------------------------------------------------------------------------------------

static void setErr(char* err, size_t cap, int* col, int at, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

static void setErr(char* err, size_t cap, int* col, int at, const char* fmt, ...) {
    if (err != NULL && cap > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, cap, fmt, ap);
        va_end(ap);
    }
    if (col != NULL) {
        *col = at;
    }
}

static int parseKey(const char* v, size_t n, int base, XrBindAction* out, char* err, size_t cap,
                    int* col) {
    int mods = 0;
    size_t start = 0;
    for (;;) {
        size_t end = start;
        while (end < n && v[end] != '+') {
            end++;
        }
        size_t len = end - start;
        if (len == 0) {
            setErr(err, cap, col, base + (int)start, "an empty key name");
            return 0;
        }
        if (end == n) {
            // The last part is the key
            int vk = keyFindN(v + start, len);
            if (vk == 0) {
                setErr(err, cap, col, base + (int)start,
                       "unknown key '%.*s' (a name such as F, space, esc, f5 or pagedown, or a "
                       "VK as 0x41)", (int)len, v + start);
                return 0;
            }
            out->kind = XR_BIND_ACT_KEY;
            out->code = vk;
            out->mods = mods;
            return 1;
        }
        int m = findCode(MOD_NAMES, MOD_NAME_COUNT, v + start, len);
        if (m < 0) {
            setErr(err, cap, col, base + (int)start,
                   "'%.*s' is not a modifier (ctrl, shift, alt or win); the key comes last",
                   (int)len, v + start);
            return 0;
        }
        mods |= MOD_NAMES[m].code;
        start = end + 1;
    }
}

int xrBindActionParse(const char* text, XrBindAction* out, char* err, size_t cap, int* col) {
    XrBindAction a = { XR_BIND_ACT_NONE, 0, 0 };
    if (err != NULL && cap > 0) {
        err[0] = '\0';
    }
    if (col != NULL) {
        *col = 0;
    }
    size_t n = strlen(text);
    while (n > 0 && isSpace((unsigned char)text[n - 1])) {
        n--;
    }
    size_t s = 0;
    while (s < n && isSpace((unsigned char)text[s])) {
        s++;
    }
    const char* t = text + s;
    n -= s;
    if (ieqn(t, n, "none")) {
        *out = a;
        return 1;
    }
    const char* colon = memchr(t, ':', n);
    if (colon == NULL) {
        setErr(err, cap, col, (int)s,
               "'%.*s' is not an action (key:, mouse:, wheel:, pad:, stick:, app: or none)",
               (int)n, t);
        return 0;
    }
    size_t kn = (size_t)(colon - t);
    int kind = findName(KIND_NAMES, KIND_COUNT, t, kn);
    if (kind <= XR_BIND_ACT_NONE) {
        setErr(err, cap, col, (int)s,
               "unknown action kind '%.*s' (key, mouse, wheel, pad, stick, app or none)",
               (int)kn, t);
        return 0;
    }
    const char* v = colon + 1;
    size_t vn = n - kn - 1;
    int vcol = (int)(s + kn + 1);
    if (vn == 0) {
        setErr(err, cap, col, vcol, "nothing after '%s:'", KIND_NAMES[kind]);
        return 0;
    }
    int i;
    switch (kind) {
        case XR_BIND_ACT_KEY:
            if (!parseKey(v, vn, vcol, &a, err, cap, col)) {
                return 0;
            }
            break;
        case XR_BIND_ACT_MOUSE:
            i = findName(MOUSE_NAMES, XR_BIND_MOUSE_BUTTONS, v, vn);
            if (i < 0) {
                setErr(err, cap, col, vcol,
                       "unknown mouse button '%.*s' (left, right, middle, x1 or x2)", (int)vn, v);
                return 0;
            }
            a.kind = kind;
            a.code = i;
            break;
        case XR_BIND_ACT_WHEEL:
            i = findName(WHEEL_NAMES, 4, v, vn);
            if (i < 0) {
                setErr(err, cap, col, vcol, "unknown wheel direction '%.*s' (up, down, left or right)",
                       (int)vn, v);
                return 0;
            }
            a.kind = kind;
            a.code = i;
            break;
        case XR_BIND_ACT_PAD:
            i = findCode(PAD_NAMES, PAD_NAME_COUNT, v, vn);
            if (i < 0) {
                setErr(err, cap, col, vcol,
                       "unknown pad button '%.*s' (a, b, x, y, lb, rb, lt, rt, ls, rs, start, back, "
                       "guide, dpad_up, dpad_down, dpad_left or dpad_right)", (int)vn, v);
                return 0;
            }
            a.kind = kind;
            a.code = PAD_NAMES[i].code;
            break;
        case XR_BIND_ACT_STICK:
            i = findName(STICK_NAMES, 7, v, vn);
            if (i < 0) {
                setErr(err, cap, col, vcol,
                       "unknown stick use '%.*s' (scroll, mouse, wasd, arrows, dpad, pad_left or "
                       "pad_right)", (int)vn, v);
                return 0;
            }
            a.kind = kind;
            a.code = i;
            break;
        default:
            if (ieqn(v, vn, "none")) {
                break;
            }
            i = findName(APP_NAMES, XR_BIND_APP_COUNT, v, vn);
            if (i < 0) {
                setErr(err, cap, col, vcol,
                       "unknown app command '%.*s' (keyboard, ray, recentre, cog, 3d, gamepad, "
                       "bringhere, host, zoomin, zoomout, zoomreset or none)", (int)vn, v);
                return 0;
            }
            a.kind = kind;
            a.code = i;
            break;
    }
    *out = a;
    return 1;
}

int xrBindActionFormat(const XrBindAction* a, char* out, size_t cap) {
    switch (a->kind) {
        case XR_BIND_ACT_KEY: {
            char mods[32];
            snprintf(mods, sizeof(mods), "%s%s%s%s", (a->mods & XR_BIND_MOD_CTRL) ? "ctrl+" : "",
                     (a->mods & XR_BIND_MOD_SHIFT) ? "shift+" : "",
                     (a->mods & XR_BIND_MOD_ALT) ? "alt+" : "",
                     (a->mods & XR_BIND_MOD_WIN) ? "win+" : "");
            const char* name = xrBindKeyName(a->code);
            if (name != NULL) {
                return snprintf(out, cap, "key:%s%s", mods, name);
            }
            return snprintf(out, cap, "key:%s0x%02X", mods, (unsigned)a->code & 0xFFu);
        }
        case XR_BIND_ACT_MOUSE:
            if (a->code >= 0 && a->code < XR_BIND_MOUSE_BUTTONS) {
                return snprintf(out, cap, "mouse:%s", MOUSE_NAMES[a->code]);
            }
            break;
        case XR_BIND_ACT_WHEEL:
            if (a->code >= 0 && a->code < 4) {
                return snprintf(out, cap, "wheel:%s", WHEEL_NAMES[a->code]);
            }
            break;
        case XR_BIND_ACT_PAD:
            for (int i = 0; i < PAD_NAME_COUNT; i++) {
                if (PAD_NAMES[i].code == a->code) {
                    return snprintf(out, cap, "pad:%s", PAD_NAMES[i].name);
                }
            }
            break;
        case XR_BIND_ACT_STICK:
            if (a->code >= 0 && a->code < 7) {
                return snprintf(out, cap, "stick:%s", STICK_NAMES[a->code]);
            }
            break;
        case XR_BIND_ACT_APP:
            if (a->code >= 0 && a->code < XR_BIND_APP_COUNT) {
                return snprintf(out, cap, "app:%s", APP_NAMES[a->code]);
            }
            break;
        default:
            break;
    }
    return snprintf(out, cap, "none");
}

int xrBindActionEqual(const XrBindAction* a, const XrBindAction* b) {
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == XR_BIND_ACT_NONE) {
        return 1;
    }
    return a->code == b->code && (a->kind != XR_BIND_ACT_KEY || a->mods == b->mods);
}

// ---- the defaults ----------------------------------------------------------------------------------

typedef struct {
    int mode;
    uint64_t sources;
    int kind;
    int code;
    const char* note;
} DefaultBind;

#define MODE_P XR_BIND_MODE_POINTER
#define MODE_G XR_BIND_MODE_GAMEPAD
#define GRAB_NOTE "over our UI it grabs, whatever it is bound to"

// Today's fixed maps (USER_GUIDE.md "Controllers", xr_pressfix.c
// pointerButtonMask, xr_gamepad.c padMapGrips) and the seven controls pointer
// mode left idle: X, Y, d-pad up and left, both bumpers and the right menu
static const DefaultBind DEFAULTS[] = {
    { MODE_P, B(LEFT_TRIGGER), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_LEFT, "left click, hold to drag" },
    { MODE_P, B(LEFT_GRIP), XR_BIND_ACT_NONE, 0, GRAB_NOTE },
    { MODE_P, B(LEFT_BUMPER), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_X1, "back" },
    { MODE_P, B(LEFT_STICK), XR_BIND_ACT_STICK, XR_BIND_STICK_SCROLL, "scroll" },
    { MODE_P, B(LEFT_STICK_CLICK), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_RIGHT, "right click" },
    { MODE_P, B(LEFT_VIEW), XR_BIND_ACT_NONE, 0, "held with the left grip: pointer/gamepad switch" },
    { MODE_P, B(LEFT_DPAD_UP), XR_BIND_ACT_APP, XR_BIND_APP_RECENTRE,
      "the screen comes round to where you face" },
    { MODE_P, B(LEFT_DPAD_DOWN), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_RIGHT, "right click" },
    { MODE_P, B(LEFT_DPAD_LEFT), XR_BIND_ACT_NONE, 0, "free" },
    { MODE_P, B(LEFT_DPAD_RIGHT), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_MIDDLE, "middle click" },
    { MODE_P, B(RIGHT_TRIGGER), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_LEFT, "left click, hold to drag" },
    { MODE_P, B(RIGHT_GRIP), XR_BIND_ACT_NONE, 0, GRAB_NOTE },
    { MODE_P, B(RIGHT_BUMPER), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_X2, "forward" },
    { MODE_P, B(RIGHT_STICK), XR_BIND_ACT_STICK, XR_BIND_STICK_SCROLL, "scroll" },
    { MODE_P, B(RIGHT_STICK_CLICK), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_RIGHT, "right click" },
    { MODE_P, B(RIGHT_A), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_RIGHT, "right click" },
    { MODE_P, B(RIGHT_B), XR_BIND_ACT_MOUSE, XR_BIND_MOUSE_MIDDLE, "middle click" },
    { MODE_P, B(RIGHT_X), XR_BIND_ACT_APP, XR_BIND_APP_3D, "3D on or off" },
    { MODE_P, B(RIGHT_Y), XR_BIND_ACT_APP, XR_BIND_APP_KEYBOARD, "show or hide the keyboard" },
    { MODE_P, B(RIGHT_MENU), XR_BIND_ACT_APP, XR_BIND_APP_COG, "open or close the cog panel" },

    { MODE_G, B(LEFT_TRIGGER), XR_BIND_ACT_PAD, PAD_HELD_LT, "LT" },
    { MODE_G, B(LEFT_GRIP), XR_BIND_ACT_PAD, PAD_LB, "LB; config.ini's pad_grip while left alone" },
    { MODE_G, B(LEFT_BUMPER), XR_BIND_ACT_PAD, PAD_LB, "LB" },
    { MODE_G, B(LEFT_STICK), XR_BIND_ACT_STICK, XR_BIND_STICK_PAD_LEFT, "left stick" },
    { MODE_G, B(LEFT_STICK_CLICK), XR_BIND_ACT_PAD, PAD_LS_CLICK, "LS" },
    { MODE_G, B(LEFT_VIEW), XR_BIND_ACT_PAD, PAD_BACK, "Back" },
    { MODE_G, B(LEFT_DPAD_UP), XR_BIND_ACT_PAD, PAD_UP, "d-pad" },
    { MODE_G, B(LEFT_DPAD_DOWN), XR_BIND_ACT_PAD, PAD_DOWN, "d-pad" },
    { MODE_G, B(LEFT_DPAD_LEFT), XR_BIND_ACT_PAD, PAD_LEFT, "d-pad" },
    { MODE_G, B(LEFT_DPAD_RIGHT), XR_BIND_ACT_PAD, PAD_RIGHT, "d-pad" },
    { MODE_G, B(RIGHT_TRIGGER), XR_BIND_ACT_PAD, PAD_HELD_RT, "RT" },
    { MODE_G, B(RIGHT_GRIP), XR_BIND_ACT_PAD, PAD_RB, "RB; config.ini's pad_grip while left alone" },
    { MODE_G, B(RIGHT_BUMPER), XR_BIND_ACT_PAD, PAD_RB, "RB" },
    { MODE_G, B(RIGHT_STICK), XR_BIND_ACT_STICK, XR_BIND_STICK_PAD_RIGHT, "right stick" },
    { MODE_G, B(RIGHT_STICK_CLICK), XR_BIND_ACT_PAD, PAD_RS_CLICK, "RS" },
    { MODE_G, B(RIGHT_A), XR_BIND_ACT_PAD, PAD_A, "A" },
    { MODE_G, B(RIGHT_B), XR_BIND_ACT_PAD, PAD_B, "B" },
    { MODE_G, B(RIGHT_X), XR_BIND_ACT_PAD, PAD_X, "X" },
    { MODE_G, B(RIGHT_Y), XR_BIND_ACT_PAD, PAD_Y, "Y" },
    { MODE_G, B(RIGHT_MENU), XR_BIND_ACT_PAD, PAD_START, "Start" },
    { MODE_G, B(LEFT_VIEW) | B(RIGHT_MENU), XR_BIND_ACT_PAD, PAD_GUIDE,
      "held a quarter second; gives way to the disconnect chord" },
};
#define DEFAULT_COUNT ((int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0])))

// ---- text out ------------------------------------------------------------------------------------

typedef struct {
    char* out;
    size_t cap;
    size_t len;
} Out;

static void put(Out* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(Out* o, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t room = o->len < o->cap ? o->cap - o->len : 0;
    int n = vsnprintf(room > 0 ? o->out + o->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0) {
        o->len += (size_t)n;
    }
}

static void outInit(Out* o, char* out, size_t cap) {
    o->out = out;
    o->cap = out != NULL ? cap : 0;
    o->len = 0;
    if (o->cap > 0) {
        out[0] = '\0';
    }
}

// The controls of a set joined with +, in the enum's order
static void sourcesText(uint64_t sources, char* out, size_t cap) {
    Out o;
    outInit(&o, out, cap);
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (sources & XR_BIND_BIT(i)) {
            put(&o, "%s%s", o.len > 0 ? "+" : "", SOURCE_NAMES[i]);
        }
    }
}

static const DefaultBind* defaultFor(int mode, uint64_t sources) {
    for (int i = 0; i < DEFAULT_COUNT; i++) {
        if (DEFAULTS[i].mode == mode && DEFAULTS[i].sources == sources) {
            return &DEFAULTS[i];
        }
    }
    return NULL;
}

static void defaultLine(Out* o, int mode, uint64_t sources, const DefaultBind* d) {
    XrBindAction a = { XR_BIND_ACT_NONE, 0, 0 };
    if (d != NULL) {
        a.kind = d->kind;
        a.code = d->code;
    }
    char lhs[160], act[64], line[240];
    sourcesText(sources, lhs, sizeof(lhs));
    xrBindActionFormat(&a, act, sizeof(act));
    snprintf(line, sizeof(line), "%s = %s", lhs, act);
    if (d != NULL && d->note != NULL) {
        put(o, "#%-40s ; %s\n", line, d->note);
    }
    else {
        put(o, "#%s\n", line);
    }
}

int xrBindDefaultText(char* out, size_t cap) {
    Out o;
    outInit(&o, out, cap);
    put(&o,
        "# Moonlight XR controller bindings: what each Frame control does.\n"
        "#\n"
        "# Every line below is a default, commented out, so this file changes nothing\n"
        "# until a line is uncommented (take the # off) and edited. A line is\n"
        "#   control = action\n"
        "# or a chord of two to four controls held together:\n"
        "#   left.view+right.menu = pad:guide\n"
        "#\n"
        "# Actions:\n"
        "#   key:F  key:space  key:ctrl+shift+esc  key:0x41   a keyboard key, modifiers first\n"
        "#   mouse:left|right|middle|x1|x2                    a mouse button (x1 back, x2 forward)\n"
        "#   wheel:up|down|left|right                         one wheel notch per press\n"
        "#   pad:a|b|x|y|lb|rb|lt|rt|ls|rs|start|back|guide|dpad_up|dpad_down|dpad_left|dpad_right\n"
        "#                                                    a gamepad button (gamepad mode only)\n"
        "#   stick:scroll|mouse|wasd|arrows|dpad|pad_left|pad_right\n"
        "#                                                    a whole stick (left.stick, right.stick)\n"
        "#   app:keyboard|ray|recentre|cog|3d|gamepad|bringhere|host|zoomin|zoomout|zoomreset\n"
        "#   none                                             nothing\n"
        "#\n"
        "# [pointer] is pointer mode and [gamepad] gamepad mode, for every app. An\n"
        "# [app \"Name\"] section is for one host app (its name as the app list shows\n"
        "# it) and goes over those two; its lines say the mode first:\n"
        "#   [app \"Time Crisis 2\"]\n"
        "#   pointer.right.x = mouse:right\n"
        "#   gamepad.left.grip = none\n"
        "#\n"
        "# Never bindable: the pointer/gamepad switch chords (left.view+left.grip,\n"
        "# left.stick_click+right.stick_click, both triggers with both grips), the\n"
        "# disconnect chord (left.view+right.menu+left.bumper+right.bumper), a chord\n"
        "# with a grip in pointer mode, and the grips' grab while the ray is on our UI.\n"
        "# A line that is not right is left out and moonlight.log says which and why.\n");
    for (int mode = 0; mode < XR_BIND_MODES; mode++) {
        put(&o, "\n[%s]\n", mode == XR_BIND_MODE_POINTER ? "pointer" : "gamepad");
        for (int s = 0; s < XR_BIND_SRC_COUNT; s++) {
            defaultLine(&o, mode, XR_BIND_BIT(s), defaultFor(mode, XR_BIND_BIT(s)));
        }
        for (int i = 0; i < DEFAULT_COUNT; i++) {
            if (DEFAULTS[i].mode == mode && bitCount(DEFAULTS[i].sources) > 1) {
                defaultLine(&o, mode, DEFAULTS[i].sources, &DEFAULTS[i]);
            }
        }
    }
    return (int)o.len;
}

static void ruleLine(Out* o, const XrBindRule* r) {
    char lhs[160], act[64];
    sourcesText(r->sources, lhs, sizeof(lhs));
    xrBindActionFormat(&r->action, act, sizeof(act));
    if (r->app >= 0) {
        put(o, "%s.%s = %s\n", r->mode == XR_BIND_MODE_POINTER ? "pointer" : "gamepad", lhs, act);
    }
    else {
        put(o, "%s = %s\n", lhs, act);
    }
}

int xrBindSerialize(const XrBindSet* s, char* out, size_t cap) {
    Out o;
    outInit(&o, out, cap);
    put(&o, "# Moonlight XR controller bindings (docs/dev/bindings.md has the format)\n");
    for (int mode = 0; mode < XR_BIND_MODES; mode++) {
        put(&o, "\n[%s]\n", mode == XR_BIND_MODE_POINTER ? "pointer" : "gamepad");
        for (int i = 0; i < s->ruleCount; i++) {
            if (s->rules[i].app < 0 && s->rules[i].mode == mode) {
                ruleLine(&o, &s->rules[i]);
            }
        }
    }
    for (int a = 0; a < s->appCount; a++) {
        put(&o, "\n[app \"%s\"]\n", s->apps[a]);
        for (int i = 0; i < s->ruleCount; i++) {
            if (s->rules[i].app == a) {
                ruleLine(&o, &s->rules[i]);
            }
        }
    }
    return (int)o.len;
}

// ---- reading --------------------------------------------------------------------------------------

static void addError(XrBindErrors* e, int line, int col, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void addError(XrBindErrors* e, int line, int col, const char* fmt, ...) {
    if (e == NULL) {
        return;
    }
    if (e->count < XR_BIND_MAX_ERRORS) {
        XrBindError* x = &e->errors[e->count];
        x->line = line;
        x->column = col;
        // Room left for the "line:column: " in front
        char what[XR_BIND_ERROR_MAX - 32];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(what, sizeof(what), fmt, ap);
        va_end(ap);
        if (line > 0) {
            snprintf(x->text, sizeof(x->text), "%d:%d: %s", line, col, what);
        }
        else {
            snprintf(x->text, sizeof(x->text), "%s", what);
        }
    }
    e->count++;
}

void xrBindSetInit(XrBindSet* s) {
    memset(s, 0, sizeof(*s));
}

#define SECTION_NONE 0
#define SECTION_OK 1
#define SECTION_SKIP 2

typedef struct {
    XrBindSet* set;
    XrBindErrors* errs;
    int line;
    int section;
    int app;
    int mode;
} Reader;

static char* skipSpace(char* p) {
    while (isSpace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static void trimEnd(char* start, char* end) {
    while (end > start && isSpace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
}

static int appIndex(XrBindSet* s, const char* name) {
    for (int i = 0; i < s->appCount; i++) {
        if (ieq(s->apps[i], name)) {
            return i;
        }
    }
    return -1;
}

static void readSection(Reader* r, char* buf, char* p) {
    char* q = skipSpace(p + 1);
    char* w = q;
    while ((*w >= 'a' && *w <= 'z') || (*w >= 'A' && *w <= 'Z')) {
        w++;
    }
    size_t wn = (size_t)(w - q);
    int col = (int)(q - buf) + 1;
    r->section = SECTION_SKIP;
    char* close = NULL;
    if (ieqn(q, wn, "pointer") || ieqn(q, wn, "gamepad")) {
        int mode = ieqn(q, wn, "pointer") ? XR_BIND_MODE_POINTER : XR_BIND_MODE_GAMEPAD;
        close = skipSpace(w);
        if (*close != ']') {
            addError(r->errs, r->line, (int)(close - buf) + 1, "expected ']'");
            return;
        }
        r->section = SECTION_OK;
        r->app = -1;
        r->mode = mode;
    }
    else if (ieqn(q, wn, "app")) {
        char* quote = skipSpace(w);
        if (*quote != '"') {
            addError(r->errs, r->line, (int)(quote - buf) + 1,
                     "expected the app's name in quotes: [app \"Name\"]");
            return;
        }
        char* name = quote + 1;
        char* endq = strchr(name, '"');
        if (endq == NULL) {
            addError(r->errs, r->line, (int)(quote - buf) + 1, "the app's name has no closing quote");
            return;
        }
        size_t nn = (size_t)(endq - name);
        if (nn == 0) {
            addError(r->errs, r->line, (int)(name - buf) + 1, "an empty app name");
            return;
        }
        if (nn >= XR_BIND_APP_MAX) {
            addError(r->errs, r->line, (int)(name - buf) + 1,
                     "an app name longer than %d characters", XR_BIND_APP_MAX - 1);
            return;
        }
        close = skipSpace(endq + 1);
        if (*close != ']') {
            addError(r->errs, r->line, (int)(close - buf) + 1, "expected ']'");
            return;
        }
        *endq = '\0';
        int idx = appIndex(r->set, name);
        if (idx < 0) {
            if (r->set->appCount >= XR_BIND_MAX_APPS) {
                addError(r->errs, r->line, (int)(name - buf) + 1, "too many apps (at most %d)",
                         XR_BIND_MAX_APPS);
                return;
            }
            idx = r->set->appCount++;
            snprintf(r->set->apps[idx], XR_BIND_APP_MAX, "%s", name);
        }
        r->section = SECTION_OK;
        r->app = idx;
        r->mode = -1;
    }
    else {
        addError(r->errs, r->line, col,
                 "unknown section '%.*s' ([pointer], [gamepad] or [app \"Name\"])", (int)wn, q);
        return;
    }
    char* rest = skipSpace(close + 1);
    if (*rest != '\0' && *rest != '#' && *rest != ';') {
        addError(r->errs, r->line, (int)(rest - buf) + 1, "text after the section's ']'");
    }
}

// Why a set of controls may not be bound in a mode, or NULL
static const char* reservedWhy(uint64_t sources, int mode) {
    if (bitCount(sources) < 2) {
        return NULL;
    }
    if (sources == TOGGLE_MENU_GRIP || sources == TOGGLE_STICKS || sources == TOGGLE_TRIGGERS_GRIPS) {
        return "is a pointer/gamepad switch shortcut and cannot be bound";
    }
    if ((sources & DISCONNECT_CHORD) == DISCONNECT_CHORD) {
        return "holds the disconnect chord (left.view+right.menu+left.bumper+right.bumper), "
               "which cannot be bound";
    }
    if (mode == XR_BIND_MODE_POINTER && (sources & GRIPS) != 0) {
        return "has a grip in it, which grabs over the UI in pointer mode, so it cannot be bound "
               "there";
    }
    return NULL;
}

static void readBinding(Reader* r, char* buf, char* p) {
    // An inline comment: nothing a binding says has a # or a ;
    char* cut = strpbrk(p, "#;");
    if (cut != NULL) {
        *cut = '\0';
    }
    char* eq = strchr(p, '=');
    if (eq == NULL) {
        addError(r->errs, r->line, (int)(p - buf) + 1, "expected 'control = action'");
        return;
    }
    *eq = '\0';
    trimEnd(p, eq);
    char* rhs = skipSpace(eq + 1);
    trimEnd(rhs, rhs + strlen(rhs));
    if (*p == '\0') {
        addError(r->errs, r->line, (int)(p - buf) + 1, "no control before '='");
        return;
    }
    if (*rhs == '\0') {
        addError(r->errs, r->line, (int)(eq - buf) + 2, "no action after '='");
        return;
    }

    int mode = r->mode;
    char* lhs = p;
    // The first 8 bytes against the whole prefix; a shorter name differs at
    // its end before anything past it is read
    int hasPointer = ieqn(lhs, 8, "pointer.");
    int hasGamepad = ieqn(lhs, 8, "gamepad.");
    if (r->app >= 0) {
        if (!hasPointer && !hasGamepad) {
            addError(r->errs, r->line, (int)(lhs - buf) + 1,
                     "in an [app] section a line starts pointer. or gamepad. (pointer.right.x = ...)");
            return;
        }
        mode = hasPointer ? XR_BIND_MODE_POINTER : XR_BIND_MODE_GAMEPAD;
        lhs += 8;
    }
    else if (hasPointer || hasGamepad) {
        addError(r->errs, r->line, (int)(lhs - buf) + 1,
                 "pointer. and gamepad. are for [app] sections; [%s] is one mode already",
                 r->mode == XR_BIND_MODE_POINTER ? "pointer" : "gamepad");
        return;
    }

    // The controls, + between them
    uint64_t sources = 0;
    int parts = 0;
    char* part = lhs;
    for (;;) {
        char* plus = strchr(part, '+');
        char* end = plus != NULL ? plus : part + strlen(part);
        char* s = skipSpace(part);
        char* e = end;
        while (e > s && isSpace((unsigned char)e[-1])) {
            e--;
        }
        size_t n = (size_t)(e - s);
        int col = (int)(s - buf) + 1;
        if (n == 0) {
            addError(r->errs, r->line, col, "an empty control name");
            return;
        }
        int src = findName(SOURCE_NAMES, XR_BIND_SRC_COUNT, s, n);
        if (src < 0) {
            if (ieqn(s, n, "left.system") || ieqn(s, n, "right.system")) {
                addError(r->errs, r->line, col, "the system button is SteamVR's and cannot be bound");
            }
            else {
                addError(r->errs, r->line, col,
                         "unknown control '%.*s' (docs/dev/bindings.md lists them)", (int)n, s);
            }
            return;
        }
        if (sources & XR_BIND_BIT(src)) {
            addError(r->errs, r->line, col, "%s twice in one chord", SOURCE_NAMES[src]);
            return;
        }
        if (xrBindSourceIsStick(src) && (plus != NULL || parts > 0)) {
            addError(r->errs, r->line, col,
                     "a whole stick cannot be part of a chord; use its directions (%s_up ...)",
                     SOURCE_NAMES[src]);
            return;
        }
        sources |= XR_BIND_BIT(src);
        parts++;
        if (parts > XR_BIND_CHORD_MAX_PARTS) {
            addError(r->errs, r->line, col, "a chord is at most %d controls",
                     XR_BIND_CHORD_MAX_PARTS);
            return;
        }
        if (plus == NULL) {
            break;
        }
        part = plus + 1;
    }

    // The action
    XrBindAction act;
    char why[XR_BIND_ERROR_MAX];
    int at = 0;
    if (!xrBindActionParse(rhs, &act, why, sizeof(why), &at)) {
        addError(r->errs, r->line, (int)(rhs - buf) + 1 + at, "%s", why);
        return;
    }
    int rcol = (int)(rhs - buf) + 1;
    int lcol = (int)(lhs - buf) + 1;
    char names[160];
    sourcesText(sources, names, sizeof(names));
    int stick = (sources & WHOLE_STICKS) != 0;
    if (stick && act.kind != XR_BIND_ACT_STICK && act.kind != XR_BIND_ACT_NONE) {
        addError(r->errs, r->line, rcol,
                 "%s takes a stick: action or none; bind %s_up and the other directions for "
                 "buttons", names, names);
        return;
    }
    if (!stick && act.kind == XR_BIND_ACT_STICK) {
        addError(r->errs, r->line, rcol, "stick: actions are for left.stick and right.stick");
        return;
    }
    if (mode == XR_BIND_MODE_POINTER
            && (act.kind == XR_BIND_ACT_PAD
                || (act.kind == XR_BIND_ACT_STICK
                    && (act.code == XR_BIND_STICK_PAD_LEFT || act.code == XR_BIND_STICK_PAD_RIGHT)))) {
        addError(r->errs, r->line, rcol, "the pad only exists in gamepad mode");
        return;
    }
    const char* reserved = reservedWhy(sources, mode);
    if (reserved != NULL) {
        addError(r->errs, r->line, lcol, "%s %s", names, reserved);
        return;
    }

    XrBindSet* set = r->set;
    int chords = 0;
    for (int i = 0; i < set->ruleCount; i++) {
        const XrBindRule* o = &set->rules[i];
        if (o->app != r->app || o->mode != mode) {
            continue;
        }
        if (o->sources == sources) {
            addError(r->errs, r->line, lcol, "%s is already bound on line %d", names, o->line);
            return;
        }
        chords += bitCount(o->sources) > 1;
    }
    if (parts > 1 && chords >= XR_BIND_MAX_SECTION_CHORDS) {
        addError(r->errs, r->line, lcol, "too many chords in one section and mode (at most %d)",
                 XR_BIND_MAX_SECTION_CHORDS);
        return;
    }
    if (set->ruleCount >= XR_BIND_MAX_RULES) {
        addError(r->errs, r->line, lcol, "too many bindings (at most %d)", XR_BIND_MAX_RULES);
        return;
    }
    XrBindRule* rule = &set->rules[set->ruleCount++];
    rule->app = r->app;
    rule->mode = mode;
    rule->sources = sources;
    rule->action = act;
    rule->line = r->line;
}

int xrBindParse(XrBindSet* s, const char* text, XrBindErrors* errs) {
    xrBindSetInit(s);
    if (errs != NULL) {
        memset(errs, 0, sizeof(*errs));
    }
    XrBindErrors local;
    memset(&local, 0, sizeof(local));
    Reader r;
    memset(&r, 0, sizeof(r));
    r.set = s;
    r.errs = errs != NULL ? errs : &local;
    r.section = SECTION_NONE;
    r.app = -1;
    if (text == NULL) {
        return 0;
    }
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
    }
    const char* p = text;
    while (*p != '\0') {
        r.line++;
        const char* eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        const char* next = eol != NULL ? eol + 1 : p + n;
        char buf[XR_BIND_LINE_MAX + 1];
        if (n > XR_BIND_LINE_MAX) {
            addError(r.errs, r.line, XR_BIND_LINE_MAX + 1, "a line longer than %d characters",
                     XR_BIND_LINE_MAX);
            p = next;
            continue;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        p = next;
        if (n > 0 && buf[n - 1] == '\r') {
            buf[n - 1] = '\0';
        }
        char* l = skipSpace(buf);
        if (*l == '\0' || *l == '#' || *l == ';') {
            continue;
        }
        if (*l == '[') {
            readSection(&r, buf, l);
            continue;
        }
        if (r.section == SECTION_SKIP) {
            // Under a section already reported
            continue;
        }
        if (r.section == SECTION_NONE) {
            addError(r.errs, r.line, (int)(l - buf) + 1,
                     "a binding before any section ([pointer], [gamepad] or [app \"Name\"])");
            continue;
        }
        readBinding(&r, buf, l);
    }
    return r.errs->count;
}

// ---- files ---------------------------------------------------------------------------------------

const char* xrBindDefaultPath(char* buf, size_t cap) {
    const char* dir = getenv("MLXR_CONFIG_DIR");
    if (dir != NULL && *dir != '\0') {
        snprintf(buf, cap, "%s/bindings.ini", dir);
        return buf;
    }
    const char* home = getenv("HOME");
    snprintf(buf, cap, "%s/.config/moonlight-xr/bindings.ini",
             home != NULL && *home != '\0' ? home : ".");
    return buf;
}

// Every folder above the file
static int makeParents(const char* path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* c = tmp + 1; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *c = '/';
        }
    }
    return 0;
}

int xrBindLoadFile(XrBindSet* s, const char* path, XrBindErrors* errs) {
    char def[1024];
    xrBindSetInit(s);
    if (errs != NULL) {
        memset(errs, 0, sizeof(*errs));
    }
    if (path == NULL) {
        path = xrBindDefaultPath(def, sizeof(def));
    }
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        if (errno != ENOENT) {
            addError(errs, 0, 0, "%s: %s", path, strerror(errno));
            return -1;
        }
        int len = xrBindDefaultText(NULL, 0);
        char* text = malloc((size_t)len + 1);
        if (text == NULL) {
            addError(errs, 0, 0, "%s: out of memory", path);
            return -1;
        }
        xrBindDefaultText(text, (size_t)len + 1);
        int ok = makeParents(path) == 0;
        FILE* w = ok ? fopen(path, "wb") : NULL;
        ok = w != NULL && fwrite(text, 1, (size_t)len, w) == (size_t)len;
        if (w != NULL && fclose(w) != 0) {
            ok = 0;
        }
        free(text);
        if (!ok) {
            addError(errs, 0, 0, "%s: could not write the default bindings", path);
            return -1;
        }
        return 1;
    }
    // A bindings file is a few KB; anything past 1 MB is not one
    char* text = NULL;
    size_t len = 0, cap = 0;
    int ok = 1;
    for (;;) {
        if (len + 4096 + 1 > cap) {
            size_t nc = cap > 0 ? cap * 2 : 16384;
            if (nc > (1u << 20)) {
                ok = 0;
                break;
            }
            char* nt = realloc(text, nc);
            if (nt == NULL) {
                ok = 0;
                break;
            }
            text = nt;
            cap = nc;
        }
        size_t got = fread(text + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    if (ferror(f)) {
        ok = 0;
    }
    fclose(f);
    if (!ok) {
        free(text);
        addError(errs, 0, 0, "%s: could not be read (or is over 1 MB)", path);
        return -1;
    }
    text[len] = '\0';
    xrBindParse(s, text, errs);
    free(text);
    return 0;
}

// ---- the resolved table ---------------------------------------------------------------------------

static void setBinding(XrBindTable* t, uint64_t sources, const XrBindAction* a, int from) {
    if (bitCount(sources) == 1) {
        for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
            if (sources == XR_BIND_BIT(i)) {
                t->single[i] = *a;
                t->from[i] = from;
            }
        }
        return;
    }
    for (int i = 0; i < t->chordCount; i++) {
        if (t->chords[i].parts == sources) {
            t->chords[i].action = *a;
            t->chords[i].from = from;
            return;
        }
    }
    if (t->chordCount < XR_BIND_MAX_CHORDS) {
        XrBindChord* c = &t->chords[t->chordCount++];
        c->parts = sources;
        c->action = *a;
        c->from = from;
    }
}

void xrBindResolve(const XrBindSet* s, int mode, const char* app, XrBindTable* out) {
    memset(out, 0, sizeof(*out));
    if (mode != XR_BIND_MODE_GAMEPAD) {
        mode = XR_BIND_MODE_POINTER;
    }
    out->mode = mode;
    for (int i = 0; i < DEFAULT_COUNT; i++) {
        if (DEFAULTS[i].mode == mode) {
            XrBindAction a = { DEFAULTS[i].kind, DEFAULTS[i].code, 0 };
            setBinding(out, DEFAULTS[i].sources, &a, XR_BIND_FROM_DEFAULT);
        }
    }
    if (s != NULL) {
        int appIdx = -1;
        if (app != NULL && *app != '\0') {
            for (int i = 0; i < s->appCount; i++) {
                if (ieq(s->apps[i], app)) {
                    appIdx = i;
                }
            }
        }
        for (int i = 0; i < s->ruleCount; i++) {
            const XrBindRule* r = &s->rules[i];
            if (r->app < 0 && r->mode == mode) {
                setBinding(out, r->sources, &r->action, XR_BIND_FROM_MODE);
            }
        }
        for (int i = 0; appIdx >= 0 && i < s->ruleCount; i++) {
            const XrBindRule* r = &s->rules[i];
            if (r->app == appIdx && r->mode == mode) {
                setBinding(out, r->sources, &r->action, XR_BIND_FROM_APP);
            }
        }
    }
    // A chord bound to none is no chord
    int n = 0;
    for (int i = 0; i < out->chordCount; i++) {
        if (out->chords[i].action.kind != XR_BIND_ACT_NONE) {
            out->chords[n++] = out->chords[i];
        }
    }
    out->chordCount = n;
}

// ---- the table at work ---------------------------------------------------------------------------

static int held(float value, int was, float on, float off) {
    return was ? value > off : value >= on;
}

static uint64_t handDown(const PadHand* p, int left, uint64_t was) {
    uint64_t m = 0;
#define SRC(l, r) (left ? XR_BIND_SRC_LEFT_##l : XR_BIND_SRC_RIGHT_##r)
#define WAS(src) ((was & XR_BIND_BIT(src)) != 0)
#define SET(src, on) do { if (on) { m |= XR_BIND_BIT(src); } } while (0)
    int s;
    s = SRC(TRIGGER, TRIGGER);
    SET(s, padGripDown(p->trigger, WAS(s)));
    s = SRC(GRIP, GRIP);
    SET(s, padGripDown(p->grip, WAS(s)));
    s = SRC(STICK_UP, STICK_UP);
    SET(s, held(p->stickY, WAS(s), XR_BIND_STICK_ON, XR_BIND_STICK_OFF));
    s = SRC(STICK_DOWN, STICK_DOWN);
    SET(s, held(-p->stickY, WAS(s), XR_BIND_STICK_ON, XR_BIND_STICK_OFF));
    s = SRC(STICK_RIGHT, STICK_RIGHT);
    SET(s, held(p->stickX, WAS(s), XR_BIND_STICK_ON, XR_BIND_STICK_OFF));
    s = SRC(STICK_LEFT, STICK_LEFT);
    SET(s, held(-p->stickX, WAS(s), XR_BIND_STICK_ON, XR_BIND_STICK_OFF));
    SET(SRC(STICK_CLICK, STICK_CLICK), p->stickClick);
    SET(SRC(BUMPER, BUMPER), p->bumper);
    if (left) {
        // The left controller's lower and upper are the d-pad's down and
        // right, and its menu the View button
        SET(XR_BIND_SRC_LEFT_VIEW, p->menu);
        SET(XR_BIND_SRC_LEFT_DPAD_UP, p->dpadUp);
        SET(XR_BIND_SRC_LEFT_DPAD_DOWN, p->lower);
        SET(XR_BIND_SRC_LEFT_DPAD_LEFT, p->dpadLeft);
        SET(XR_BIND_SRC_LEFT_DPAD_RIGHT, p->upper);
    }
    else {
        SET(XR_BIND_SRC_RIGHT_A, p->lower);
        SET(XR_BIND_SRC_RIGHT_B, p->upper);
        SET(XR_BIND_SRC_RIGHT_X, p->faceX);
        SET(XR_BIND_SRC_RIGHT_Y, p->faceY);
        SET(XR_BIND_SRC_RIGHT_MENU, p->start);
    }
#undef SRC
#undef WAS
#undef SET
    return m;
}

uint64_t xrBindSourcesDown(const PadHand* left, const PadHand* right, uint64_t was) {
    return handDown(left, 1, was) | handDown(right, 0, was);
}

// A chord made of the disconnect chord's controls while another of them, or
// anything else that is LB or RB, is down: the disconnect chord may be on its
// way, and the chord gives way to it the way padGuideStep's Guide does
static int givesWay(const XrBindTable* t, uint64_t down, uint64_t parts) {
    if ((parts & ~DISCONNECT_CHORD) != 0) {
        return 0;
    }
    uint64_t others = down & ~parts;
    if ((others & DISCONNECT_CHORD) != 0) {
        return 1;
    }
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if ((others & XR_BIND_BIT(i)) && t->single[i].kind == XR_BIND_ACT_PAD
                && (t->single[i].code & (PAD_LB | PAD_RB)) != 0) {
            return 1;
        }
    }
    return 0;
}

static void emit(XrBindAction* out, int cap, int* n, const XrBindAction* a) {
    if (*n < cap) {
        out[(*n)++] = *a;
    }
}

int xrBindHeld(const XrBindTable* t, uint64_t down, XrBindAction* out, int cap) {
    int n = 0;
    uint64_t used = WHOLE_STICKS;
    int gamepad = t->mode == XR_BIND_MODE_GAMEPAD;
    if (gamepad && (down & DISCONNECT_CHORD) == DISCONNECT_CHORD) {
        XrBindAction a = { XR_BIND_ACT_PAD, PAD_START | PAD_BACK | PAD_LB | PAD_RB, 0 };
        emit(out, cap, &n, &a);
        used |= DISCONNECT_CHORD;
    }
    // The biggest chords first, so one inside another gives way to it
    for (int parts = XR_BIND_CHORD_MAX_PARTS; parts >= 2; parts--) {
        for (int i = 0; i < t->chordCount; i++) {
            const XrBindChord* c = &t->chords[i];
            if (bitCount(c->parts) != parts || (down & c->parts) != c->parts
                    || (used & c->parts) != 0) {
                continue;
            }
            if (gamepad && givesWay(t, down, c->parts)) {
                continue;
            }
            emit(out, cap, &n, &c->action);
            used |= c->parts;
        }
    }
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        uint64_t bit = XR_BIND_BIT(i);
        if ((down & bit) && !(used & bit) && t->single[i].kind != XR_BIND_ACT_NONE) {
            emit(out, cap, &n, &t->single[i]);
        }
    }
    return n;
}

#define HELD_MAX (XR_BIND_SRC_COUNT + XR_BIND_MAX_CHORDS + 1)

int xrBindMouseMask(const XrBindTable* t, uint64_t down) {
    XrBindAction a[HELD_MAX];
    int n = xrBindHeld(t, down, a, HELD_MAX);
    int mask = 0;
    for (int i = 0; i < n; i++) {
        if (a[i].kind == XR_BIND_ACT_MOUSE && a[i].code >= 0 && a[i].code < XR_BIND_MOUSE_BUTTONS) {
            mask |= 1 << a[i].code;
        }
    }
    return mask;
}

int xrBindPadButtons(const XrBindTable* t, uint64_t down) {
    XrBindAction a[HELD_MAX];
    int n = xrBindHeld(t, down, a, HELD_MAX);
    int buttons = 0;
    for (int i = 0; i < n; i++) {
        if (a[i].kind == XR_BIND_ACT_PAD) {
            buttons |= a[i].code & 0xFFFF;
        }
    }
    return buttons;
}
