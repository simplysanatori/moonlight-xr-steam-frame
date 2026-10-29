// The pad probe's core, see padprobe.h. GPLv3.
#include "padprobe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Limelight.h"

// ---- output buffer ----------------------------------------------------------------

typedef struct {
    char* out;
    size_t cap, len;
} Buf;

static void put(Buf* b, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Buf* b, const char* fmt, ...) {
    if (b->cap == 0 || b->len >= b->cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->out + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= b->cap - b->len) b->len = b->cap - 1;
    else b->len += (size_t)n;
}

static Buf begin(char* out, size_t cap) {
    Buf b = { out, cap, 0 };
    if (cap) out[0] = 0;
    return b;
}

// ---- names --------------------------------------------------------------------------

const char* padProbeTypeName(unsigned type) {
    switch (type) {
    case LI_CTYPE_UNKNOWN: return "Unknown";
    case LI_CTYPE_XBOX: return "Xbox";
    case LI_CTYPE_PS: return "PlayStation";
    case LI_CTYPE_NINTENDO: return "Nintendo";
    case LI_CTYPE_STEAM: return "Steam";
    default: return "?";
    }
}

static const struct { int flag; const char* name; } kButtonNames[] = {
    { A_FLAG, "A" }, { B_FLAG, "B" }, { X_FLAG, "X" }, { Y_FLAG, "Y" },
    { UP_FLAG, "Up" }, { DOWN_FLAG, "Down" }, { LEFT_FLAG, "Left" }, { RIGHT_FLAG, "Right" },
    { LB_FLAG, "LB" }, { RB_FLAG, "RB" }, { PLAY_FLAG, "Start" }, { BACK_FLAG, "Back" },
    { LS_CLK_FLAG, "LS" }, { RS_CLK_FLAG, "RS" }, { SPECIAL_FLAG, "Guide" }, { MISC_FLAG, "Misc" },
    { PADDLE1_FLAG, "P1" }, { PADDLE2_FLAG, "P2" }, { PADDLE3_FLAG, "P3" }, { PADDLE4_FLAG, "P4" },
    { TOUCHPAD_FLAG, "Touchpad" },
};

size_t padProbeButtons(int flags, char* out, size_t cap) {
    Buf b = begin(out, cap);
    int n = 0;
    for (size_t i = 0; i < sizeof(kButtonNames) / sizeof(kButtonNames[0]); i++)
        if (flags & kButtonNames[i].flag) put(&b, "%s%s", n++ ? "+" : "", kButtonNames[i].name);
    if (!n) put(&b, "-");
    return b.len;
}

static const struct { unsigned bit; const char* name; } kCapNames[] = {
    { LI_CCAP_ANALOG_TRIGGERS, "analog-triggers" }, { LI_CCAP_RUMBLE, "rumble" },
    { LI_CCAP_TRIGGER_RUMBLE, "trigger-rumble" }, { LI_CCAP_TOUCHPAD, "touchpad" },
    { LI_CCAP_ACCEL, "accel" }, { LI_CCAP_GYRO, "gyro" }, { LI_CCAP_BATTERY_STATE, "battery" },
    { LI_CCAP_RGB_LED, "led" }, { LI_CCAP_DUAL_TOUCHPAD, "dual-touchpad" },
};

size_t padProbeCaps(unsigned caps, char* out, size_t cap) {
    Buf b = begin(out, cap);
    int n = 0;
    for (size_t i = 0; i < sizeof(kCapNames) / sizeof(kCapNames[0]); i++)
        if (caps & kCapNames[i].bit) put(&b, "%s%s", n++ ? " " : "", kCapNames[i].name);
    if (!n) put(&b, "none");
    return b.len;
}

// ---- watch lines ----------------------------------------------------------------------

size_t padProbeStateLine(char* out, size_t cap, double t, int id, const char* what, const PadOutPacket* pk) {
    char btn[160];
    padProbeButtons(pk->buttonFlags, btn, sizeof(btn));
    Buf b = begin(out, cap);
    put(&b, "%9.3f [%d] %-26s btn=%s lt=%u rt=%u L=%d,%d R=%d,%d", t, id, what, btn, (unsigned)pk->leftTrigger,
        (unsigned)pk->rightTrigger, pk->leftStickX, pk->leftStickY, pk->rightStickX, pk->rightStickY);
    return b.len;
}

size_t padProbeSensorLine(char* out, size_t cap, double t, int id, const char* sensor, const float v[3]) {
    Buf b = begin(out, cap);
    put(&b, "%9.3f [%d] %-5s x=%.3f y=%.3f z=%.3f %s", t, id, sensor, (double)v[0], (double)v[1], (double)v[2],
        strcmp(sensor, "gyro") == 0 ? "rad/s" : "m/s^2");
    return b.len;
}

// ---- the session ----------------------------------------------------------------------

void padProbeInit(PadProbe* p) {
    memset(p, 0, sizeof(*p));
    gpInit(&p->gp, NULL, NULL, NULL, NULL);
    for (int i = 0; i < PP_MAX; i++) p->dev[i].number = p->dev[i].partner = p->dev[i].dupOf = -1;
}

