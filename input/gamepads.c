// Physical gamepads to the host, see gamepads.h. GPLv3.
#include "gamepads.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "Limelight.h"
#include "rumble.h"

// Moonlight's own test (moonlight-qt gamepad.cpp): exactly these four buttons
#define CHORD_BUTTONS (PLAY_FLAG | BACK_FLAG | LB_FLAG | RB_FLAG)

static void say(Gamepads* g, char level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(Gamepads* g, char level, const char* fmt, ...) {
    if (!g->log) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g->log(g->logUser, level, line);
}

int gpIsChord(int buttonFlags) { return buttonFlags == CHORD_BUTTONS; }

uint16_t gpCapsFor(const GpDeviceFacts* f) {
    uint16_t caps = 0;
    if (f->analogTriggers) caps |= LI_CCAP_ANALOG_TRIGGERS;
    if (f->rumble) caps |= LI_CCAP_RUMBLE;
    if (f->triggerRumble) caps |= LI_CCAP_TRIGGER_RUMBLE;
    if (f->touchpads > 0) caps |= LI_CCAP_TOUCHPAD;
    if (f->accel) caps |= LI_CCAP_ACCEL;
    if (f->gyro) caps |= LI_CCAP_GYRO;
    if (f->battery) caps |= LI_CCAP_BATTERY_STATE;
    if (f->led) caps |= LI_CCAP_RGB_LED;
    return caps;
}

int gpBatteryFromPowerLevel(int level, uint8_t* state, uint8_t* percentage) {
    switch (level) {
    case -1: // SDL_JOYSTICK_POWER_UNKNOWN
        *state = LI_BATTERY_STATE_UNKNOWN;
        *percentage = LI_BATTERY_PERCENTAGE_UNKNOWN;
        return 1;
    case 0: // EMPTY
        *state = LI_BATTERY_STATE_DISCHARGING;
        *percentage = 5;
        return 1;
    case 1: // LOW
        *state = LI_BATTERY_STATE_DISCHARGING;
        *percentage = 20;
        return 1;
    case 2: // MEDIUM
        *state = LI_BATTERY_STATE_DISCHARGING;
        *percentage = 50;
        return 1;
    case 3: // FULL
        *state = LI_BATTERY_STATE_DISCHARGING;
        *percentage = 90;
        return 1;
    case 4: // WIRED
        *state = LI_BATTERY_STATE_CHARGING;
        *percentage = LI_BATTERY_PERCENTAGE_UNKNOWN;
        return 1;
    default:
        return 0;
    }
}

// ---- classification ------------------------------------------------------

// SDL's controller_list.h (Valve products), as moonlight-qt 6.2 copies it
static const struct { unsigned product; const char* name; } kSteamPids[] = {
    { 0x1101, "Steam Controller (2015, CHELL)" },
    { 0x1102, "Steam Controller (2015, wired)" },
    { 0x1105, "Steam Controller (2015, Bluetooth)" },
    { 0x1106, "Steam Controller (2015, Bluetooth)" },
    { 0x1142, "Steam Controller (2015, wireless dongle)" },
    { 0x1201, "Steam Controller (HEADCRAB, wired)" },
    { 0x1202, "Steam Controller (HEADCRAB, Bluetooth)" },
    { 0x1205, "Steam Deck built-in controller" },
    { 0x1302, "Steam Controller (Triton, USB)" },
    { 0x1303, "Steam Controller (Triton, Bluetooth LE)" },
    { 0x1304, "Steam Controller (Proteus puck)" },
    { 0x1305, "Steam Controller (Nereid puck)" },
};

const char* gpSteamControllerPidName(unsigned product) {
    for (size_t i = 0; i < sizeof(kSteamPids) / sizeof(kSteamPids[0]); i++)
        if (kSteamPids[i].product == product) return kSteamPids[i].name;
    return NULL;
}

int gpSteamControllerPid(unsigned product) { return gpSteamControllerPidName(product) != NULL; }

int gpIsSteamVirtualGamepad(unsigned vendor, unsigned product) { return vendor == 0x28de && product == 0x11ff; }

uint8_t gpControllerType(GpSdlKind kind, unsigned vendor, unsigned product) {
    switch (kind) {
    case GP_SDL_XBOX360:
    case GP_SDL_XBOXONE:
        return LI_CTYPE_XBOX;
    case GP_SDL_PS3:
    case GP_SDL_PS4:
    case GP_SDL_PS5:
        return LI_CTYPE_PS;
    case GP_SDL_SWITCH_PRO:
    case GP_SDL_JOYCON_LEFT:
    case GP_SDL_JOYCON_RIGHT:
    case GP_SDL_JOYCON_PAIR:
        return LI_CTYPE_NINTENDO;
    default:
        return (vendor == 0x28de && gpSteamControllerPid(product)) ? LI_CTYPE_STEAM : LI_CTYPE_UNKNOWN;
    }
}

void gpClassify(GpInfo* info, GpSdlKind kind) {
    info->type = gpControllerType(kind, info->vendor, info->product);
    // Steam Input's virtual copy of a pad (SDL lists it as 28de:11ff)
    info->mirror = gpIsSteamVirtualGamepad(info->vendor, info->product);
}

static int containsNoCase(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int equalNoCase(const char* a, const char* b) {
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return *a == 0 && *b == 0;
}

int gpIgnoreReason(const GpInfo* info, const char* ignoreList) {
    if (info->vendor == 0x28de && containsNoCase(info->name, "frame")) return 1;
    if (ignoreList && *ignoreList) {
        char* copy = strdup(ignoreList);
        int hit = 0;
        if (!copy) return 0;
        for (char* save = NULL, *t = strtok_r(copy, ", ", &save); t && !hit; t = strtok_r(NULL, ", ", &save))
            hit = equalNoCase(t, info->guid);
        free(copy);
        return hit ? 2 : 0;
    }
    return 0;
}

const char* gpIgnoreReasonText(int reason) {
    switch (reason) {
    case 1: return "SteamVR's own Frame controller (read by SteamVR, not for the host)";
    case 2: return "its GUID is in MLXR_GAMEPAD_IGNORE";
    default: return "";
    }
}

void gpInit(Gamepads* g, const PadOutSink* sink, const GpHaptics* haptics, GpLog log, void* logUser) {
    memset(g, 0, sizeof(*g));
    pthread_mutex_init(&g->lock, NULL);
    if (sink) g->sink = *sink;
    if (haptics) g->haptics = *haptics;
    g->log = log;
    g->logUser = logUser;
    g->chordEnabled = 1;
    g->frameNumber = -1;
    atomic_init(&g->announcedMask, 0);
    g->ready = 1;
}

void gpDestroy(Gamepads* g) {
    if (!g->ready) return;
    g->ready = 0;
    pthread_mutex_destroy(&g->lock);
}

void gpSetExtraSink(Gamepads* g, const GpExtraSink* extra) {
    pthread_mutex_lock(&g->lock);
    memset(&g->extra, 0, sizeof(g->extra));
    if (extra) g->extra = *extra;
    pthread_mutex_unlock(&g->lock);
}

void gpSetDisconnectHook(Gamepads* g, void (*fn)(void*), void* user) {
    pthread_mutex_lock(&g->lock);
    g->disconnect = fn;
    g->disconnectUser = user;
    pthread_mutex_unlock(&g->lock);
}

void gpSetChordEnabled(Gamepads* g, int enabled) {
    pthread_mutex_lock(&g->lock);
    g->chordEnabled = enabled != 0;
    pthread_mutex_unlock(&g->lock);
}

// ---- sends (lock held) ---------------------------------------------------

static int live(const Gamepads* g) { return g->active && g->streaming; }

// ---- Native (lane artemis-settings): Artemis's gamepad options -----------------
static atomic_int gRumbleOff, gDeadzone, gFlip;

void gamepadsSetRumbleEnabled(int enabled) { atomic_store(&gRumbleOff, !enabled); }
void gamepadsSetDeadzone(int percent) { atomic_store(&gDeadzone, percent < -20 ? -20 : percent > 20 ? 20 : percent); }
int gamepadsGetDeadzone(void) { return atomic_load(&gDeadzone); }
void gamepadsSetFlipFaceButtons(int flip) { atomic_store(&gFlip, flip != 0); }

static void stickDeadzone(short* x, short* y, int pct) {
    double fx = *x, fy = *y, m = sqrt(fx * fx + fy * fy);
    if (m <= 0.0) return;
    const double full = 32767.0, dz = full * (pct < 0 ? -pct : pct) / 100.0;
    double out;
    if (pct > 0) out = m <= dz ? 0.0 : (m - dz) / (full - dz) * full;  // radial dead zone, rescaled
    else out = dz + (m > full ? full : m) * (full - dz) / full;          // compensation: start past dz
    double k = out / m;
    double nx = fx * k, ny = fy * k;
    *x = (short)(nx > 32767 ? 32767 : nx < -32768 ? -32768 : nx);
    *y = (short)(ny > 32767 ? 32767 : ny < -32768 ? -32768 : ny);
}

void gpApplyStickOptions(PadOutPacket* pk, int deadzonePercent, int flip) {
    if (deadzonePercent) {
        stickDeadzone(&pk->leftStickX, &pk->leftStickY, deadzonePercent);
        stickDeadzone(&pk->rightStickX, &pk->rightStickY, deadzonePercent);
    }
    if (flip) {
        int b = pk->buttonFlags & ~(A_FLAG | B_FLAG | X_FLAG | Y_FLAG);
        if (pk->buttonFlags & A_FLAG) b |= B_FLAG;
        if (pk->buttonFlags & B_FLAG) b |= A_FLAG;
        if (pk->buttonFlags & X_FLAG) b |= Y_FLAG;
        if (pk->buttonFlags & Y_FLAG) b |= X_FLAG;
        pk->buttonFlags = b;
    }
}

static int maskNow(const Gamepads* g) {
    int m = atomic_load(&g->announcedMask);
    if (g->frameNumber >= 0) m |= 1 << g->frameNumber;
    return m & 0xffff;
}

static void sendPacket(Gamepads* g, GpSlot* s, const PadOutPacket* pk) {
    if (g->sink.state) {
        // Native (lane artemis-settings): dead zone and face button flip on what the host gets only
        PadOutPacket o = *pk;
        gpApplyStickOptions(&o, atomic_load(&gDeadzone), atomic_load(&gFlip));
        g->sink.state(g->sink.user, (short)s->number, (short)(uint16_t)maskNow(g), o.buttonFlags,
                      o.leftTrigger, o.rightTrigger, o.leftStickX, o.leftStickY,
                      o.rightStickX, o.rightStickY);
    }
    s->sent = *pk;
    s->haveSent = 1;
    g->states++;
}

static void sendRest(Gamepads* g, GpSlot* s) {
    PadOutPacket rest;
    memset(&rest, 0, sizeof(rest));
    sendPacket(g, s, &rest);
}

static int isRest(const PadOutPacket* pk) {
    PadOutPacket rest;
    memset(&rest, 0, sizeof(rest));
    return memcmp(pk, &rest, sizeof(rest)) == 0;
}

// The pad's battery, once per change, while it is on the host (moonlight-qt
// sends it at arrival and on SDL's battery event)
static void sendBattery(Gamepads* g, GpSlot* s) {
    if (!s->haveBattery || !live(g) || !s->announced || !(s->info.caps & LI_CCAP_BATTERY_STATE)) return;
    if (s->batterySent && s->batteryHost[0] == s->battery[0] && s->batteryHost[1] == s->battery[1]) return;
    s->batteryHost[0] = s->battery[0];
    s->batteryHost[1] = s->battery[1];
    s->batterySent = 1;
    if (g->extra.battery) g->extra.battery(g->extra.user, (uint8_t)s->number, s->battery[0], s->battery[1]);
}

// Motion types are LI_MOTION_TYPE_ACCEL (1) and _GYRO (2); the slot's arrays take 0 and 1
static int motionIndex(int motionType) {
    return motionType == LI_MOTION_TYPE_ACCEL ? 0 : (motionType == LI_MOTION_TYPE_GYRO ? 1 : -1);
}

static int motionTypeOf(int index) { return index ? LI_MOTION_TYPE_GYRO : LI_MOTION_TYPE_ACCEL; }

// A sensor runs only while the pad is on the host and the host asked for it
// (power: moonlight-common-c's own advice)
static void applySensors(Gamepads* g, GpSlot* s) {
    for (int t = 0; t < 2; t++) {
        int want = (live(g) && s->announced) ? s->motionHz[t] : 0;
        if (want == s->sensorHz[t]) continue;
        s->sensorHz[t] = want;
        s->haveMotion[t] = 0;
        if (g->haptics.setSensor) g->haptics.setSensor(g->haptics.user, s->src[0].id, motionTypeOf(t), want);
    }
}

// Fingers the host has down are let go (focus lost, pad or stream gone)
static void cancelTouches(Gamepads* g, GpSlot* s) {
    if (!s->touchDown) return;
    s->touchDown = 0;
    if (g->extra.touch) g->extra.touch(g->extra.user, (uint8_t)s->number, LI_TOUCH_EVENT_CANCEL_ALL, 0, 0, 0, 0);
}

static void announce(Gamepads* g, GpSlot* s) {
    atomic_fetch_or(&g->announcedMask, 1 << s->number);
    if (g->sink.arrival) {
        g->sink.arrival(g->sink.user, (uint8_t)s->number, (uint16_t)maskNow(g), s->info.type,
                        s->info.buttons, s->info.caps);
    }
    s->announced = 1;
    s->haveSent = 0;
    s->latched = gpIsChord(s->cur.buttonFlags); // a chord held while arriving is not a command
    g->arrivals++;
    // The first state whatever it is, as pad_out does
    if (s->latched) sendRest(g, s);
    else sendPacket(g, s, &s->cur);
    s->batterySent = 0;
    sendBattery(g, s);
}

// The host loses the pad: a last packet at rest with the mask that no longer
// has it
static void unannounce(Gamepads* g, GpSlot* s) {
    if (!s->announced) return;
    atomic_fetch_and(&g->announcedMask, ~(1 << s->number));
    sendRest(g, s);
    cancelTouches(g, s);
    s->announced = 0;
    s->haveSent = 0;
    s->latched = 0;
    s->batterySent = 0;
    g->removals++;
}

static void rumbleSlot(Gamepads* g, GpSlot* s, uint16_t low, uint16_t high) {
    if (!g->haptics.rumble) return;
    for (int k = 0; k < 1 + s->hasMirror; k++) g->haptics.rumble(g->haptics.user, s->src[k].id, low, high);
}

static void rumbleTriggersSlot(Gamepads* g, GpSlot* s, uint16_t left, uint16_t right) {
    if (!g->haptics.rumbleTriggers) return;
    for (int k = 0; k < 1 + s->hasMirror; k++) g->haptics.rumbleTriggers(g->haptics.user, s->src[k].id, left, right);
}

static void stopRumble(Gamepads* g, GpSlot* s) {
    rumbleSlot(g, s, 0, 0);
    if (s->info.caps & LI_CCAP_TRIGGER_RUMBLE) rumbleTriggersSlot(g, s, 0, 0);
}

// The slot a source id feeds, and which of its sources it is
static GpSlot* find(Gamepads* g, int id, int* which) {
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        if (!s->used) continue;
        for (int k = 0; k < 1 + s->hasMirror; k++)
            if (s->src[k].id == id) {
                if (which) *which = k;
                return s;
            }
    }
    return NULL;
}

static int stickMag(short x, short y) { return abs(x) + abs(y); }

// A pad that is being used: a button, a trigger past its dead zone or a stick
// out of its own
static int inUse(const PadOutPacket* pk) {
    return pk->buttonFlags != 0 || pk->leftTrigger > 12 || pk->rightTrigger > 12
        || stickMag(pk->leftStickX, pk->leftStickY) > 8000 || stickMag(pk->rightStickX, pk->rightStickY) > 8000;
}

// What the host sees of a pad with a Steam Input copy. Steam either leaves the
// real device alone (then the copy only echoes it, remapped and a little off:
// the real pad is the truth) or takes it over (the real pad goes silent and
// the copy carries the input). So: the real pad while it is in use, else the
// copy while that is, else the real pad at rest.
static void merge(GpSlot* s) {
    if (s->hasMirror && !inUse(&s->src[0].pk) && inUse(&s->src[1].pk)) s->cur = s->src[1].pk;
    else s->cur = s->src[0].pk;
}

static int lowestFree(const Gamepads* g) {
    int taken = g->frameNumber >= 0 ? 1 << g->frameNumber : 0;
    for (int i = 0; i < GP_MAX; i++)
        if (g->slot[i].used) taken |= 1 << g->slot[i].number;
    for (int n = 0; n < GP_MAX; n++)
        if (!(taken & (1 << n))) return n;
    return -1;
}

// ---- the source's side ----------------------------------------------------

int gpAdd(Gamepads* g, int id, const GpInfo* info) {
    pthread_mutex_lock(&g->lock);
    int result = -1;
    GpSlot* have = find(g, id, NULL);
    if (have) { // SDL can report the same pad twice in rare cases
        result = have->number;
        say(g, 'W', "gamepad: duplicate add for id %d (%s), ignored", id, info->name);
        goto done;
    }
    if (info->serial[0]) {
        for (int i = 0; i < GP_MAX; i++) {
            const GpSlot* o = &g->slot[i];
            if (o->used && !strcmp(o->info.serial, info->serial) && o->info.vendor == info->vendor
                && o->info.product == info->product) {
                say(g, 'I', "gamepad: %s (guid %s, serial %s) is the same pad as #%d (%s), skipped",
                    info->name, info->guid, info->serial, o->number, o->info.name);
                goto done;
            }
        }
    }
    // Steam Input's virtual copy and the pad it copies are one pad to the host,
    // whichever SDL lists first
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* o = &g->slot[i];
        if (!o->used || o->hasMirror) continue;
        if (info->mirror && !o->virtualOnly) {
            o->src[1] = (GpSource){ id, {0} };
            o->hasMirror = 1;
            merge(o);
            say(g, 'I', "gamepad: %s (guid %s) is Steam Input's copy of #%d (%s), merged into it", info->name,
                info->guid, o->number, o->info.name);
            result = o->number;
            goto done;
        }
        if (!info->mirror && o->virtualOnly) {
            // The real pad turns up after its copy: the pad takes the lead, the copy follows
            o->src[1] = o->src[0];
            o->src[0] = (GpSource){ id, {0} };
            o->hasMirror = 1;
            o->virtualOnly = 0;
            if (!o->announced) o->info = *info; // else the host keeps what it was told
            merge(o);
            say(g, 'I', "gamepad: %s (guid %s) is the real pad of #%d, whose Steam Input copy is merged into it",
                info->name, info->guid, o->number);
            result = o->number;
            goto done;
        }
    }
    int n = lowestFree(g);
    if (n < 0) {
        say(g, 'W', "gamepad: %s ignored, all %d numbers are taken", info->name, GP_MAX);
        goto done;
    }
    GpSlot* s = NULL;
    for (int i = 0; i < GP_MAX && !s; i++)
        if (!g->slot[i].used) s = &g->slot[i];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->src[0].id = id;
    s->virtualOnly = info->mirror != 0;
    s->number = n;
    s->info = *info;
    say(g, 'I', "gamepad #%d arrived: %s (VID/PID %04x/%04x, guid %s, type %u, caps 0x%x, buttons 0x%x, serial '%s'), mapping: %s",
        n, info->name, info->vendor, info->product, info->guid, info->type, info->caps, info->buttons,
        info->serial, info->mapping[0] ? info->mapping : "none");
    if (live(g)) announce(g, s);
    result = n;
done:
    pthread_mutex_unlock(&g->lock);
    return result;
}

