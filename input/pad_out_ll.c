// padOutUseLimelight: the real sends, see pad_out.h
#include "Limelight.h"
#include "gamepads.h"
#include "pad_out.h"
#include "xr_presence.h"

static int llArrival(void* user, uint8_t controllerNumber, uint16_t activeGamepadMask,
                     uint8_t type, uint32_t supportedButtonFlags, uint16_t capabilities) {
    (void)user;
    return LiSendControllerArrivalEvent(controllerNumber, activeGamepadMask, type,
                                        supportedButtonFlags, capabilities);
}

static int llState(void* user, short controllerNumber, short activeGamepadMask, int buttonFlags,
                   unsigned char leftTrigger, unsigned char rightTrigger, short leftStickX,
                   short leftStickY, short rightStickX, short rightStickY) {
    (void)user;
    presenceNoteActivity(); // Native (lane wave3-session): a pad's state changed, so the idle clock starts over
    return LiSendMultiControllerEvent(controllerNumber, activeGamepadMask, buttonFlags,
                                      leftTrigger, rightTrigger, leftStickX, leftStickY,
                                      rightStickX, rightStickY);
}

void padOutUseLimelight(PadOutSink* sink) {
    sink->arrival = llArrival;
    sink->state = llState;
    sink->user = NULL;
}

static int llMotion(void* user, uint8_t number, uint8_t motionType, float x, float y, float z) {
    (void)user;
    return LiSendControllerMotionEvent(number, motionType, x, y, z);
}

static int llBattery(void* user, uint8_t number, uint8_t state, uint8_t percentage) {
    (void)user;
    return LiSendControllerBatteryEvent(number, state, percentage);
}

static int llTouch(void* user, uint8_t number, uint8_t eventType, uint32_t pointerId, float x, float y,
                   float pressure) {
    (void)user;
    return LiSendControllerTouchEvent(number, eventType, pointerId, x, y, pressure);
}

void gpUseLimelightExtras(GpExtraSink* sink) {
    sink->motion = llMotion;
    sink->battery = llBattery;
    sink->touch = llTouch;
    sink->user = NULL;
}