void padProbeDestroy(PadProbe* p) { gpDestroy(&p->gp); }

PadProbeDev* padProbeFind(PadProbe* p, int id) {
    for (int i = 0; i < PP_MAX; i++)
        if (p->dev[i].used && p->dev[i].id == id) return &p->dev[i];
    return NULL;
}

PadProbeDev* padProbeAdd(PadProbe* p, const PadProbeDev* facts, const char* ignoreList) {
    int slot = -1;
    for (int i = 0; i < PP_MAX && slot < 0; i++)
        if (!p->dev[i].used) slot = i;
    if (slot < 0) return NULL;
    PadProbeDev* d = &p->dev[slot];
    *d = *facts;
    d->used = 1;
    d->ignored = 0;
    d->number = d->partner = d->dupOf = -1;
    if (!d->isController) return d; // the app only opens game controllers
    d->ignored = gpIgnoreReason(&d->info, ignoreList);
    if (d->ignored) return d;
    int n = gpAdd(&p->gp, d->id, &d->info);
    d->number = n;
    if (n >= 0) {
        // A number someone else holds: the Steam Input copy and its pad are one
        for (int j = 0; j < PP_MAX; j++) {
            PadProbeDev* o = &p->dev[j];
            if (j == slot || !o->used || !o->isController || o->ignored || o->number != n) continue;
            d->partner = j;
            o->partner = slot;
            break;
        }
    } else if (d->info.serial[0]) {
        for (int j = 0; j < PP_MAX; j++) {
            const PadProbeDev* o = &p->dev[j];
            if (j == slot || !o->used || !o->isController || o->ignored) continue;
            if (!strcmp(o->info.serial, d->info.serial) && o->info.vendor == d->info.vendor
                && o->info.product == d->info.product) {
                d->dupOf = j;
                break;
            }
        }
    }
    return d;
}

int padProbeRemove(PadProbe* p, int id, PadProbeDev* out) {
    PadProbeDev* d = padProbeFind(p, id);
    if (!d) return 0;
    int slot = (int)(d - p->dev);
    if (out) *out = *d;
    if (d->isController && !d->ignored) gpRemove(&p->gp, id);
    if (d->partner >= 0) p->dev[d->partner].partner = -1;
    for (int j = 0; j < PP_MAX; j++)
        if (p->dev[j].used && p->dev[j].dupOf == slot) p->dev[j].dupOf = -1;
    d->used = 0;
    d->partner = d->dupOf = -1;
    d->number = -1;
    return 1;
}

// ---- the report -----------------------------------------------------------------------

static int realPads(const PadProbe* p) {
    int n = 0;
    for (int i = 0; i < PP_MAX; i++) {
        const PadProbeDev* o = &p->dev[i];
        if (o->used && o->isController && !o->ignored && o->number >= 0 && !o->info.mirror) n++;
    }
    return n;
}

static void steamLine(Buf* b, const PadProbe* p, const PadProbeDev* d) {
    const PadProbeDev* o = d->partner >= 0 ? &p->dev[d->partner] : NULL;
    const char* pidName = d->info.vendor == 0x28de ? gpSteamControllerPidName(d->info.product) : NULL;
    if (d->info.mirror) {
        if (o) {
            put(b, "    steam       Steam Virtual Gamepad (28de:11ff): Steam Input's copy, merged into [%d] %s (controller #%d)\n",
                o->id, o->info.name, d->number);
            if (realPads(p) > 1)
                put(b, "                (several real pads are attached: the app joins a copy to the lowest-numbered pad that has none, which may not be its own)\n");
        } else if (d->number >= 0) {
            put(b, "    steam       Steam Virtual Gamepad (28de:11ff): Steam Input's copy, no real pad joined to it (controller #%d)\n",
                d->number);
            put(b, "                a pad that only exists as Steam Input's copy, e.g. a Steam Controller behind Steam Input: the copy has no sensors\n");
        } else {
            put(b, "    steam       Steam Virtual Gamepad (28de:11ff)\n");
        }
    } else if (pidName) {
        put(b, "    steam       %s (%04x:%04x), on the app's Steam Controller list\n", pidName, d->info.vendor, d->info.product);
        if (o) put(b, "                its Steam Input copy is [%d] %s, merged into it\n", o->id, o->info.name);
        else put(b, "                no Steam Input copy listed: SDL has the device itself (Steam Input is off or not claiming it)\n");
    } else if (o) {
        put(b, "    steam       real pad; its Steam Input copy is [%d] %s, merged into it\n", o->id, o->info.name);
    } else {
        put(b, "    steam       no\n");
    }
}