// The slot's merged state changed or may have: send it, or handle the chord
static void push(Gamepads* g, GpSlot* s, void (**fire)(void*), void** fireUser) {
    const PadOutPacket* pk = &s->cur;
    if (!live(g) || !s->announced) return;
    if (s->latched) {
        // Let go of all four to arm it again; until then the host sees rest
        if (!(pk->buttonFlags & CHORD_BUTTONS)) s->latched = 0;
    } else if (g->chordEnabled && gpIsChord(pk->buttonFlags)) {
        s->latched = 1;
        g->chords++;
        if (!s->haveSent || !isRest(&s->sent)) sendRest(g, s);
        say(g, 'I', "gamepad #%d: Disconnected: Start+Select+L1+R1 (disconnect your session)", s->number);
        *fire = g->disconnect;
        *fireUser = g->disconnectUser;
    } else if (!s->haveSent || memcmp(pk, &s->sent, sizeof(*pk)) != 0) {
        sendPacket(g, s, pk);
    } else {
        g->drops++;
    }
}

void gpRemove(Gamepads* g, int id) {
    void (*fire)(void*) = NULL;
    void* fireUser = NULL;
    pthread_mutex_lock(&g->lock);
    int which = 0;
    GpSlot* s = find(g, id, &which);
    if (s && s->hasMirror) {
        // One of the two went: the other is the pad now
        say(g, 'I', "gamepad #%d: %s gone, %s stays", s->number, s->info.name, which == 0 ? "its Steam Input copy" : "the real pad");
        if (which == 0) {
            // The real pad took its sensors, touchpad and battery with it
            cancelTouches(g, s);
            memset(s->motionHz, 0, sizeof(s->motionHz));
            memset(s->sensorHz, 0, sizeof(s->sensorHz));
            memset(s->haveMotion, 0, sizeof(s->haveMotion));
            s->haveBattery = 0;
            s->src[0] = s->src[1];
            s->virtualOnly = 1;
        }
        s->hasMirror = 0;
        memset(&s->src[1], 0, sizeof(s->src[1]));
        merge(s);
        push(g, s, &fire, &fireUser);
    } else if (s) {
        say(g, 'I', "gamepad #%d gone: %s", s->number, s->info.name);
        unannounce(g, s);
        applySensors(g, s); // announced is 0: off, while the device can still be told
        s->used = 0;
    }
    pthread_mutex_unlock(&g->lock);
    if (fire) fire(fireUser);
}

