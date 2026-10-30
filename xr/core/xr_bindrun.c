// The binding table at work frame to frame. See xr_bindrun.h. GPLv3.
#include "xr_bindrun.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define BIT(s) XR_BIND_BIT(XR_BIND_SRC_##s)

// Windows virtual keys a stick's directions take
#define VK_W 0x57
#define VK_A 0x41
#define VK_S 0x53
#define VK_D 0x44
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28

static int holds(const XrBindAction* list, int n, const XrBindAction* a) {
    for (int i = 0; i < n; i++) {
        if (xrBindActionEqual(&list[i], a)) {
            return 1;
        }
    }
    return 0;
}

void xrBindRunReset(XrBindRun* r) {
    memset(r, 0, sizeof(*r));
}

void xrBindRunStep(XrBindRun* r, const XrBindTable* t, uint64_t down, XrBindEdges* out) {
    r->ignore &= down;
    down &= ~r->ignore;
    XrBindAction raw[XR_BIND_HELD_MAX];
    int n = xrBindHeld(t, down, raw, XR_BIND_HELD_MAX);
    out->heldCount = 0;
    out->pressedCount = 0;
    out->releasedCount = 0;
    for (int i = 0; i < n; i++) {
        if (raw[i].kind != XR_BIND_ACT_NONE && !holds(out->held, out->heldCount, &raw[i])) {
            out->held[out->heldCount++] = raw[i];
        }
    }
    for (int i = 0; i < out->heldCount; i++) {
        if (!holds(r->held, r->count, &out->held[i])) {
            out->pressed[out->pressedCount++] = out->held[i];
        }
    }
    for (int i = 0; i < r->count; i++) {
        if (!holds(out->held, out->heldCount, &r->held[i])) {
            out->released[out->releasedCount++] = r->held[i];
        }
    }
    r->count = out->heldCount;
    memcpy(r->held, out->held, sizeof(r->held[0]) * (size_t)out->heldCount);
}

void xrBindRunLetGo(XrBindRun* r, uint64_t down, XrBindEdges* out) {
    out->heldCount = 0;
    out->pressedCount = 0;
    out->releasedCount = r->count;
    memcpy(out->released, r->held, sizeof(r->held[0]) * (size_t)r->count);
    r->count = 0;
    r->ignore |= down;
}

int xrBindEdgesMouse(const XrBindEdges* e) {
    int mask = 0;
    for (int i = 0; i < e->heldCount; i++) {
        const XrBindAction* a = &e->held[i];
        if (a->kind == XR_BIND_ACT_MOUSE && a->code >= 0 && a->code < XR_BIND_MOUSE_BUTTONS) {
            mask |= 1 << a->code;
        }
    }
    return mask;
}

int xrBindEdgesPad(const XrBindEdges* e, int* pulled) {
    int buttons = 0, full = 0;
    for (int i = 0; i < e->heldCount; i++) {
        const XrBindAction* a = &e->held[i];
        if (a->kind == XR_BIND_ACT_PAD) {
            buttons |= a->code & 0xFFFF;
            full |= a->code & (PAD_HELD_LT | PAD_HELD_RT);
        }
    }
    if (pulled != NULL) {
        *pulled = full;
    }
    return buttons;
}

void xrBindTableExpandSticks(XrBindTable* t) {
    static const int STICKS[2] = { XR_BIND_SRC_LEFT_STICK, XR_BIND_SRC_RIGHT_STICK };
    // up, down, left, right, the order the directions follow each stick in
    static const int KEYS_WASD[4] = { VK_W, VK_S, VK_A, VK_D };
    static const int KEYS_ARROWS[4] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
    static const int PAD_DPAD[4] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT };
    for (int side = 0; side < 2; side++) {
        const XrBindAction* a = &t->single[STICKS[side]];
        if (a->kind != XR_BIND_ACT_STICK) {
            continue;
        }
        const int* codes;
        int kind = XR_BIND_ACT_KEY;
        if (a->code == XR_BIND_STICK_WASD) {
            codes = KEYS_WASD;
        }
        else if (a->code == XR_BIND_STICK_ARROWS) {
            codes = KEYS_ARROWS;
        }
        else if (a->code == XR_BIND_STICK_DPAD) {
            int pad = t->mode == XR_BIND_MODE_GAMEPAD;
            codes = pad ? PAD_DPAD : KEYS_ARROWS;
            kind = pad ? XR_BIND_ACT_PAD : XR_BIND_ACT_KEY;
        }
        else {
            continue;
        }
        for (int d = 0; d < 4; d++) {
            XrBindAction* dir = &t->single[STICKS[side] + 1 + d];
            if (dir->kind == XR_BIND_ACT_NONE) {
                dir->kind = kind;
                dir->code = codes[d];
                dir->mods = 0;
            }
        }
    }
}

