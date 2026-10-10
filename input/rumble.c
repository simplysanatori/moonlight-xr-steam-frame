// The host's rumble as haptic requests, see rumble.h
#include <string.h>

#include "rumble.h"

void rumbleInit(Rumble* r) {
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->lock, NULL);
    padRumbleReset(&r->pulses);
    r->frequency[0] = XR_FREQUENCY_UNSPECIFIED;
    r->frequency[1] = XR_FREQUENCY_UNSPECIFIED;
}

void rumbleDestroy(Rumble* r) {
    pthread_mutex_destroy(&r->lock);
}

void rumbleSetController(Rumble* r, int controllerNumber) {
    pthread_mutex_lock(&r->lock);
    r->controllerNumber = controllerNumber;
    pthread_mutex_unlock(&r->lock);
}

void rumbleSetFrequency(Rumble* r, float leftHz, float rightHz) {
    r->frequency[0] = leftHz;
    r->frequency[1] = rightHz;
}

void rumbleOnHost(Rumble* r, unsigned short controllerNumber, unsigned short lowFreqMotor,
                  unsigned short highFreqMotor) {
    pthread_mutex_lock(&r->lock);
    if (controllerNumber == r->controllerNumber) {
        r->count++;
        r->low = lowFreqMotor;
        r->high = highFreqMotor;
    }
    pthread_mutex_unlock(&r->lock);
}

void rumbleStop(Rumble* r) {
    pthread_mutex_lock(&r->lock);
    r->count++;
    r->low = 0;
    r->high = 0;
    pthread_mutex_unlock(&r->lock);
}

void rumbleReset(Rumble* r) {
    pthread_mutex_lock(&r->lock);
    // What the host said before is not for the next pad
    r->handed = r->count;
    pthread_mutex_unlock(&r->lock);
    padRumbleReset(&r->pulses);
}

void rumblePoll(Rumble* r, const int live[2], int64_t nowNs, RumbleRequest out[2]) {
    pthread_mutex_lock(&r->lock);
    uint64_t count = r->count;
    int low = r->low;
    int high = r->high;
    pthread_mutex_unlock(&r->lock);

    // Each word from the host arms the controllers on this pass
    if (count != r->handed) {
        r->handed = count;
        padRumbleAsk(&r->pulses, low, high);
    }
    int action[2];
    float amp[2];
    padRumbleStep(&r->pulses, live, nowNs, action, amp);
    for (int h = 0; h < 2; h++) {
        out[h].action = action[h];
        out[h].amplitude = amp[h];
        out[h].frequency = r->frequency[h];
        out[h].durationNs = PAD_RUMBLE_PULSE_NS;
    }
}

int rumbleVibration(const RumbleRequest* req, XrHapticVibration* out) {
    if (req->action != PAD_RUMBLE_APPLY) {
        return 0;
    }
    float amp = req->amplitude;
    out->type = XR_TYPE_HAPTIC_VIBRATION;
    out->next = NULL;
    out->duration = req->durationNs;
    out->frequency = req->frequency;
    out->amplitude = amp < 0.0f ? 0.0f : (amp > 1.0f ? 1.0f : amp);
    return 1;
}

static Rumble gRumble;
static pthread_once_t gOnce = PTHREAD_ONCE_INIT;

static void initGlobal(void) {
    rumbleInit(&gRumble);
}

Rumble* rumbleGlobal(void) {
    pthread_once(&gOnce, initGlobal);
    return &gRumble;
}

void rumbleHostCallback(unsigned short controllerNumber, unsigned short lowFreqMotor,
                        unsigned short highFreqMotor) {
    rumbleOnHost(rumbleGlobal(), controllerNumber, lowFreqMotor, highFreqMotor);
}

void rumbleTriggersCallback(uint16_t controllerNumber, uint16_t leftTriggerMotor,
                            uint16_t rightTriggerMotor) {
    (void)controllerNumber;
    (void)leftTriggerMotor;
    (void)rightTriggerMotor;
}