void gpState(Gamepads* g, int id, const PadOutPacket* pk) {
    void (*fire)(void*) = NULL;
    void* fireUser = NULL;
    pthread_mutex_lock(&g->lock);
    int which = 0;
    GpSlot* s = find(g, id, &which);
    if (s) {
        s->src[which].pk = *pk;
        merge(s);
        push(g, s, &fire, &fireUser);
    }
    pthread_mutex_unlock(&g->lock);
    if (fire) fire(fireUser); // outside the lock: it takes the launcher's
}

// ---- the app's side -------------------------------------------------------

void gpSetActive(Gamepads* g, int active) {
    active = active != 0;
    pthread_mutex_lock(&g->lock);
    if (g->active != active) {
        g->active = active;
        if (g->streaming) {
            for (int i = 0; i < GP_MAX; i++) {
                GpSlot* s = &g->slot[i];
                if (!s->used) continue;
                if (active) {
                    if (!s->announced) announce(g, s);
                    else if (!s->latched && memcmp(&s->cur, &s->sent, sizeof(s->cur)) != 0) sendPacket(g, s, &s->cur);
                } else if (s->announced) {
                    // Focus lost: nothing stays pressed or rumbling on the host
                    if (!s->haveSent || !isRest(&s->sent)) sendRest(g, s);
                    stopRumble(g, s);
                    cancelTouches(g, s);
                }
                applySensors(g, s);
            }
        }
    }
    pthread_mutex_unlock(&g->lock);
}

