// Physical gamepads (Xbox, DualSense/DualShock, 8BitDo, Switch Pro, Steam's
// virtual pad...) connected to the headset, forwarded to the host as Moonlight
// gamepads while the VR client is focused and streaming, the way the physical
// keyboard is (stream/evdev_keyboard.c). Native addition, no upstream
// counterpart: the Android client has the phone's own pads, moonlight-qt's
// app/streaming/input/gamepad.cpp is the behaviour this follows (arrival with
// type/capabilities/active mask, state on change, removal as a last packet at
// rest with the mask that no longer has the pad, host rumble back to the pad,
// Start+Select+L1+R1 to disconnect).
//
// This file is the pure core: it knows nothing of SDL or evdev. A source
// (gamepads_sdl.c, or a test) reports pads with gpAdd/gpRemove/gpState from
// any one thread; moonlight-common-c's rumble callbacks arrive on another and
// reach the pad through GpHaptics. The sends are PadOutSink hooks, as in
// pad_out.h, so a test runs without a connection.
//
// Controller numbers are shared with gamepad mode (the Frame controllers as
// one Xbox pad, xr_pad_bridge / pad_out): both take the lowest number the other
// does not hold, and every packet either sends carries the mask of all the
// announced pads, so the host sees one consistent set. All of it is under one
// lock; the disconnect hook is called outside it.
#ifndef MLXR_GAMEPADS_H
#define MLXR_GAMEPADS_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "pad_out.h"

#define GP_MAX 16

// What a source says about a pad. type and caps are LI_CTYPE_ and LI_CCAP_
// values, buttons the Limelight flags of the buttons the pad has.
typedef struct {
    char name[96];
    char guid[40];
    // The device's serial/unique id when it has one; two pads with the same
    // non-empty serial, vendor and product are one physical pad seen twice
    char serial[64];
    // For the log only: the mapping string the pad was matched with
    char mapping[320];
    uint16_t vendor;
    uint16_t product;
    uint8_t type;
    uint32_t buttons;
    uint16_t caps;
    // Set by the source for Steam's "Steam Virtual Gamepad": Steam Input's copy
    // of a pad that SDL may also show directly. It is not a second pad: it
    // feeds the pad it copies (see gpAdd).
    int mirror;
} GpInfo;

// How rumble, sensors and the LED reach a pad, by the id its source gave it
// (called under the lock, so a source must remove a pad before it closes it).
// Any of them may be NULL.
//   setSensor: switch one of the pad's sensors (motionType LI_MOTION_TYPE_ACCEL
//     or _GYRO) on at rateHz, or off when rateHz is 0. Only called for a pad
//     whose caps say it has that sensor, and only while the pad is on the host.
//   setLed: the host's colour for the pad's LED
typedef struct {
    void (*rumble)(void* user, int id, uint16_t low, uint16_t high);
    void (*rumbleTriggers)(void* user, int id, uint16_t left, uint16_t right);
    void (*setSensor)(void* user, int id, int motionType, int rateHz);
    void (*setLed)(void* user, int id, uint8_t red, uint8_t green, uint8_t blue);
    void* user;
} GpHaptics;

// The sends beyond state, for what a pad reports besides its buttons: the
// arguments of LiSendControllerMotionEvent, LiSendControllerBatteryEvent and
// LiSendControllerTouchEvent. Optional (a test sets a recorder, the app
// gpUseLimelightExtras).
typedef struct {
    int (*motion)(void* user, uint8_t number, uint8_t motionType, float x, float y, float z);
    int (*battery)(void* user, uint8_t number, uint8_t state, uint8_t percentage);
    int (*touch)(void* user, uint8_t number, uint8_t eventType, uint32_t pointerId, float x, float y,
                 float pressure);
    void* user;
} GpExtraSink;

// What the source found out about a device, in plain flags, and the LI_CCAP_
// bits the pad advertises for it. The capability rule lives here (and is
// tested) rather than in the SDL source: moonlight-qt's gamepad.cpp.
typedef struct {
    int analogTriggers; // the mapping binds a trigger to an axis
    int rumble;
    int triggerRumble;
    int touchpads;      // how many (one advertises LI_CCAP_TOUCHPAD)
    int accel;
    int gyro;
    int battery;        // reports a power level (or the SDL is new enough to)
    int led;
} GpDeviceFacts;
uint16_t gpCapsFor(const GpDeviceFacts* f);

