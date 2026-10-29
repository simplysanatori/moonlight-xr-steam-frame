// mlxr-gamepad-probe: lists every SDL game controller and joystick the way the VR client's
// physical gamepad source sees it (type, ids, Steam Input copy, sensors, rumble, touchpads...),
// and with --watch prints hotplug events and one line per input event, so a certification pass
// on the headset can be captured to a file (docs/dev/GAMEPAD_CERT.md). GPLv3.
//
// The classification is the app's own code (input/gamepads.c, gamepads_sdl_info.c); the text is
// input/padprobe.c, which the tests cover. Nothing is sent anywhere, no stream is needed.
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Limelight.h"
#include "gamepads_sdl.h"
#include "padprobe.h"

static volatile sig_atomic_t gStop;

static void onSignal(int sig) {
    (void)sig;
    gStop = 1;
}

static double nowSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double gT0;
static double since(void) { return nowSec() - gT0; }

static struct {
    int watch;
    double seconds; // 0: until Ctrl-C
    int sensorHz;   // lines per second per pad and sensor, 0: none
    int axisHz;     // axis lines per second per pad, 0: every event
    int rumble;
    int noMappings;
} opt = { 0, 0.0, 4, 20, 0, 0 };

// What goes with a PadProbe slot: the open handles and the watch's throttles
typedef struct {
    SDL_Joystick* js;
    SDL_GameController* c;
    double lastAxis;
    int axisPending;
    char axisWhat[64];
    double lastSensor[2];
    char battery[24];
    double lastBatteryPoll;
} Rt;

static PadProbe gProbe;
static Rt gRt[PP_MAX];

static const char* batteryName(SDL_JoystickPowerLevel level) {
    switch (level) {
    case SDL_JOYSTICK_POWER_UNKNOWN: return "unknown";
    case SDL_JOYSTICK_POWER_EMPTY: return "empty (5% or less)";
    case SDL_JOYSTICK_POWER_LOW: return "low (20% or less)";
    case SDL_JOYSTICK_POWER_MEDIUM: return "medium (70% or less)";
    case SDL_JOYSTICK_POWER_FULL: return "full (100% or less)";
    case SDL_JOYSTICK_POWER_WIRED: return "wired";
    default: return "n/a";
    }
}

static void usage(FILE* f) {
    fprintf(f,
        "usage: mlxr-gamepad-probe [options]\n"
        "Lists every SDL joystick and game controller as the VR client's gamepad source sees it:\n"
        "index, name, SDL type, vendor:product, serial, the Steam Virtual Gamepad copy (28de:11ff)\n"
        "and the real pad it merges with, sensors, rumble, touchpads, LED, battery, and what the\n"
        "fork makes of it (type, capabilities, controller number, skipped or not).\n"
        "  --watch          keep running: hotplug events, one line per input event (button, axis,\n"
        "                   touchpad) with the state the host would get, and sensor samples\n"
        "  --seconds N      with --watch: stop after N seconds (default: until Ctrl-C)\n"
        "  --sensor-hz N    sensor lines per second per pad and sensor (default 4, 0 = none)\n"
        "  --axis-hz N      axis lines per second per pad (default 20, 0 = every event);\n"
        "                   buttons are always printed\n"
        "  --rumble         pulse the rumble and trigger rumble of every pad that has them\n"
        "  --no-mappings    do not load the gamecontrollerdb files the client reads\n"
        "  -h, --help       this text\n"
        "Environment the client honours and the probe follows: MLXR_GAMEPAD_IGNORE (GUIDs to skip),\n"
        "MLXR_GAMECONTROLLERDB, SDL_GAMECONTROLLERCONFIG, SDL_JOYSTICK_HIDAPI*, SDL_GAMECONTROLLER_IGNORE_DEVICES.\n"
        "Exit status: 0 done, 1 SDL could not start, 2 bad arguments.\n");
}

static int parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--watch")) opt.watch = 1;
        else if (!strcmp(a, "--rumble")) opt.rumble = 1;
        else if (!strcmp(a, "--no-mappings")) opt.noMappings = 1;
        else if ((!strcmp(a, "--seconds") || !strcmp(a, "--sensor-hz") || !strcmp(a, "--axis-hz")) && i + 1 < argc) {
            char* end = NULL;
            double v = strtod(argv[++i], &end);
            if (!end || *end || v < 0) {
                fprintf(stderr, "mlxr-gamepad-probe: %s needs a number, 0 or more\n", a);
                return -1;
            }
            if (!strcmp(a, "--seconds")) opt.seconds = v;
            else if (!strcmp(a, "--sensor-hz")) opt.sensorHz = (int)v;
            else opt.axisHz = (int)v;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 1;
        } else {
            fprintf(stderr, "mlxr-gamepad-probe: unknown or incomplete option %s\n", a);
            usage(stderr);
            return -1;
        }
    }
    return 0;
}