void gpStreamStart(Gamepads* g) {
    pthread_mutex_lock(&g->lock);
    g->streaming = 1;
    atomic_store(&g->announcedMask, 0);
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        s->announced = s->haveSent = s->latched = 0;
        // A new host asks for its sensors again
        memset(s->motionHz, 0, sizeof(s->motionHz));
        s->touchDown = 0;
        s->batterySent = 0;
        if (s->used) applySensors(g, s);
    }
    if (g->active)
        for (int i = 0; i < GP_MAX; i++)
            if (g->slot[i].used) announce(g, &g->slot[i]);
    pthread_mutex_unlock(&g->lock);
}

void gpStreamStop(Gamepads* g) {
    pthread_mutex_lock(&g->lock);
    if (g->streaming) {
        for (int i = 0; i < GP_MAX; i++) {
            GpSlot* s = &g->slot[i];
            if (!s->used) continue;
            if (s->announced) stopRumble(g, s);
            unannounce(g, s);
            memset(s->motionHz, 0, sizeof(s->motionHz));
            applySensors(g, s);
        }
        g->streaming = 0;
    }
    pthread_mutex_unlock(&g->lock);
}

int gpClaimFramePad(Gamepads* g) {
    pthread_mutex_lock(&g->lock);
    if (g->frameNumber < 0) {
        int n = lowestFree(g);
        g->frameNumber = n < 0 ? 0 : n;
        say(g, 'I', "gamepad: gamepad mode's pad takes controller #%d", g->frameNumber);
    }
    int n = g->frameNumber;
    pthread_mutex_unlock(&g->lock);
    return n;
}