size_t padProbeFormat(const PadProbe* p, const PadProbeDev* d, char* out, size_t cap) {
    Buf b = begin(out, cap);
    char caps[160];
    put(&b, "[%d] %s   (SDL index %d)\n", d->id, d->info.name, d->index);
    if (d->isController)
        put(&b, "    sdl type    %s -> fork type %s (LI_CTYPE %u)\n", d->sdlType, padProbeTypeName(d->info.type),
            (unsigned)d->info.type);
    else
        put(&b, "    sdl type    none: a joystick without a game controller mapping (the app does not use it;\n"
                "                MLXR_GAMECONTROLLERDB or SDL_GAMECONTROLLERCONFIG can add a mapping)\n");
    put(&b, "    ids         %04x:%04x   version 0x%04x\n", d->info.vendor, d->info.product, (unsigned)d->version);
    put(&b, "    guid        %s\n", d->info.guid);
    if (d->info.serial[0]) put(&b, "    serial      %s\n", d->info.serial);
    else put(&b, "    serial      none\n");
    if (d->path[0]) put(&b, "    path        %s\n", d->path);
    if (d->isController) {
        size_t n = strlen(d->info.mapping);
        if (n) put(&b, "    mapping     %.90s%s\n", d->info.mapping, n > 90 ? "..." : "");
        else put(&b, "    mapping     none\n");
    }
    put(&b, "    inputs      %d axes, %d buttons, %d hats\n", d->axes, d->buttons, d->hats);
    steamLine(&b, p, d);
    padProbeCaps(d->info.caps, caps, sizeof(caps));
    if (!d->isController) {
        put(&b, "    host        not used (no game controller mapping)\n");
    } else if (d->ignored) {
        put(&b, "    host        SKIPPED: %s\n", gpIgnoreReasonText(d->ignored));
    } else if (d->dupOf >= 0) {
        put(&b, "    host        skipped: the same pad as [%d] (same serial, vendor and product)\n", p->dev[d->dupOf].id);
    } else if (d->number < 0) {
        put(&b, "    host        no controller number free (all %d are taken)\n", GP_MAX);
    } else if (d->partner >= 0) {
        put(&b, "    host        controller #%d, shared with [%d]: one pad to the host. type %s, caps: %s, buttons 0x%x\n",
            d->number, p->dev[d->partner].id, padProbeTypeName(d->info.type), caps, (unsigned)d->info.buttons);
    } else {
        put(&b, "    host        controller #%d, type %s, caps: %s, buttons 0x%x\n", d->number,
            padProbeTypeName(d->info.type), caps, (unsigned)d->info.buttons);
    }
    if (d->isController) {
        char gy[40], ac[40];
        if (d->hasGyro && d->gyroHz > 0) snprintf(gy, sizeof(gy), "yes, %g Hz", (double)d->gyroHz);
        else snprintf(gy, sizeof(gy), "%s", d->hasGyro ? "yes" : "no");
        if (d->hasAccel && d->accelHz > 0) snprintf(ac, sizeof(ac), "yes, %g Hz", (double)d->accelHz);
        else snprintf(ac, sizeof(ac), "%s", d->hasAccel ? "yes" : "no");
        put(&b, "    sensors     gyro %s; accel %s\n", gy, ac);
        put(&b, "    rumble      %s; trigger rumble %s\n", (d->info.caps & LI_CCAP_RUMBLE) ? "yes" : "no",
            (d->info.caps & LI_CCAP_TRIGGER_RUMBLE) ? "yes" : "no");
        put(&b, "    other       touchpads %d; LED %s; battery %s; player index %d\n", d->touchpads,
            d->hasLed ? "yes" : "no", d->battery[0] ? d->battery : "n/a", d->playerIndex);
    } else {
        put(&b, "    other       battery %s\n", d->battery[0] ? d->battery : "n/a");
    }
    return b.len;
}

size_t padProbeSummary(const PadProbe* p, char* out, size_t cap) {
    Buf b = begin(out, cap);
    char caps[160];
    int any = 0;
    put(&b, "what the app presents to the host:\n");
    for (int i = 0; i < PP_MAX; i++) {
        const PadProbeDev* d = &p->dev[i];
        if (!d->used || !d->isController || d->ignored || d->number < 0) continue;
        if (d->info.mirror && d->partner >= 0) continue; // listed with its real pad
        padProbeCaps(d->info.caps, caps, sizeof(caps));
        put(&b, "  pad #%d  [%d] %s  (%s, %04x:%04x)", d->number, d->id, d->info.name, padProbeTypeName(d->info.type),
            d->info.vendor, d->info.product);
        if (d->partner >= 0) put(&b, " + Steam Input copy [%d]", p->dev[d->partner].id);
        else if (d->info.mirror) put(&b, " (Steam Input copy, no real pad)");
        put(&b, "  caps: %s\n", caps);
        any = 1;
    }
    if (!any) put(&b, "  no pads\n");
    for (int i = 0; i < PP_MAX; i++) {
        const PadProbeDev* d = &p->dev[i];
        if (!d->used) continue;
        if (!d->isController) put(&b, "  not used: [%d] %s (no game controller mapping)\n", d->id, d->info.name);
        else if (d->ignored) put(&b, "  skipped:  [%d] %s (%s)\n", d->id, d->info.name, gpIgnoreReasonText(d->ignored));
        else if (d->dupOf >= 0) put(&b, "  skipped:  [%d] %s (same pad as [%d])\n", d->id, d->info.name, p->dev[d->dupOf].id);
    }
    return b.len;
}
