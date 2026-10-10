// padOutUseLimelight: the real sends, see pad_out.h
#include "Limelight.h"
#include "pad_out.h"

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
    return LiSendMultiControllerEvent(controllerNumber, activeGamepadMask, buttonFlags,
                                      leftTrigger, rightTrigger, leftStickX, leftStickY,
                                      rightStickX, rightStickY);
}

void padOutUseLimelight(PadOutSink* sink) {
    sink->arrival = llArrival;
    sink->state = llState;
    sink->user = NULL;
}