void gpReleaseFramePad(Gamepads* g) {
    pthread_mutex_lock(&g->lock);
    g->frameNumber = -1;
    pthread_mutex_unlock(&g->lock);
}

int gpAnnouncedMask(const Gamepads* g) { return atomic_load(&g->announcedMask) & 0xffff; }

int gpRumble(Gamepads* g, unsigned short number, unsigned short low, unsigned short high) {
    int mine = 0;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        if (!s->used || s->number != number) continue;
        mine = 1;
        if (live(g) || (low == 0 && high == 0)) rumbleSlot(g, s, low, high);
        break;
    }
    pthread_mutex_unlock(&g->lock);
    return mine;
}

int gpRumbleTriggers(Gamepads* g, unsigned short number, unsigned short left, unsigned short right) {
    int mine = 0;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        if (!s->used || s->number != number) continue;
        mine = 1;
        if (live(g) || (left == 0 && right == 0)) rumbleTriggersSlot(g, s, left, right);
        break;
    }
    pthread_mutex_unlock(&g->lock);
    return mine;
}

int gpMotionState(Gamepads* g, unsigned short number, unsigned char motionType, unsigned short rateHz) {
    int mine = 0;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        if (!s->used || s->number != number) continue;
        mine = 1;
        int t = motionIndex(motionType);
        if (t < 0 || !(s->info.caps & (t ? LI_CCAP_GYRO : LI_CCAP_ACCEL))) {
            say(g, 'I', "gamepad #%d: the host asked for motion type %u at %u Hz, which this pad does not have, ignored",
                s->number, (unsigned)motionType, (unsigned)rateHz);
            break;
        }
        say(g, 'I', "gamepad #%d: the host %s the %s at %u Hz", s->number, rateHz ? "asks for" : "stops",
            t ? "gyroscope" : "accelerometer", (unsigned)rateHz);
        s->motionHz[t] = rateHz;
        applySensors(g, s);
        break;
    }
    pthread_mutex_unlock(&g->lock);
    return mine;
}

