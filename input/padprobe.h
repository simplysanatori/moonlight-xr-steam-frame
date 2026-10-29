// The pad probe's core (mlxr-gamepad-probe, input/gamepad_probe.c): what the fork makes of
// each SDL device, as text. No SDL here, so the tests feed it made-up devices.
//
// The SDL side fills a PadProbeDev with what SDL says (gpSdlDescribe for the GpInfo, sensors,
// touchpads...) and hands it to padProbeAdd(), which asks the same code the app asks: the
// ignore rules (gpIgnoreReason) and gpAdd() on a throwaway Gamepads for the controller
// number and the Steam Input copy merge. padProbeFormat() then prints the result. GPLv3.
#ifndef MLXR_PADPROBE_H
#define MLXR_PADPROBE_H

#include <stddef.h>
#include <stdint.h>

#include "gamepads.h"

#define PP_MAX 32

typedef struct {
    int used;
    // What SDL says
    int index;           // device index when it was seen
    int id;              // instance id: the key for padProbeRemove and the label in the output
    int isController;    // has a game controller mapping (else a bare joystick)
    char sdlType[32];    // SDL_GameControllerGetType, by name
    char path[128];
    char battery[24];    // SDL_JoystickCurrentPowerLevel, by name
    uint16_t version;
    int axes, buttons, hats;
    int hasGyro, hasAccel;
    float gyroHz, accelHz; // SDL_GameControllerGetSensorDataRate, 0 when unknown
    int touchpads;
    int hasLed;
    int playerIndex;     // -1 none
    // The app's own description (gpSdlDescribe + gpClassify): name, ids, GUID, serial, mapping,
    // LI_CTYPE_, buttons, caps (analogue triggers, rumble, trigger rumble), mirror
    GpInfo info;
    // Filled by padProbeAdd
    int ignored;         // gpIgnoreReason: 0 for the host, 1 Frame controller, 2 MLXR_GAMEPAD_IGNORE
    int number;          // controller number gpAdd gave it, -1 when none
    int partner;         // PadProbe.dev slot of the Steam Input copy / the real pad it merged with, -1
    int dupOf;           // slot of the pad it duplicates by serial, -1
} PadProbeDev;

typedef struct {
    Gamepads gp;
    PadProbeDev dev[PP_MAX];
} PadProbe;

void padProbeInit(PadProbe* p);
void padProbeDestroy(PadProbe* p);
// Classifies a device the way the app's source does and adds it. ignoreList is the
// MLXR_GAMEPAD_IGNORE value (NULL for none). Returns the stored copy, NULL when full
PadProbeDev* padProbeAdd(PadProbe* p, const PadProbeDev* facts, const char* ignoreList);
// The device is gone. Copies it to *out (when not NULL) first. Returns 0 when unknown
int padProbeRemove(PadProbe* p, int id, PadProbeDev* out);
PadProbeDev* padProbeFind(PadProbe* p, int id);

// One device as a block of text (several lines, newline ended). Returns the length written
// (truncated at cap - 1)
size_t padProbeFormat(const PadProbe* p, const PadProbeDev* d, char* out, size_t cap);
// What the host would get: one line per controller number, then the devices the app skips or
// does not use
size_t padProbeSummary(const PadProbe* p, char* out, size_t cap);

// "A+LB+Start", "-" for none (Limelight button flags)
size_t padProbeButtons(int flags, char* out, size_t cap);
// "LI_CCAP" bits as words: "analog-triggers rumble ...", "none"
size_t padProbeCaps(unsigned caps, char* out, size_t cap);
const char* padProbeTypeName(unsigned type);
// "   12.345 [3] btn a down            btn=A lt=0 rt=0 L=0,0 R=0,0": the line --watch prints
// per input event. what is the event in words
size_t padProbeStateLine(char* out, size_t cap, double t, int id, const char* what, const PadOutPacket* pk);
// "   12.345 [3] gyro  x=0.012 y=-0.003 z=0.001 rad/s"
size_t padProbeSensorLine(char* out, size_t cap, double t, int id, const char* sensor, const float v[3]);

#endif