static int chordIndex(const XrBindTable* t, uint64_t parts) {
    for (int i = 0; i < t->chordCount; i++) {
        if (t->chords[i].parts == parts) {
            return i;
        }
    }
    return -1;
}

int xrBindTableSame(const XrBindTable* a, const XrBindTable* b) {
    if (a->mode != b->mode || a->chordCount != b->chordCount) {
        return 0;
    }
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (!xrBindActionEqual(&a->single[i], &b->single[i])) {
            return 0;
        }
    }
    for (int i = 0; i < a->chordCount; i++) {
        int j = chordIndex(b, a->chords[i].parts);
        if (j < 0 || !xrBindActionEqual(&a->chords[i].action, &b->chords[j].action)) {
            return 0;
        }
    }
    return 1;
}

static int isPad(const XrBindAction* a, int code) {
    return a->kind == XR_BIND_ACT_PAD && a->code == code;
}

int xrBindTableGuideHold(XrBindTable* t) {
    const uint64_t parts = BIT(LEFT_VIEW) | BIT(RIGHT_MENU);
    if (t->mode != XR_BIND_MODE_GAMEPAD || !isPad(&t->single[XR_BIND_SRC_LEFT_VIEW], PAD_BACK)
            || !isPad(&t->single[XR_BIND_SRC_RIGHT_MENU], PAD_START)) {
        return 0;
    }
    int i = chordIndex(t, parts);
    if (i < 0 || !isPad(&t->chords[i].action, PAD_GUIDE)) {
        return 0;
    }
    for (int j = i + 1; j < t->chordCount; j++) {
        t->chords[j - 1] = t->chords[j];
    }
    t->chordCount--;
    return 1;
}

typedef struct {
    char* out;
    size_t cap;
    size_t len;
    int count;
} Line;

static void say(Line* l, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void say(Line* l, const char* fmt, ...) {
    if (l->len >= l->cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(l->out + l->len, l->cap - l->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        l->len += (size_t)n < l->cap - l->len ? (size_t)n : l->cap - l->len - 1;
    }
}

static void sayBinding(Line* l, uint64_t parts, const XrBindAction* a) {
    char act[64];
    xrBindActionFormat(a, act, sizeof(act));
    say(l, "%s", l->count > 0 ? ", " : "");
    int first = 1;
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (parts & XR_BIND_BIT(i)) {
            say(l, "%s%s", first ? "" : "+", xrBindSourceName(i));
            first = 0;
        }
    }
    say(l, " = %s", act);
    l->count++;
}

int xrBindSummary(const XrBindTable* t, char* out, size_t cap) {
    static const XrBindAction unbound = { XR_BIND_ACT_NONE, 0, 0 };
    Line l = { out, cap, 0, 0 };
    if (cap > 0) {
        out[0] = '\0';
    }
    XrBindTable d;
    xrBindResolve(NULL, t->mode, NULL, &d);
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (!xrBindActionEqual(&t->single[i], &d.single[i])) {
            sayBinding(&l, XR_BIND_BIT(i), &t->single[i]);
        }
    }
    for (int i = 0; i < t->chordCount; i++) {
        int j = chordIndex(&d, t->chords[i].parts);
        if (j < 0 || !xrBindActionEqual(&t->chords[i].action, &d.chords[j].action)) {
            sayBinding(&l, t->chords[i].parts, &t->chords[i].action);
        }
    }
    for (int j = 0; j < d.chordCount; j++) {
        if (chordIndex(t, d.chords[j].parts) < 0) {
            sayBinding(&l, d.chords[j].parts, &unbound);
        }
    }
    return l.count;
}
