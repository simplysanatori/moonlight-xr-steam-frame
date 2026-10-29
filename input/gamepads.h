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

// How rumble reaches a pad, by the id its source gave it (called under the
// lock, so a source must remove a pad before it closes it)
typedef struct {
    void (*rumble)(void* user, int id, uint16_t low, uint16_t high);
    void (*rumbleTriggers)(void* user, int id, uint16_t left, uint16_t right);
    void* user;
} GpHaptics;

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
    GpInfo info;
    PadOutPacket cur;  // the sources merged
    PadOutPacket sent;
} GpSlot;

typedef struct Gamepads {
    pthread_mutex_t lock;
    int ready;
    PadOutSink sink;
    GpHaptics haptics;
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
    long statesLogged;
} Gamepads;

void gpInit(Gamepads* g, const PadOutSink* sink, const GpHaptics* haptics, GpLog log, void* logUser);
void gpDestroy(Gamepads* g);
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

// The packet's buttons exactly Start+Select+L1+R1
int gpIsChord(int buttonFlags);

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

void gamepadsHostRumble(unsigned short controllerNumber, unsigned short lowFreqMotor,
                        unsigned short highFreqMotor);
void gamepadsHostRumbleTriggers(uint16_t controllerNumber, uint16_t leftTriggerMotor,
                                uint16_t rightTriggerMotor);

// ---- the SDL source (gamepads_sdl.c, library mlxr_input_sdl) --------------
// Starts SDL's game controller subsystem and its thread, and gamepadsInit()s
// with the real Limelight sends. Returns 0, or -1 when SDL could not start
// (the app goes on without physical pads). MLXR_GAMEPADS=0 turns it off.
int gamepadsStart(void (*disconnect)(void*), void* user);
void gamepadsStop(void);

#endif