// ---- devices ---------------------------------------------------------------------------------

static void closeRt(Rt* r) {
    if (r->c) SDL_GameControllerClose(r->c);
    else if (r->js) SDL_JoystickClose(r->js);
    memset(r, 0, sizeof(*r));
}

static PadProbeDev* addDevice(int index) {
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    if (id < 0 || padProbeFind(&gProbe, (int)id)) return NULL;
    PadProbeDev d;
    memset(&d, 0, sizeof(d));
    d.index = index;
    d.id = (int)id;
    d.playerIndex = -1;
    SDL_GameController* c = NULL;
    SDL_Joystick* js = NULL;
    if (SDL_IsGameController(index)) {
        c = SDL_GameControllerOpen(index);
        if (!c) {
            printf("device %d: could not open as a game controller: %s\n", index, SDL_GetError());
            return NULL;
        }
        d.isController = 1;
        gpSdlDescribe(c, &d.info); // what the app records about it
        snprintf(d.sdlType, sizeof(d.sdlType), "%s", gpSdlTypeName(SDL_GameControllerGetType(c)));
        js = SDL_GameControllerGetJoystick(c);
        d.hasGyro = SDL_GameControllerHasSensor(c, SDL_SENSOR_GYRO) == SDL_TRUE;
        d.hasAccel = SDL_GameControllerHasSensor(c, SDL_SENSOR_ACCEL) == SDL_TRUE;
#if SDL_VERSION_ATLEAST(2, 0, 16)
        if (d.hasGyro) d.gyroHz = SDL_GameControllerGetSensorDataRate(c, SDL_SENSOR_GYRO);
        if (d.hasAccel) d.accelHz = SDL_GameControllerGetSensorDataRate(c, SDL_SENSOR_ACCEL);
#endif
        d.touchpads = SDL_GameControllerGetNumTouchpads(c);
        d.hasLed = SDL_GameControllerHasLED(c) == SDL_TRUE;
#if SDL_VERSION_ATLEAST(2, 24, 0)
        const char* path = SDL_GameControllerPath(c);
        if (path) snprintf(d.path, sizeof(d.path), "%s", path);
#endif
    } else {
        js = SDL_JoystickOpen(index);
        if (!js) {
            printf("device %d: could not open as a joystick: %s\n", index, SDL_GetError());
            return NULL;
        }
        const char* name = SDL_JoystickName(js);
        snprintf(d.info.name, sizeof(d.info.name), "%s", name ? name : "<unnamed>");
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), d.info.guid, sizeof(d.info.guid));
        d.info.vendor = SDL_JoystickGetVendor(js);
        d.info.product = SDL_JoystickGetProduct(js);
        const char* serial = SDL_JoystickGetSerial(js);
        if (serial) snprintf(d.info.serial, sizeof(d.info.serial), "%s", serial);
#if SDL_VERSION_ATLEAST(2, 24, 0)
        const char* path = SDL_JoystickPath(js);
        if (path) snprintf(d.path, sizeof(d.path), "%s", path);
#endif
    }
    d.version = SDL_JoystickGetProductVersion(js);
    d.axes = SDL_JoystickNumAxes(js);
    d.buttons = SDL_JoystickNumButtons(js);
    d.hats = SDL_JoystickNumHats(js);
    d.playerIndex = SDL_JoystickGetPlayerIndex(js);
    snprintf(d.battery, sizeof(d.battery), "%s", batteryName(SDL_JoystickCurrentPowerLevel(js)));

    PadProbeDev* stored = padProbeAdd(&gProbe, &d, getenv("MLXR_GAMEPAD_IGNORE"));
    if (!stored) {
        printf("device %d: more than %d devices, ignored\n", index, PP_MAX);
        if (c) SDL_GameControllerClose(c);
        else SDL_JoystickClose(js);
        return NULL;
    }
    Rt* r = &gRt[stored - gProbe.dev];
    memset(r, 0, sizeof(*r));
    r->js = js;
    r->c = c;
    snprintf(r->battery, sizeof(r->battery), "%s", d.battery);
    r->lastBatteryPoll = nowSec();
    if (opt.watch && opt.sensorHz > 0 && c) { // SDL sends no sensor events until asked
        const SDL_SensorType types[2] = { SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL };
        const int has[2] = { d.hasGyro, d.hasAccel };
        for (int k = 0; k < 2; k++)
            if (has[k] && SDL_GameControllerSetSensorEnabled(c, types[k], SDL_TRUE) != 0)
                printf("%9.3f [%d] could not enable the %s: %s\n", since(), d.id, k ? "accelerometer" : "gyroscope", SDL_GetError());
    }
    return stored;
}

