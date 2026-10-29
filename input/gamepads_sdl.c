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
#include "gamepads_sdl.h"
#include "../xr/mlxr_log.h"

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

// ---- pad description (gamepads_sdl_info.c, shared with the probe) ------------------

// Pads that are not for the host: the Frame's own controllers (SteamVR reads
// those), and anything in MLXR_GAMEPAD_IGNORE (comma separated GUIDs)
static int ignored(const GpInfo* info) {
    return gpIgnoreReason(info, getenv("MLXR_GAMEPAD_IGNORE")) != 0;
}

// ---- state ---------------------------------------------------------------------------

static void sendState(SDL_JoystickID id) {
    SDL_GameController* c = SDL_GameControllerFromInstanceID(id);
    if (!c) return;
    PadOutPacket pk;
    gpSdlReadPacket(c, &pk);
    gpState(gamepadsGlobal(), id, &pk);
}

static void mappingsLoaded(void* user, const char* path, int count) {
    (void)user;
    MLOG_I("gamepad: %d mappings from %s", count, path);
}

static void loadMappings(void) { gpSdlLoadMappings(mappingsLoaded, NULL); }

static void deviceAdded(int index) {
    SDL_GameController* c = SDL_GameControllerOpen(index);
    if (!c) {
        MLOG_W("gamepad: could not open device %d: %s", index, SDL_GetError());
        return;
    }
    GpInfo info;
    gpSdlDescribe(c, &info);
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
    gpSdlApplyHints();
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