// The SDL_JoystickPowerLevel value (SDL_JOYSTICK_POWER_UNKNOWN is -1, EMPTY 0,
// LOW 1, MEDIUM 2, FULL 3, WIRED 4) as Moonlight's battery state and
// percentage, as moonlight-qt does (SDL cannot say more). Returns 0 for a
// value that is not reported.
int gpBatteryFromPowerLevel(int level, uint8_t* state, uint8_t* percentage);

// level 'I', 'W' or 'E', one formatted line
typedef void (*GpLog)(void* user, char level, const char* line);

// One SDL device (or other source handle) feeding a slot, with its last state
typedef struct {
    int id;
    PadOutPacket pk;
} GpSource;

typedef struct {
    int used;
    int number;
    GpSource src[2]; // src[0] the pad, src[1] its mirror (Steam's virtual copy) when hasMirror
    int hasMirror;
    int virtualOnly; // the slot has only a mirror-type device so far
    int announced;   // the host knows it
    int haveSent;
    int latched;     // the disconnect chord fired and its buttons are still down
    // Motion, index 0 accelerometer and 1 gyroscope (LI_MOTION_TYPE_ minus 1)
    int motionHz[2];      // what the host asked for
    int sensorHz[2];      // what the pad's sensor is set to now (0 off)
    int haveMotion[2];    // lastMotion holds a sample that was sent
    uint32_t motionMs[2]; // its timestamp
    float lastMotion[2][3];
    // Battery as the source last said it, and whether the host has it
    int haveBattery;
    uint8_t battery[2];   // state, percentage
    uint8_t batteryHost[2]; // what the host was last told
    int batterySent;
    uint32_t touchDown;   // fingers (by id, below 32) the host has down
    GpInfo info;
    PadOutPacket cur;  // the sources merged
    PadOutPacket sent;
} GpSlot;

typedef struct Gamepads {
    pthread_mutex_t lock;
    int ready;
    PadOutSink sink;
    GpHaptics haptics;
    GpExtraSink extra;
    GpLog log;
    void* logUser;
    void (*disconnect)(void*);
    void* disconnectUser;
    int chordEnabled;

    GpSlot slot[GP_MAX];
    int active;     // focused, streaming, connected: pads go to the host
    int streaming;  // a connection exists (between xrPadBridgeStart and Stop)
    int frameNumber; // the number gamepad mode's pad holds, -1 for none
    atomic_int announcedMask; // pads the host knows about

    // Counts, for logs and tests
    long arrivals, removals, states, drops, chords;
    long motions, motionsDropped;
    long statesLogged;
} Gamepads;

void gpInit(Gamepads* g, const PadOutSink* sink, const GpHaptics* haptics, GpLog log, void* logUser);
void gpDestroy(Gamepads* g);
// Where motion, battery and touchpad reports go (default: nowhere)
void gpSetExtraSink(Gamepads* g, const GpExtraSink* extra);
void gpSetDisconnectHook(Gamepads* g, void (*fn)(void*), void* user);
// Off to ignore Start+Select+L1+R1 (moonlight-qt's NO_GAMEPAD_QUIT)
void gpSetChordEnabled(Gamepads* g, int enabled);

// A pad appeared. Returns its controller number, or -1 when it is a duplicate
// of one already held (same non-empty serial, vendor and product) or there is
// no free number. id is the source's own handle for it. A mirror (info.mirror,
// Steam's virtual copy) joins the real pad that has none yet, whichever came
// first. The host sees the real pad's state while it is in use, else the copy's
// while that is (Steam Input takes a pad over by silencing the real device);
// a mirror with no real pad to join is a pad of its own.
int gpAdd(Gamepads* g, int id, const GpInfo* info);
// The pad is gone (unplugged, powered off): the host hears it if it knew it.
// A pad's mirror going leaves the pad; the pad going leaves the mirror.
void gpRemove(Gamepads* g, int id);
// The pad's whole state as it stands now. Sent when it changes, while active.
void gpState(Gamepads* g, int id, const PadOutPacket* pk);

