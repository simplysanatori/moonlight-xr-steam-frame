// The Host sheet's keys, macros and chord capture. See xr_hostkeys.h. GPLv3.
#include "xr_hostkeys.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xr_bind.h"
#include "xr_kbfull.h"
#include "xr_shared.h"

// ---- the grid ---------------------------------------------------------------------------------

#define HOSTCELL_PAD_X 34.0f
#define HOSTCELL_TOP 96.0f
#define HOSTCELL_W 180.0f
#define HOSTCELL_H 72.0f
#define HOSTCELL_GAP_X 14.0f
#define HOSTCELL_GAP_Y 12.0f

static void uv(float x0, float y0, float x1, float y1, float r[4]) {
    r[0] = x0 / (float)XR_HOST_TEX_W;
    r[1] = y0 / (float)XR_HOST_TEX_H;
    r[2] = x1 / (float)XR_HOST_TEX_W;
    r[3] = y1 / (float)XR_HOST_TEX_H;
}

void xrHostCellRect(int cell, float r[4]) {
    if (cell < 0 || cell >= XR_HOST_CELLS) {
        r[0] = r[1] = r[2] = r[3] = 0.0f;
        return;
    }
    int col = cell % XR_HOST_COLS;
    int row = cell / XR_HOST_COLS;
    float x = HOSTCELL_PAD_X + (float)col * (HOSTCELL_W + HOSTCELL_GAP_X);
    float y = HOSTCELL_TOP + (float)row * (HOSTCELL_H + HOSTCELL_GAP_Y);
    uv(x, y, x + HOSTCELL_W, y + HOSTCELL_H, r);
}

void xrHostCloseRect(float r[4]) {
    uv(840.0f, 22.0f, 990.0f, 70.0f, r);
}

void xrHostConfirmBox(float r[4]) {
    uv(212.0f, 112.0f, 812.0f, 352.0f, r);
}

void xrHostConfirmSendRect(float r[4]) {
    uv(252.0f, 276.0f, 502.0f, 332.0f, r);
}

void xrHostConfirmCancelRect(float r[4]) {
    uv(522.0f, 276.0f, 772.0f, 332.0f, r);
}

int xrHostCellKind(int cell, int macros) {
    if (cell < 0 || cell >= XR_HOST_CELLS) {
        return XR_HOST_CELL_NONE;
    }
    if (cell < XR_HOST_QUICK_COUNT) {
        return XR_HOST_CELL_QUICK;
    }
    if (cell == XR_HOST_CELL_SEND_KEYS) {
        return XR_HOST_CELL_KEYS;
    }
    return cell - XR_HOST_MACRO_FIRST < macros ? XR_HOST_CELL_MACRO : XR_HOST_CELL_NONE;
}

static int inRect(const float r[4], float u, float v) {
    return u >= r[0] && u <= r[2] && v >= r[1] && v <= r[3];
}

int xrHostZoneAt(float u, float v, int macros, int confirm) {
    float r[4];
    if (confirm) {
        xrHostConfirmSendRect(r);
        if (inRect(r, u, v)) {
            return XR_HOST_ZONE_CONFIRM_SEND;
        }
        xrHostConfirmCancelRect(r);
        if (inRect(r, u, v)) {
            return XR_HOST_ZONE_CONFIRM_CANCEL;
        }
        return XR_HOST_ZONE_NONE;
    }
    xrHostCloseRect(r);
    if (inRect(r, u, v)) {
        return XR_HOST_ZONE_CLOSE;
    }
    for (int cell = 0; cell < XR_HOST_CELLS; cell++) {
        if (xrHostCellKind(cell, macros) == XR_HOST_CELL_NONE) {
            continue;
        }
        xrHostCellRect(cell, r);
        if (inRect(r, u, v)) {
            return cell;
        }
    }
    return XR_HOST_ZONE_NONE;
}

// ---- quick actions -------------------------------------------------------------------------------