int gpSetLed(Gamepads* g, unsigned short number, uint8_t red, uint8_t green, uint8_t blue) {
    int mine = 0;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < GP_MAX; i++) {
        GpSlot* s = &g->slot[i];
        if (!s->used || s->number != number) continue;
        mine = 1;
        if (live(g) && s->announced && (s->info.caps & LI_CCAP_RGB_LED) && g->haptics.setLed)
            g->haptics.setLed(g->haptics.user, s->src[0].id, red, green, blue);
        break;
    }
    pthread_mutex_unlock(&g->lock);
    return mine;
}

// SDL's gyroscope is rad/s, Moonlight's deg/s (moonlight-qt's constant)
#define RAD_TO_DEG 57.2957795f

void gpMotion(Gamepads* g, int id, int motionType, float x, float y, float z, uint32_t timestampMs) {
    pthread_mutex_lock(&g->lock);
    int which = 0;
    GpSlot* s = find(g, id, &which);
    int t = motionIndex(motionType);
    // Only the real pad has sensors: its Steam Input copy has none
    if (s && which == 0 && t >= 0 && live(g) && s->announced && s->sensorHz[t] > 0) {
        uint32_t period = 1000u / (uint32_t)s->sensorHz[t];
        if (period == 0) period = 1;
        const float v[3] = { x, y, z };
        int due = !s->haveMotion[t] || (int32_t)(timestampMs - (s->motionMs[t] + period)) >= 0;
        if (due && !(s->haveMotion[t] && memcmp(v, s->lastMotion[t], sizeof(v)) == 0)) {
            memcpy(s->lastMotion[t], v, sizeof(v));
            s->motionMs[t] = timestampMs;
            s->haveMotion[t] = 1;
            float k = t ? RAD_TO_DEG : 1.0f;
            if (g->extra.motion)
                g->extra.motion(g->extra.user, (uint8_t)s->number, (uint8_t)motionType, x * k, y * k, z * k);
            g->motions++;
        } else {
            g->motionsDropped++;
        }
    }
    pthread_mutex_unlock(&g->lock);
}