// Focused and streaming: pads reach the host (arrival first). Inactive: every
// pad is put at rest on the host, nothing more is sent, rumble is stopped.
void gpSetActive(Gamepads* g, int active);
// A connection begins / ends. The end takes every announced pad out of the
// host first; a begin forgets what the previous host was told.
void gpStreamStart(Gamepads* g);
void gpStreamStop(Gamepads* g);

// Gamepad mode's pad: the number to plug in with (lowest free), and its release
int gpClaimFramePad(Gamepads* g);
void gpReleaseFramePad(Gamepads* g);
// Bits of the pads the host knows (for the Frame pad's mask)
int gpAnnouncedMask(const Gamepads* g);

// The host's rumble for a controller number. Returns 1 when a physical pad has
// that number (and it was handled), 0 when it is not one of ours.
int gpRumble(Gamepads* g, unsigned short number, unsigned short low, unsigned short high);
int gpRumbleTriggers(Gamepads* g, unsigned short number, unsigned short left, unsigned short right);

// The host's setMotionEventState: it wants the pad with this number to report
// motionType (LI_MOTION_TYPE_ACCEL or _GYRO) about rateHz times a second, 0 to
// stop. Returns 1 when a physical pad has that number (even if it has no such
// sensor, which is then ignored), 0 when it is not one of ours (the Frame's
// own pad never has motion). The sensor is switched on only while the pad is
// on the host; a stream's end, focus loss or the pad leaving switches it off,
// and a new stream starts with no requests (the host asks again).
int gpMotionState(Gamepads* g, unsigned short number, unsigned char motionType, unsigned short rateHz);
// The host's LED colour for a physical pad's number, as gpRumble
int gpSetLed(Gamepads* g, unsigned short number, uint8_t red, uint8_t green, uint8_t blue);

// A sensor sample from the source: SDL's units (accelerometer m/s^2, gyroscope
// rad/s, SDL's axes) and its timestamp in ms. Sent as motion on the pad's own
// number, gyroscope in deg/s, as moonlight-qt: at most one per 1000/rate ms,
// and not when the sample equals the last one sent. Dropped unless the host
// asked for it and the pad is on the host.
void gpMotion(Gamepads* g, int id, int motionType, float x, float y, float z, uint32_t timestampMs);
// The pad's battery (see gpBatteryFromPowerLevel): sent when it changes, and
// again at the next arrival
void gpBattery(Gamepads* g, int id, uint8_t state, uint8_t percentage);
// One touchpad finger event, eventType an LI_TOUCH_EVENT_ value (down, up,
// move) with SDL's coordinates 0..1. Only for a pad advertising a touchpad;
// fingers still down when the pad leaves the host are cancelled.
void gpTouch(Gamepads* g, int id, int eventType, uint32_t finger, float x, float y, float pressure);

// The packet's buttons exactly Start+Select+L1+R1
int gpIsChord(int buttonFlags);

// ---- classification (no SDL): what a pad is, from the facts a source has ----
// gamepads_sdl.c fills a GpInfo from SDL and finishes it with gpClassify();
// the pad probe (padprobe.h) and the tests do the same with made-up facts.

// SDL_GameControllerType, reduced to what the fork tells apart
typedef enum {
    GP_SDL_UNKNOWN = 0,
    GP_SDL_XBOX360,
    GP_SDL_XBOXONE,
    GP_SDL_PS3,
    GP_SDL_PS4,
    GP_SDL_PS5,
    GP_SDL_SWITCH_PRO,
    GP_SDL_JOYCON_LEFT,
    GP_SDL_JOYCON_RIGHT,
    GP_SDL_JOYCON_PAIR,
    GP_SDL_OTHER // an SDL type the fork does not map (Luna, Stadia, Shield, virtual...)
} GpSdlKind;