static void printDevice(const PadProbeDev* d) {
    char buf[8192];
    padProbeFormat(&gProbe, d, buf, sizeof(buf));
    fputs(buf, stdout);
}

static void printSummary(void) {
    char buf[8192];
    padProbeSummary(&gProbe, buf, sizeof(buf));
    fputs(buf, stdout);
}

static void removeDevice(int id) {
    PadProbeDev gone;
    PadProbeDev* d = padProbeFind(&gProbe, id);
    if (!d) return;
    Rt* r = &gRt[d - gProbe.dev];
    padProbeRemove(&gProbe, id, &gone);
    printf("%9.3f HOTPLUG removed [%d] %s (%04x:%04x)%s\n", since(), gone.id, gone.info.name, gone.info.vendor,
           gone.info.product, gone.number >= 0 ? ", was a host pad" : "");
    closeRt(r);
    printSummary();
}

// ---- header ----------------------------------------------------------------------------------

static void mappingsLoaded(void* user, const char* path, int count) {
    (void)user;
    printf("mappings    %d from %s\n", count, path);
}

static void printHeader(void) {
    SDL_version compiled, linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    time_t t = time(NULL);
    char when[64] = "";
    struct tm tmv;
    if (localtime_r(&t, &tmv)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
    printf("mlxr-gamepad-probe  %s\n", when);
    printf("sdl         runtime %d.%d.%d, compiled against %d.%d.%d, platform %s\n", linked.major, linked.minor,
           linked.patch, compiled.major, compiled.minor, compiled.patch, SDL_GetPlatform());
    static const char* const hints[] = {
        "SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_HIDAPI_STEAM", "SDL_JOYSTICK_HIDAPI_PS5", "SDL_JOYSTICK_HIDAPI_SWITCH",
        "SDL_JOYSTICK_HIDAPI_XBOX", "SDL_JOYSTICK_ENHANCED_REPORTS", "SDL_GAMECONTROLLER_IGNORE_DEVICES",
        "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT",
    };
    for (size_t i = 0; i < sizeof(hints) / sizeof(hints[0]); i++) {
        const char* v = SDL_GetHint(hints[i]);
        if (v && *v) printf("hint        %s=%s\n", hints[i], v);
    }
    const char* ignore = getenv("MLXR_GAMEPAD_IGNORE");
    if (ignore && *ignore) printf("env         MLXR_GAMEPAD_IGNORE=%s\n", ignore);
    if (getenv("SDL_GAMECONTROLLERCONFIG")) printf("env         SDL_GAMECONTROLLERCONFIG is set\n");
    if (!opt.noMappings) gpSdlLoadMappings(mappingsLoaded, NULL);
}

// ---- rumble ------------------------------------------------------------------------------------

static void rumbleResult(int rc) {
    if (rc == 0) printf("ok\n");
    else printf("FAILED: %s\n", SDL_GetError());
    fflush(stdout);
}

static void rumbleTest(void) {
    for (int i = 0; i < PP_MAX; i++) {
        const PadProbeDev* d = &gProbe.dev[i];
        SDL_GameController* c = gRt[i].c;
        if (!d->used || !c || d->ignored) continue;
        if (d->info.caps & LI_CCAP_RUMBLE) {
            printf("rumble      [%d] %s: low-frequency motor 0.6 s ... ", d->id, d->info.name);
            fflush(stdout);
            rumbleResult(SDL_GameControllerRumble(c, 0xC000, 0, 600));
            SDL_Delay(900);
            printf("rumble      [%d] %s: high-frequency motor 0.6 s ... ", d->id, d->info.name);
            fflush(stdout);
            rumbleResult(SDL_GameControllerRumble(c, 0, 0xC000, 600));
            SDL_Delay(900);
        } else {
            printf("rumble      [%d] %s: not supported\n", d->id, d->info.name);
        }
        if (d->info.caps & LI_CCAP_TRIGGER_RUMBLE) {
            printf("rumble      [%d] %s: left trigger motor 0.6 s ... ", d->id, d->info.name);
            fflush(stdout);
            rumbleResult(SDL_GameControllerRumbleTriggers(c, 0xC000, 0, 600));
            SDL_Delay(900);
            printf("rumble      [%d] %s: right trigger motor 0.6 s ... ", d->id, d->info.name);
            fflush(stdout);
            rumbleResult(SDL_GameControllerRumbleTriggers(c, 0, 0xC000, 600));
            SDL_Delay(900);
        } else {
            printf("rumble      [%d] %s: trigger rumble not supported\n", d->id, d->info.name);
        }
    }
}

// ---- watch ---------------------------------------------------------------------------------------

static void reportState(int slot, const char* what) {
    const PadProbeDev* d = &gProbe.dev[slot];
    PadOutPacket pk;
    char line[512];
    gpSdlReadPacket(gRt[slot].c, &pk);
    padProbeStateLine(line, sizeof(line), since(), d->id, what, &pk);
    puts(line);
}

static double axisInterval(void) { return opt.axisHz > 0 ? 1.0 / opt.axisHz : 0.0; }

static void axisEvent(int slot, const char* what) {
    Rt* r = &gRt[slot];
    double t = nowSec();
    if (t - r->lastAxis >= axisInterval()) {
        reportState(slot, what);
        r->lastAxis = t;
        r->axisPending = 0;
    } else {
        snprintf(r->axisWhat, sizeof(r->axisWhat), "%s", what);
        r->axisPending = 1;
    }
}

static int slotOf(int id) {
    PadProbeDev* d = padProbeFind(&gProbe, id);
    return d ? (int)(d - gProbe.dev) : -1;
}

static void deviceAdded(int index, int viaController) {
    PadProbeDev* d = addDevice(index);
    if (!d) return;
    printf("%9.3f HOTPLUG added%s\n", since(), viaController ? "" : " (joystick without a mapping)");
    printDevice(d);
    if (d->partner >= 0) printDevice(&gProbe.dev[d->partner]); // its pad now says it has a copy
    printSummary();
}

static void handleEvent(const SDL_Event* ev) {
    char what[96];
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
        deviceAdded(ev->cdevice.which, 1);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        removeDevice(ev->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMAPPED:
        printf("%9.3f [%d] mapping changed\n", since(), ev->cdevice.which);
        break;
    case SDL_JOYDEVICEADDED:
        if (!SDL_IsGameController(ev->jdevice.which)) deviceAdded(ev->jdevice.which, 0);
        break;
    case SDL_JOYDEVICEREMOVED: {
        int slot = slotOf(ev->jdevice.which);
        if (slot >= 0 && !gProbe.dev[slot].isController) removeDevice(ev->jdevice.which);
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int slot = slotOf(ev->cbutton.which);
        if (slot < 0) break;
        const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)ev->cbutton.button);
        snprintf(what, sizeof(what), "btn %s %s", n ? n : "?", ev->type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
        reportState(slot, what);
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        int slot = slotOf(ev->caxis.which);
        if (slot < 0) break;
        const char* n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)ev->caxis.axis);
        snprintf(what, sizeof(what), "axis %s %d", n ? n : "?", ev->caxis.value);
        axisEvent(slot, what);
        break;
    }
    case SDL_CONTROLLERSENSORUPDATE: {
        int slot = slotOf(ev->csensor.which);
        if (slot < 0 || opt.sensorHz <= 0) break;
        int k = ev->csensor.sensor == SDL_SENSOR_GYRO ? 0 : ev->csensor.sensor == SDL_SENSOR_ACCEL ? 1 : -1;
        if (k < 0) break;
        double t = nowSec();
        if (t - gRt[slot].lastSensor[k] < 1.0 / opt.sensorHz) break;
        gRt[slot].lastSensor[k] = t;
        char line[256];
        padProbeSensorLine(line, sizeof(line), since(), gProbe.dev[slot].id, k ? "accel" : "gyro", ev->csensor.data);
        puts(line);
        break;
    }
    case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADMOTION:
    case SDL_CONTROLLERTOUCHPADUP: {
        int slot = slotOf(ev->ctouchpad.which);
        if (slot < 0) break;
        const char* kind = ev->type == SDL_CONTROLLERTOUCHPADDOWN ? "down" : ev->type == SDL_CONTROLLERTOUCHPADUP ? "up" : "move";
        if (ev->type == SDL_CONTROLLERTOUCHPADMOTION) {
            double t = nowSec();
            if (t - gRt[slot].lastAxis < axisInterval()) break;
            gRt[slot].lastAxis = t;
        }
        printf("%9.3f [%d] touchpad %d finger %d %-4s x=%.3f y=%.3f pressure=%.2f\n", since(), gProbe.dev[slot].id,
               ev->ctouchpad.touchpad, ev->ctouchpad.finger, kind, (double)ev->ctouchpad.x, (double)ev->ctouchpad.y,
               (double)ev->ctouchpad.pressure);
        break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int slot = slotOf(ev->jbutton.which);
        if (slot < 0 || gProbe.dev[slot].isController) break;
        printf("%9.3f [%d] joystick button %d %s\n", since(), gProbe.dev[slot].id, ev->jbutton.button,
               ev->type == SDL_JOYBUTTONDOWN ? "down" : "up");
        break;
    }
    case SDL_JOYHATMOTION: {
        int slot = slotOf(ev->jhat.which);
        if (slot < 0 || gProbe.dev[slot].isController) break;
        printf("%9.3f [%d] joystick hat %d value 0x%x\n", since(), gProbe.dev[slot].id, ev->jhat.hat, ev->jhat.value);
        break;
    }
    case SDL_JOYAXISMOTION: {
        int slot = slotOf(ev->jaxis.which);
        if (slot < 0 || gProbe.dev[slot].isController) break;
        double t = nowSec();
        if (t - gRt[slot].lastAxis < axisInterval()) break;
        gRt[slot].lastAxis = t;
        printf("%9.3f [%d] joystick axis %d value %d\n", since(), gProbe.dev[slot].id, ev->jaxis.axis, ev->jaxis.value);
        break;
    }
    default:
        break;
    }
}