void gpBattery(Gamepads* g, int id, uint8_t state, uint8_t percentage) {
    pthread_mutex_lock(&g->lock);
    int which = 0;
    GpSlot* s = find(g, id, &which);
    if (s && which == 0) {
        s->haveBattery = 1;
        s->battery[0] = state;
        s->battery[1] = percentage;
        sendBattery(g, s);
    }
    pthread_mutex_unlock(&g->lock);
}

void gpTouch(Gamepads* g, int id, int eventType, uint32_t finger, float x, float y, float pressure) {
    pthread_mutex_lock(&g->lock);
    int which = 0;
    GpSlot* s = find(g, id, &which);
    if (s && which == 0 && live(g) && s->announced && (s->info.caps & LI_CCAP_TOUCHPAD)) {
        if (finger < 32) {
            if (eventType == LI_TOUCH_EVENT_DOWN) s->touchDown |= 1u << finger;
            else if (eventType == LI_TOUCH_EVENT_UP || eventType == LI_TOUCH_EVENT_CANCEL) s->touchDown &= ~(1u << finger);
        }
        if (g->extra.touch)
            g->extra.touch(g->extra.user, (uint8_t)s->number, (uint8_t)eventType, finger, x, y, pressure);
    }
    pthread_mutex_unlock(&g->lock);
}