// Valve's Steam Controllers by product id (vendor 0x28de): the list moonlight-qt 6.2
// (app/streaming/input/gamepad.cpp) takes from SDL's controller_list.h. 0 when not one
int gpSteamControllerPid(unsigned product);
// What SDL's controller_list.h calls that product id, NULL when not a Steam Controller
const char* gpSteamControllerPidName(unsigned product);
// "Steam Virtual Gamepad", Steam Input's copy of a pad (28de:11ff)
int gpIsSteamVirtualGamepad(unsigned vendor, unsigned product);
// LI_CTYPE_ value for a pad: SDL's own type first, then the Steam Controller ids
uint8_t gpControllerType(GpSdlKind kind, unsigned vendor, unsigned product);
// Sets info->type and info->mirror from info->vendor, info->product and the SDL type
void gpClassify(GpInfo* info, GpSdlKind kind);

// Why a pad is not for the host: 0 it is, 1 SteamVR's own Frame controller (vendor 28de,
// name contains "frame"), 2 its GUID is in ignoreList (MLXR_GAMEPAD_IGNORE: GUIDs separated
// by commas or spaces; NULL for none)
int gpIgnoreReason(const GpInfo* info, const char* ignoreList);
const char* gpIgnoreReasonText(int reason);

// ---- the one instance the app uses ----------------------------------------
// Everything below is a no-op until gamepadsInit() has run (no SDL, no pads).
Gamepads* gamepadsGlobal(void);
void gamepadsInit(const PadOutSink* sink, const GpHaptics* haptics, GpLog log, void* logUser);
int gamepadsReady(void);
void gamepadsSetDisconnectHook(void (*fn)(void*), void* user);
void gamepadsSetActive(int active);
void gamepadsStreamStart(void);
void gamepadsStreamStop(void);
int gamepadsClaimFramePad(void);
void gamepadsReleaseFramePad(void);
int gamepadsAnnouncedMask(void);
// For ConnListenerRumble / ConnListenerRumbleTriggers: a physical pad with the
// number takes it, anything else goes on to gamepad mode's input/rumble
// Native (lane artemis-settings): the flat launcher's Artemis-style gamepad settings. Process-wide,
// may be set before gamepadsStart.
//   rumble off: the host's rumble is dropped (physical pads and the headset controllers)
//   deadzone: percent, -20..20 (Artemis seekbar_deadzone): positive is a radial dead zone on the
//             physical pads' sticks, negative is dead zone compensation (the game's own dead zone is
//             jumped over); 0 sends the sticks as read
//   flip: A<->B and X<->Y on the physical pads (Artemis checkbox_flip_face_buttons)
void gamepadsSetRumbleEnabled(int enabled);
void gamepadsSetDeadzone(int percent);
// What was set, for the Frame controllers' own dead zone (xr_gamepad's padDeadzonePercentFromLauncher)
int gamepadsGetDeadzone(void);
void gamepadsSetFlipFaceButtons(int flip);
// The transform above on one packet (tests)
void gpApplyStickOptions(PadOutPacket* pk, int deadzonePercent, int flip);

// For ConnListenerSetMotionEventState / ConnListenerSetControllerLED
void gamepadsHostMotionState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);
void gamepadsHostSetLed(uint16_t controllerNumber, uint8_t red, uint8_t green, uint8_t blue);

void gamepadsHostRumble(unsigned short controllerNumber, unsigned short lowFreqMotor,
                        unsigned short highFreqMotor);
void gamepadsHostRumbleTriggers(uint16_t controllerNumber, uint16_t leftTriggerMotor,
                                uint16_t rightTriggerMotor);

// The real sends for the extras (pad_out_ll.c, library mlxr_input)
void gpUseLimelightExtras(GpExtraSink* sink);

// ---- the SDL source (gamepads_sdl.c, library mlxr_input_sdl) --------------
// Starts SDL's game controller subsystem and its thread, and gamepadsInit()s
// with the real Limelight sends. Returns 0, or -1 when SDL could not start
// (the app goes on without physical pads). MLXR_GAMEPADS=0 turns it off.
int gamepadsStart(void (*disconnect)(void*), void* user);
void gamepadsStop(void);

#endif
