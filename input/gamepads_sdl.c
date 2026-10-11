// The SDL2 game controller source for input/gamepads.c: hotplug, mappings,
// state and rumble for every pad SDL enumerates. GPLv3.
//
// SDL is used without a window: events come from its own thread here, with
// background events allowed (the app is a VR client, nothing is "focused" to
// SDL). SDL reads the pads' evdev/hidraw nodes without grabbing them, so a pad
// also reaches whatever else listens to it (Steam Input in Game Mode); unlike
// the keyboard there is no exclusive grab, because a grab would also take the
// events away from SDL's own handle.
#include <SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "Limelight.h"
#include "gamepads.h"
#include "../xr/mlxr_log.h"

// SDL_GameControllerButton order -> Moonlight flags (moonlight-qt k_ButtonMap)
static const int kButtonMap[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    BACK_FLAG, SPECIAL_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    LB_FLAG, RB_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    MISC_FLAG,
    PADDLE1_FLAG, PADDLE2_FLAG, PADDLE3_FLAG, PADDLE4_FLAG,
    TOUCHPAD_FLAG,
};
#define BUTTONS ((int)(sizeof(kButtonMap) / sizeof(kButtonMap[0])))

static struct {
    pthread_t thread;
    int started;
    atomic_int stop;
} g;

static void logHook(void* user, char level, const char* line) {
    (void)user;
    mlxr_logf(level, "%s", line);
}

// ---- rumble (called by gamepads.c under its lock) -------------------------------

static void hapRumble(void* user, int id, uint16_t low, uint16_t high) {
    (void)user;
    SDL_GameController* c = SDL_GameControllerFromInstanceID(id);
    if (c) SDL_GameControllerRumble(c, low, high, 30000);
}

static void hapTriggers(void* user, int id, uint16_t left, uint16_t right) {
    (void)user;
    SDL_GameController* c = SDL_GameControllerFromInstanceID(id);
    if (c) SDL_GameControllerRumbleTriggers(c, left, right, 30000);
}

// ---- pad description --------------------------------------------------------------

static int steamControllerPid(unsigned pid) {
    // From SDL's controller_list.h (as moonlight-qt)
    switch (pid) {
    case 0x1101: case 0x1102: case 0x1105: case 0x1106: case 0x1142: case 0x1201:
    case 0x1202: case 0x1205: case 0x1302: case 0x1303: case 0x1304: case 0x1305:
        return 1;
    default:
        return 0;
    }
}

static uint8_t ctype(SDL_GameController* c, unsigned vendor, unsigned product) {
    switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE:
        return LI_CTYPE_XBOX;
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5:
        return LI_CTYPE_PS;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return LI_CTYPE_NINTENDO;
    default:
        return (vendor == 0x28de && steamControllerPid(product)) ? LI_CTYPE_STEAM : LI_CTYPE_UNKNOWN;
    }
}

static void describe(SDL_GameController* c, GpInfo* info) {
    memset(info, 0, sizeof(*info));
    const char* name = SDL_GameControllerName(c);
    snprintf(info->name, sizeof(info->name), "%s", name ? name : "<unnamed>");
    SDL_Joystick* js = SDL_GameControllerGetJoystick(c);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), info->guid, sizeof(info->guid));
    const char* serial = SDL_GameControllerGetSerial(c);
    if (serial) snprintf(info->serial, sizeof(info->serial), "%s", serial);
    char* mapping = SDL_GameControllerMapping(c);
    if (mapping) {
        snprintf(info->mapping, sizeof(info->mapping), "%s", mapping);
        SDL_free(mapping);
    }
    info->vendor = SDL_GameControllerGetVendor(c);
    info->product = SDL_GameControllerGetProduct(c);
    info->type = ctype(c, info->vendor, info->product);
    // Steam Input's virtual copy of a pad (SDL lists it as 28de:11ff)
    info->mirror = info->vendor == 0x28de && info->product == 0x11ff;
    for (int i = 0; i < BUTTONS; i++)
        if (SDL_GameControllerHasButton(c, (SDL_GameControllerButton)i)) info->buttons |= (uint32_t)kButtonMap[i];
    if (SDL_GameControllerGetBindForAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS
        || SDL_GameControllerGetBindForAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS)
        info->caps |= LI_CCAP_ANALOG_TRIGGERS; // as moonlight-qt: an axis binding means analogue
    if (SDL_GameControllerHasRumble(c)) info->caps |= LI_CCAP_RUMBLE;
    if (SDL_GameControllerHasRumbleTriggers(c)) info->caps |= LI_CCAP_TRIGGER_RUMBLE;
}