// ---- the one instance -------------------------------------------------------

static Gamepads gGlobal;

Gamepads* gamepadsGlobal(void) { return &gGlobal; }

void gamepadsInit(const PadOutSink* sink, const GpHaptics* haptics, GpLog log, void* logUser) {
    gpInit(&gGlobal, sink, haptics, log, logUser);
}

int gamepadsReady(void) { return gGlobal.ready; }

void gamepadsSetDisconnectHook(void (*fn)(void*), void* user) {
    if (gGlobal.ready) gpSetDisconnectHook(&gGlobal, fn, user);
}

void gamepadsSetActive(int active) {
    if (gGlobal.ready) gpSetActive(&gGlobal, active);
}

void gamepadsStreamStart(void) {
    if (gGlobal.ready) gpStreamStart(&gGlobal);
}

void gamepadsStreamStop(void) {
    if (gGlobal.ready) gpStreamStop(&gGlobal);
}

int gamepadsClaimFramePad(void) {
    return gGlobal.ready ? gpClaimFramePad(&gGlobal) : 0;
}

void gamepadsReleaseFramePad(void) {
    if (gGlobal.ready) gpReleaseFramePad(&gGlobal);
}

int gamepadsAnnouncedMask(void) {
    return gGlobal.ready ? gpAnnouncedMask(&gGlobal) : 0;
}

void gamepadsHostMotionState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz) {
    // Gamepad mode's pad is not one of ours and has no sensors: the request is dropped
    if (gGlobal.ready) gpMotionState(&gGlobal, controllerNumber, motionType, reportRateHz);
}

void gamepadsHostSetLed(uint16_t controllerNumber, uint8_t red, uint8_t green, uint8_t blue) {
    if (gGlobal.ready) gpSetLed(&gGlobal, controllerNumber, red, green, blue);
}

void gamepadsHostRumble(unsigned short controllerNumber, unsigned short lowFreqMotor,
                        unsigned short highFreqMotor) {
    if (atomic_load(&gRumbleOff) && (lowFreqMotor || highFreqMotor)) return; // Native (lane artemis-settings)
    if (gGlobal.ready && gpRumble(&gGlobal, controllerNumber, lowFreqMotor, highFreqMotor)) return;
    rumbleHostCallback(controllerNumber, lowFreqMotor, highFreqMotor);
}

void gamepadsHostRumbleTriggers(uint16_t controllerNumber, uint16_t leftTriggerMotor,
                                uint16_t rightTriggerMotor) {
    if (atomic_load(&gRumbleOff) && (leftTriggerMotor || rightTriggerMotor)) return; // Native (lane artemis-settings)
    if (gGlobal.ready && gpRumbleTriggers(&gGlobal, controllerNumber, leftTriggerMotor, rightTriggerMotor)) return;
    rumbleTriggersCallback(controllerNumber, leftTriggerMotor, rightTriggerMotor);
}