static const XrHostQuick QUICK[XR_HOST_QUICK_COUNT] = {
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

const XrHostQuick* xrHostQuick(int i) {
    return i >= 0 && i < XR_HOST_QUICK_COUNT ? &QUICK[i] : NULL;
}

int xrHostModBit(int vk) {
    switch (vk) {
        case 0x10:
        case 0xA0:
            return KBF_LSHIFT;
        case 0xA1:
            return KBF_RSHIFT;
        case 0x11:
        case 0xA2:
            return KBF_LCTRL;
        case 0xA3:
            return KBF_RCTRL;
        case 0x12:
        case 0xA4:
            return KBF_LALT;
        case 0xA5:
            return KBF_RALT;
        case 0x5B:
            return KBF_LWIN;
        case 0x5C:
            return KBF_RWIN;
        default:
            return 0;
    }
}

// ---- macros.ini ------------------------------------------------------------------------------------

void xrMacrosClear(XrMacros* m) {
    memset(m, 0, sizeof(*m));
}

static void addError(XrMacros* m, int line, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

static void addError(XrMacros* m, int line, const char* fmt, ...) {
    if (m->errors++ == 0) {
        char why[160];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(why, sizeof(why), fmt, ap);
        va_end(ap);
        snprintf(m->error, sizeof(m->error), "line %d: %s", line, why);
    }
}

static int isSep(int c) {
    return c == ' ' || c == '\t' || c == ',';
}

static int lowerChar(int c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

// Whether s starts with prefix, case blind
static int hasPrefix(const char* s, const char* prefix) {
    for (; *prefix != '\0'; s++, prefix++) {
        if (lowerChar((unsigned char)*s) != lowerChar((unsigned char)*prefix)) {
            return 0;
        }
    }
    return 1;
}

static int namesEqual(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (lowerChar((unsigned char)*a) != lowerChar((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

// The steps of one macro. Returns 1, or 0 with why filled in.
static int parseSteps(const char* rhs, XrMacro* out, char* why, size_t cap) {
    const char* p = rhs;
    int first = 1;
    out->seq.count = 0;
    out->confirm = 0;
    for (;;) {
        while (isSep((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char* start = p;
        while (*p != '\0' && !isSep((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        char tok[96];
        if (len >= sizeof(tok)) {
            snprintf(why, cap, "a step longer than %d characters", (int)sizeof(tok) - 1);
            return 0;
        }
        memcpy(tok, start, len);
        tok[len] = '\0';
        char* t = tok;
        if (first && t[0] == '!') {
            // Asks first, whatever it does
            out->confirm = 1;
            t++;
            if (*t == '\0') {
                first = 0;
                continue;
            }
        }
        first = 0;
        if (out->seq.count >= XR_HOST_STEPS) {
            snprintf(why, cap, "more than %d steps", XR_HOST_STEPS);
            return 0;
        }
        XrHostStep* st = &out->seq.step[out->seq.count];
        memset(st, 0, sizeof(*st));
        if (hasPrefix(t, "wait:")) {
            const char* d = t + 5;
            long ms = 0;
            int digits = 0;
            for (; *d >= '0' && *d <= '9'; d++, digits++) {
                ms = ms * 10 + (*d - '0');
                if (ms > 100000) {
                    break;
                }
            }
            if (digits == 0 || *d != '\0' || ms < 1 || ms > XR_HOST_WAIT_MAX_MS) {
                snprintf(why, cap, "'%s': wait: takes milliseconds from 1 to %d", t,
                         XR_HOST_WAIT_MAX_MS);
                return 0;
            }
            st->kind = XR_HOST_STEP_WAIT;
            st->ms = (int)ms;
            out->seq.count++;
            continue;
        }
        const char* spec = t;
        int kind = XR_HOST_STEP_TAP;
        if (hasPrefix(t, "down:")) {
            kind = XR_HOST_STEP_DOWN;
            spec = t + 5;
        }
        else if (hasPrefix(t, "up:")) {
            kind = XR_HOST_STEP_UP;
            spec = t + 3;
        }
        else if (hasPrefix(t, "key:")) {
            spec = t + 4;
        }
        char text[128];
        snprintf(text, sizeof(text), "key:%s", spec);
        XrBindAction a;
        char err[120];
        if (!xrBindActionParse(text, &a, err, sizeof(err), NULL) || a.kind != XR_BIND_ACT_KEY) {
            snprintf(why, cap, "'%s': %s", t, err[0] != '\0' ? err : "not a key");
            return 0;
        }
        if (kind != XR_HOST_STEP_TAP && a.mods != 0) {
            snprintf(why, cap, "'%s': down: and up: take one key, not a chord", t);
            return 0;
        }
        st->kind = kind;
        st->vk = a.code;
        st->mods = a.mods;
        out->seq.count++;
    }
    if (out->seq.count == 0) {
        snprintf(why, cap, "no steps");
        return 0;
    }
    return 1;
}

int xrMacrosParse(XrMacros* m, const char* text) {
    xrMacrosClear(m);
    if (text == NULL) {
        return 0;
    }
    // A byte order mark in front is not part of the first line
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
            && (unsigned char)text[2] == 0xBF) {
        text += 3;
    }
    int inMacros = 1;
    int lineNo = 0;
    const char* p = text;
    while (*p != '\0') {
        const char* eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        const char* next = eol != NULL ? eol + 1 : p + n;
        lineNo++;
        char line[512];
        if (n >= sizeof(line)) {
            addError(m, lineNo, "a line longer than %d characters", (int)sizeof(line) - 1);
            p = next;
            continue;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = next;

        char* s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') {
            s++;
        }
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
            s[--len] = '\0';
        }
        if (len == 0 || s[0] == '#' || s[0] == ';') {
            continue;
        }
        if (s[0] == '[') {
            char* close = strchr(s, ']');
            if (close == NULL) {
                addError(m, lineNo, "a section without its ]");
                inMacros = 0;
                continue;
            }
            *close = '\0';
            inMacros = namesEqual(s + 1, "macros");
            if (!inMacros) {
                addError(m, lineNo, "unknown section [%s] (only [macros])", s + 1);
            }
            continue;
        }
        if (!inMacros) {
            continue;
        }
        char* eq = strchr(s, '=');
        if (eq == NULL) {
            addError(m, lineNo, "expected name = steps");
            continue;
        }
        *eq = '\0';
        char* name = s;
        char* rhs = eq + 1;
        size_t nl = strlen(name);
        while (nl > 0 && (name[nl - 1] == ' ' || name[nl - 1] == '\t')) {
            name[--nl] = '\0';
        }
        while (*rhs == ' ' || *rhs == '\t') {
            rhs++;
        }
        // A comment after the steps
        for (char* c = rhs; *c != '\0'; c++) {
            if ((c[0] == ' ' || c[0] == '\t') && (c[1] == ';' || c[1] == '#')) {
                *c = '\0';
                break;
            }
        }
        if (nl == 0) {
            addError(m, lineNo, "a macro needs a name before the =");
            continue;
        }
        if (nl >= XR_MACRO_NAME) {
            addError(m, lineNo, "the name '%s' is longer than %d characters", name,
                     XR_MACRO_NAME - 1);
            continue;
        }
        int dup = 0;
        for (int i = 0; i < m->count; i++) {
            if (namesEqual(m->macro[i].name, name)) {
                dup = 1;
            }
        }
        if (dup) {
            addError(m, lineNo, "'%s' is already a macro; the first one stands", name);
            continue;
        }
        if (m->count >= XR_MACRO_MAX) {
            addError(m, lineNo, "more than %d macros", XR_MACRO_MAX);
            continue;
        }
        XrMacro* out = &m->macro[m->count];
        memset(out, 0, sizeof(*out));
        char why[160];
        why[0] = '\0';
        if (!parseSteps(rhs, out, why, sizeof(why))) {
            addError(m, lineNo, "%s: %s", name, why);
            continue;
        }
        snprintf(out->name, sizeof(out->name), "%s", name);
        m->count++;
    }
    return m->count;
}

const char* xrMacrosDefaultPath(char* buf, size_t cap) {
    const char* dir = getenv("MLXR_CONFIG_DIR");
    if (dir != NULL && *dir != '\0') {
        snprintf(buf, cap, "%s/macros.ini", dir);
        return buf;
    }
    const char* home = getenv("HOME");
    snprintf(buf, cap, "%s/.config/moonlight-xr/macros.ini",
             home != NULL && *home != '\0' ? home : ".");
    return buf;
}

#define MACROS_FILE_MAX 65536

int xrMacrosLoadFile(XrMacros* m, const char* path) {
    xrMacrosClear(m);
    char def[1024];
    if (path == NULL) {
        path = xrMacrosDefaultPath(def, sizeof(def));
    }
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    char* text = malloc(MACROS_FILE_MAX + 1);
    if (text == NULL) {
        fclose(f);
        return -1;
    }
    size_t got = fread(text, 1, MACROS_FILE_MAX, f);
    int more = fgetc(f) != EOF;
    fclose(f);
    text[got] = '\0';
    xrMacrosParse(m, text);
    free(text);
    if (more) {
        addError(m, 0, "the file is longer than %d bytes; the rest is left out", MACROS_FILE_MAX);
    }
    return 1;
}

int xrHostCellSeq(int cell, const XrMacros* macros, XrHostSeq* seq, int* confirm, char* label,
                  size_t cap) {
    int count = macros != NULL ? macros->count : 0;
    int kind = xrHostCellKind(cell, count);
    seq->count = 0;
    *confirm = 0;
    if (cap > 0) {
        label[0] = '\0';
    }
    if (kind == XR_HOST_CELL_QUICK) {
        const XrHostQuick* q = &QUICK[cell];
        seq->count = 1;
        seq->step[0].kind = XR_HOST_STEP_TAP;
        seq->step[0].vk = q->vk;
        seq->step[0].mods = q->mods;
        seq->step[0].ms = 0;
        *confirm = q->confirm;
        snprintf(label, cap, "%s", q->label);
    }
    else if (kind == XR_HOST_CELL_MACRO) {
        const XrMacro* mac = &macros->macro[cell - XR_HOST_MACRO_FIRST];
        *seq = mac->seq;
        *confirm = mac->confirm;
        snprintf(label, cap, "%s", mac->name);
    }
    return kind;
}

// ---- playing a sequence ----------------------------------------------------------------------------

void xrHostRunInit(XrHostRun* r) {
    memset(r, 0, sizeof(*r));
}

int xrHostRunBusy(const XrHostRun* r) {
    return r->active;
}

void xrHostRunAbort(XrHostRun* r) {
    if (r->active) {
        r->aborting = 1;
        r->waiting = 0;
    }
}

int xrHostRunStart(XrHostRun* r, const XrHostSeq* seq) {
    if (seq == NULL || seq->count <= 0) {
        return 0;
    }
    // Whatever the last run still holds is let go before this one starts: its
    // keys are carried over and come up first
    int heldMods = r->active ? r->heldMods : 0;
    int downKeys[4] = { 0, 0, 0, 0 };
    int downCount = r->active ? r->downCount : 0;
    for (int i = 0; i < downCount; i++) {
        downKeys[i] = r->downKeys[i];
    }
    memset(r, 0, sizeof(*r));
    r->seq = *seq;
    r->active = 1;
    r->heldMods = heldMods;
    r->downCount = downCount;
    for (int i = 0; i < downCount; i++) {
        r->downKeys[i] = downKeys[i];
    }
    r->flush = heldMods != 0 || downCount > 0;
    return 1;
}

static void emit(XrHostEmit* e, int kind, int vk, int mods) {
    e->kind = kind;
    e->vk = vk;
    e->mods = mods;
}

// One held key or modifier let go: keys first, then the modifiers. 0 if none is held.
static int releaseOne(XrHostRun* r, XrHostEmit* e) {
    if (r->downCount > 0) {
        int vk = r->downKeys[--r->downCount];
        emit(e, XR_HOST_EMIT_UP, vk, r->heldMods);
        return 1;
    }
    if (r->heldMods != 0) {
        int bit = r->heldMods & -r->heldMods;
        r->heldMods &= ~bit;
        emit(e, XR_HOST_EMIT_LET, 0, bit);
        return 1;
    }
    return 0;
}

int xrHostRunStep(XrHostRun* r, long long nowNs, XrHostEmit* e) {
    if (!r->active) {
        return 0;
    }
    for (;;) {
        if (r->flush || r->aborting || r->pos >= r->seq.count) {
            // What a run before this one held, or what this one holds when it is
            // finished or cut short, is let go of
            if (releaseOne(r, e)) {
                return 1;
            }
            if (!r->flush) {
                r->active = 0;
                return 0;
            }
            r->flush = 0;
            continue;
        }
        const XrHostStep* st = &r->seq.step[r->pos];
        if (st->kind == XR_HOST_STEP_WAIT) {
            if (!r->waiting) {
                r->waiting = 1;
                r->waitUntilNs = nowNs + (long long)st->ms * 1000000LL;
            }
            if (nowNs < r->waitUntilNs) {
                return 0;
            }
            r->waiting = 0;
            r->pos++;
            continue;
        }
        r->pos++;
        int bit = xrHostModBit(st->vk);
        if (st->kind == XR_HOST_STEP_TAP) {
            emit(e, XR_HOST_EMIT_CHORD, st->vk, st->mods | r->heldMods);
            return 1;
        }
        if (st->kind == XR_HOST_STEP_DOWN) {
            if (bit != 0) {
                r->heldMods |= bit;
                emit(e, XR_HOST_EMIT_HOLD, 0, bit);
                return 1;
            }
            if (r->downCount < 4) {
                r->downKeys[r->downCount++] = st->vk;
            }
            emit(e, XR_HOST_EMIT_DOWN, st->vk, r->heldMods);
            return 1;
        }
        // XR_HOST_STEP_UP
        if (bit != 0) {
            r->heldMods &= ~bit;
            emit(e, XR_HOST_EMIT_LET, 0, bit);
            return 1;
        }
        for (int i = 0; i < r->downCount; i++) {
            if (r->downKeys[i] == st->vk) {
                r->downKeys[i] = r->downKeys[--r->downCount];
                break;
            }
        }
        emit(e, XR_HOST_EMIT_UP, st->vk, r->heldMods);
        return 1;
    }
}

// ---- Send keys -------------------------------------------------------------------------------------------

void xrHostChordStart(XrHostChord* c) {
    c->active = 1;
    c->mods = 0;
}

void xrHostChordStop(XrHostChord* c) {
    c->active = 0;
    c->mods = 0;
}

// The US layout's virtual key for punctuation with a key of its own, as the
// keyboards type it (vrkb_punctuation_key in stream/), or 0
static int punctuationVk(int code) {
    switch (code) {
        case ';': return 0xBA;
        case '=': return 0xBB;
        case ',': return 0xBC;
        case '-': return 0xBD;
        case '.': return 0xBE;
        case '/': return 0xBF;
        case '`': return 0xC0;
        case '[': return 0xDB;
        case '\\': return 0xDC;
        case ']': return 0xDD;
        case '\'': return 0xDE;
        default: return 0;
    }
}

int xrHostChordKey(XrHostChord* c, int code, int* vk, int* mods) {
    if (!c->active) {
        return XR_CHORD_IGNORE;
    }
    int key = 0;
    int shift = 0;
    if (code == KB_CODE_HIDE) {
        xrHostChordStop(c);
        return XR_CHORD_CANCEL;
    }
    if (code == KB_CODE_CTRL || code == KB_CODE_ALT || code == KB_CODE_WIN) {
        c->mods ^= code == KB_CODE_CTRL ? KBF_LCTRL : code == KB_CODE_ALT ? KBF_LALT : KBF_LWIN;
        return XR_CHORD_MOD;
    }
    if (code <= 0) {
        return XR_CHORD_IGNORE;
    }
    if (code >= KB_CODE_SVKL) {
        key = code - KB_CODE_SVKL;
        shift = 1;
    }
    else if (code >= KB_CODE_SVK) {
        key = code - KB_CODE_SVK;
        shift = 1;
    }
    else if (code >= KB_CODE_VK) {
        key = code - KB_CODE_VK;
        int bit = xrHostModBit(key);
        if (bit != 0) {
            c->mods ^= bit;
            return XR_CHORD_MOD;
        }
    }
    else if (code == 8 || code == 9 || code == 13 || code == 32 || (code >= '0' && code <= '9')) {
        key = code;
    }
    else if (code >= 'a' && code <= 'z') {
        key = code - 32;
    }
    else if (code >= 'A' && code <= 'Z') {
        key = code;
        shift = 1;
    }
    else {
        key = punctuationVk(code);
    }
    if (key <= 0 || key > 0xFE) {
        return XR_CHORD_IGNORE;
    }
    *vk = key;
    *mods = c->mods | (shift ? KBF_LSHIFT : 0);
    xrHostChordStop(c);
    return XR_CHORD_SEND;
}
