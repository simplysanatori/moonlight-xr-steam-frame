// See xr_scroll.h. GPLv3.
#include "xr_scroll.h"

#include <math.h>

void scrollAccReset(ScrollAcc* a) {
    a->carry[0] = 0.0f;
    a->carry[1] = 0.0f;
}

int stickWind(float* carry, float tilt, float dt, float perSec, float deadzone) {
    float mag = fabsf(tilt);
    if (!(mag > deadzone) || deadzone >= 1.0f || !(dt > 0.0f) || !(perSec > 0.0f)) {
        *carry = 0.0f;
        return 0;
    }
    float past = (fminf(mag, 1.0f) - deadzone) / (1.0f - deadzone);
    *carry += copysignf(past * perSec * dt, tilt);
    float whole = truncf(*carry);
    *carry -= whole;
    return (int)whole;
}

void scrollAccStep(ScrollAcc* a, float x, float y, float dt, float notchesPerSec, float deadzone,
                   int* outV, int* outH) {
    float ax = fabsf(x), ay = fabsf(y);
    if (ax > 2.0f * ay) {
        y = 0.0f;
    }
    if (ay > 2.0f * ax) {
        x = 0.0f;
    }
    float perSec = notchesPerSec * (float)SCROLL_HIRES_NOTCH;
    *outV = stickWind(&a->carry[0], y, dt, perSec, deadzone);
    *outH = stickWind(&a->carry[1], x, dt, perSec, deadzone);
}

int scrollSpeedClamp(int percent) {
    return percent < SCROLL_SPEED_MIN ? SCROLL_SPEED_MIN
         : percent > SCROLL_SPEED_MAX ? SCROLL_SPEED_MAX : percent;
}

float scrollNotchesPerSec(int percent, float baseNotchesPerSec) {
    return baseNotchesPerSec * (float)scrollSpeedClamp(percent) / 100.0f;
}