// The last axis value of a burst, and the battery now and then
static void housekeeping(void) {
    double t = nowSec();
    for (int i = 0; i < PP_MAX; i++) {
        Rt* r = &gRt[i];
        const PadProbeDev* d = &gProbe.dev[i];
        if (!d->used || !r->js) continue;
        if (r->axisPending && t - r->lastAxis >= axisInterval()) {
            reportState(i, r->axisWhat);
            r->lastAxis = t;
            r->axisPending = 0;
        }
        if (t - r->lastBatteryPoll >= 5.0) {
            r->lastBatteryPoll = t;
            const char* now = batteryName(SDL_JoystickCurrentPowerLevel(r->js));
            if (strcmp(now, r->battery) != 0) {
                printf("%9.3f [%d] battery %s -> %s\n", since(), d->id, r->battery, now);
                snprintf(r->battery, sizeof(r->battery), "%s", now);
            }
        }
    }
}

static void watch(void) {
    printf("%9.3f watching%s (Ctrl-C to stop)\n", since(), opt.seconds > 0 ? " for a fixed time" : "");
    fflush(stdout);
    while (!gStop && (opt.seconds <= 0 || since() < opt.seconds)) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 20)) {
            do {
                handleEvent(&ev);
            } while (SDL_PollEvent(&ev));
        }
        housekeeping();
    }
    printf("%9.3f stopped\n", since());
}

int main(int argc, char** argv) {
    int pr = parseArgs(argc, argv);
    if (pr != 0) return pr > 0 ? 0 : 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    gT0 = nowSec();

    gpSdlApplyHints(); // the hints the client starts SDL with
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "mlxr-gamepad-probe: SDL could not start: %s\n", SDL_GetError());
        return 1;
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    padProbeInit(&gProbe);
    printHeader();

    int n = SDL_NumJoysticks();
    printf("devices     %d SDL joystick device(s)\n\n", n);
    for (int i = 0; i < n; i++) addDevice(i);
    for (int i = 0; i < PP_MAX; i++) {
        if (!gProbe.dev[i].used) continue;
        printDevice(&gProbe.dev[i]);
        putchar('\n');
    }
    if (n == 0)
        printf("nothing listed: is the pad on and paired (Bluetooth) or plugged in (USB-C data cable, puck)?\n"
               "A Steam Controller that Steam Input has claimed only shows as the Steam Virtual Gamepad.\n");
    printSummary();
    if (opt.rumble) rumbleTest();
    if (opt.watch) watch();

    for (int i = 0; i < PP_MAX; i++) closeRt(&gRt[i]);
    padProbeDestroy(&gProbe);
    SDL_Quit();
    return 0;
}