// Pads that are not for the host: the Frame's own controllers (SteamVR reads
// those), and anything in MLXR_GAMEPAD_IGNORE (comma separated GUIDs)
static int ignored(const GpInfo* info) {
    if (info->vendor == 0x28de && (strcasestr(info->name, "frame") != NULL)) return 1;
    const char* env = getenv("MLXR_GAMEPAD_IGNORE");
    if (env && *env) {
        char* copy = strdup(env);
        int hit = 0;
        for (char* save = NULL, *t = strtok_r(copy, ", ", &save); t && !hit; t = strtok_r(NULL, ", ", &save))
            hit = strcasecmp(t, info->guid) == 0;
        free(copy);
        return hit;
    }
    return 0;
}

// ---- state ---------------------------------------------------------------------------

static short stickY(short v) { return (short)-(v < -32767 ? -32767 : v); } // as moonlight-qt

static void readPacket(SDL_GameController* c, PadOutPacket* pk) {
    memset(pk, 0, sizeof(*pk));
    for (int i = 0; i < BUTTONS; i++)
        if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)i)) pk->buttonFlags |= kButtonMap[i];
    pk->leftStickX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    pk->leftStickY = stickY(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    pk->rightStickX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
    pk->rightStickY = stickY(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    int lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    int rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    pk->leftTrigger = (unsigned char)(lt < 0 ? 0 : lt * 255L / 32767);
    pk->rightTrigger = (unsigned char)(rt < 0 ? 0 : rt * 255L / 32767);
}

static void sendState(SDL_JoystickID id) {
    SDL_GameController* c = SDL_GameControllerFromInstanceID(id);
    if (!c) return;
    PadOutPacket pk;
    readPacket(c, &pk);
    gpState(gamepadsGlobal(), id, &pk);
}

static void loadMappings(void) {
    const char* home = getenv("HOME");
    const char* env = getenv("MLXR_GAMECONTROLLERDB");
    char paths[3][512];
    int n = 0;
    if (env && *env) snprintf(paths[n++], sizeof(paths[0]), "%s", env);
    if (home) {
        // The flat launcher's (moonlight-qt's MappingManager) cached database, then the client's own dir
        snprintf(paths[n++], sizeof(paths[0]), "%s/.cache/Moonlight XR Native/Moonlight XR Native/gamecontrollerdb.txt", home);
        snprintf(paths[n++], sizeof(paths[0]), "%s/.config/moonlight-xr/gamecontrollerdb.txt", home);
    }
    for (int i = 0; i < n; i++) {
        int r = SDL_GameControllerAddMappingsFromFile(paths[i]);
        if (r >= 0) MLOG_I("gamepad: %d mappings from %s", r, paths[i]);
    }
}

static void deviceAdded(int index) {
    SDL_GameController* c = SDL_GameControllerOpen(index);
    if (!c) {
        MLOG_W("gamepad: could not open device %d: %s", index, SDL_GetError());
        return;
    }
    GpInfo info;
    describe(c, &info);
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
    if (ignored(&info)) {
        MLOG_I("gamepad: %s (guid %s) is not for the host, ignored", info.name, info.guid);
        SDL_GameControllerClose(c);
        return;
    }
    int n = gpAdd(gamepadsGlobal(), id, &info);
    if (n < 0) {
        SDL_GameControllerClose(c);
        return;
    }
    SDL_GameControllerSetPlayerIndex(c, n); // the Xbox 360 ring, a DualSense's player LEDs
    sendState(id);
}

static void deviceRemoved(SDL_JoystickID id) {
    // Before the close: rumble holds gamepads.c's lock while it uses the handle
    gpRemove(gamepadsGlobal(), id);
    SDL_GameController* c = SDL_GameControllerFromInstanceID(id);
    if (c) SDL_GameControllerClose(c);
}

static void* run(void* arg) {
    (void)arg;
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"); // SIGTERM must still end the app
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); // no window
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0"); // positional A/B/X/Y, as moonlight-qt
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        MLOG_W("gamepad: SDL could not start (%s), no physical gamepads", SDL_GetError());
        return NULL;
    }
    SDL_version v;
    SDL_GetVersion(&v);
    MLOG_I("gamepad: SDL %d.%d.%d game controllers ready", v.major, v.minor, v.patch);
    loadMappings();

    int dirty[GP_MAX * 4];
    struct timespec lastStats;
    clock_gettime(CLOCK_MONOTONIC, &lastStats);
    long lastStates = 0;
    SDL_Event ev;
    while (!atomic_load(&g.stop)) {
        int nd = 0;
        int got = SDL_WaitEventTimeout(&ev, 250);
        while (got) {
            switch (ev.type) {
            case SDL_CONTROLLERDEVICEADDED:
                deviceAdded(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                deviceRemoved(ev.cdevice.which);
                break;
            case SDL_CONTROLLERAXISMOTION:
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                int id = ev.type == SDL_CONTROLLERAXISMOTION ? ev.caxis.which : ev.cbutton.which;
                int seen = 0;
                for (int i = 0; i < nd; i++) seen |= dirty[i] == id;
                if (!seen && nd < (int)(sizeof(dirty) / sizeof(dirty[0]))) dirty[nd++] = id;
                break;
            }
            default:
                break;
            }
            got = SDL_PollEvent(&ev); // a burst of axis events is one state
        }
        for (int i = 0; i < nd; i++) sendState(dirty[i]);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - lastStats.tv_sec >= 5) { // one line per 5 s, only while pads are talking
            lastStats = now;
            Gamepads* gp = gamepadsGlobal();
            pthread_mutex_lock(&gp->lock);
            long states = gp->states, drops = gp->drops;
            int mask = atomic_load(&gp->announcedMask);
            pthread_mutex_unlock(&gp->lock);
            if (states != lastStates) {
                MLOG_I("gamepad: %ld packets in 5 s (%ld unchanged dropped so far), host mask 0x%x", states - lastStates, drops, mask);
                lastStates = states;
            }
        }
    }
    // Pads still open go away with SDL; the stream's own stop has taken them off the host
    SDL_Quit();
    return NULL;
}

int gamepadsStart(void (*disconnect)(void*), void* user) {
    const char* off = getenv("MLXR_GAMEPADS");
    if (off && !strcmp(off, "0")) {
        MLOG_I("gamepad: physical gamepads off (MLXR_GAMEPADS=0)");
        return -1;
    }
    PadOutSink sink;
    padOutUseLimelight(&sink);
    GpHaptics hap = { hapRumble, hapTriggers, NULL };
    gamepadsInit(&sink, &hap, logHook, NULL);
    gamepadsSetDisconnectHook(disconnect, user);
    const char* quit = getenv("NO_GAMEPAD_QUIT"); // moonlight-qt's switch
    if (quit && !strcmp(quit, "1")) gpSetChordEnabled(gamepadsGlobal(), 0);
    atomic_store(&g.stop, 0);
    if (pthread_create(&g.thread, NULL, run, NULL) != 0) return -1;
    g.started = 1;
    return 0;
}

void gamepadsStop(void) {
    if (!g.started) return;
    atomic_store(&g.stop, 1);
    pthread_join(g.thread, NULL);
    g.started = 0;
}
