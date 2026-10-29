// The host's four rumble motors onto the two Frame controllers: the mix (both hands or split),
// the trigger motors folded into their own side, and the intensity scale. New in this fork.
#include <string.h>

#include "check.h"
#include "xr_gamepad.h"

#define MS 1000000LL

static void testClamps(void) {
    CHECK(padRumbleMixClamp(PAD_RUMBLE_MIX_BOTH) == PAD_RUMBLE_MIX_BOTH);
    CHECK(padRumbleMixClamp(PAD_RUMBLE_MIX_SPLIT) == PAD_RUMBLE_MIX_SPLIT);
    CHECK(padRumbleMixClamp(-1) == PAD_RUMBLE_MIX_BOTH && padRumbleMixClamp(7) == PAD_RUMBLE_MIX_BOTH);
    CHECK(padRumbleIntensityClamp(-5) == 0 && padRumbleIntensityClamp(0) == 0);
    CHECK(padRumbleIntensityClamp(60) == 60 && padRumbleIntensityClamp(100) == 100);
    CHECK(padRumbleIntensityClamp(250) == 100);
    CHECK(PAD_RUMBLE_INTENSITY_DEFAULT == 100);
}

static void testSplit(void) {
    float w[2];
    // As upstream: low motor left, high motor right
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 65535, 0, 0, 0, w);
    CHECK_NEAR(w[0], 1.0, 1e-6);
    CHECK_NEAR(w[1], 0.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 0, 32768, 0, 0, w);
    CHECK_NEAR(w[0], 0.0, 1e-6);
    CHECK_NEAR(w[1], 32768.0 / 65535.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 16384, 49152, 0, 0, w);
    CHECK_NEAR(w[0], 16384.0 / 65535.0, 1e-6);
    CHECK_NEAR(w[1], 49152.0 / 65535.0, 1e-6);
    // Which is what the plain ask does
    PadRumble r;
    padRumbleReset(&r);
    padRumbleAsk(&r, 16384, 49152);
    CHECK_NEAR(r.want[0], w[0], 1e-6);
    CHECK_NEAR(r.want[1], w[1], 1e-6);
}

static void testBoth(void) {
    float w[2];
    // A game that drives one motor only buzzes both hands
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 65535, 0, 0, 0, w);
    CHECK_NEAR(w[0], 1.0, 1e-6);
    CHECK_NEAR(w[1], 1.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 0, 20000, 0, 0, w);
    CHECK_NEAR(w[0], 20000.0 / 65535.0, 1e-6);
    CHECK_NEAR(w[1], 20000.0 / 65535.0, 1e-6);
    // Both motors: the stronger one, not the sum
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 30000, 50000, 0, 0, w);
    CHECK_NEAR(w[0], 50000.0 / 65535.0, 1e-6);
    CHECK_NEAR(w[1], 50000.0 / 65535.0, 1e-6);
    // Nothing asked, nothing felt
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 0, 0, 0, 0, w);
    CHECK(w[0] == 0.0f && w[1] == 0.0f);
}

static void testTriggers(void) {
    float w[2];
    // Each trigger motor on its own side, whichever mix
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 0, 0, 40000, 0, w);
    CHECK_NEAR(w[0], 40000.0 / 65535.0, 1e-6);
    CHECK(w[1] == 0.0f);
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 0, 0, 0, 65535, w);
    CHECK(w[0] == 0.0f);
    CHECK_NEAR(w[1], 1.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 0, 0, 0, 65535, w);
    CHECK(w[0] == 0.0f);
    CHECK_NEAR(w[1], 1.0, 1e-6);
    // Only as far as they are stronger than the body motors
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 100, 50000, 0, 20000, 60000, w);
    CHECK_NEAR(w[0], 50000.0 / 65535.0, 1e-6);
    CHECK_NEAR(w[1], 60000.0 / 65535.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 100, 10000, 0, 20000, 5000, w);
    CHECK_NEAR(w[0], 20000.0 / 65535.0, 1e-6);
    CHECK_NEAR(w[1], 5000.0 / 65535.0, 1e-6);
}

static void testIntensity(void) {
    float w[2];
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 50, 65535, 0, 0, 0, w);
    CHECK_NEAR(w[0], 0.5, 1e-6);
    CHECK_NEAR(w[1], 0.5, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_SPLIT, 25, 65535, 32768, 0, 65535, w);
    CHECK_NEAR(w[0], 0.25, 1e-6);
    CHECK_NEAR(w[1], 0.25, 1e-6);
    // Zero is silence, and the scale never goes past 1 whatever it is handed
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 0, 65535, 65535, 65535, 65535, w);
    CHECK(w[0] == 0.0f && w[1] == 0.0f);
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, 900, 65535, 65535, 0, 0, w);
    CHECK_NEAR(w[0], 1.0, 1e-6);
    padRumbleMix(PAD_RUMBLE_MIX_BOTH, -30, 65535, 65535, 0, 0, w);
    CHECK(w[0] == 0.0f && w[1] == 0.0f);
}

static void testPulsesFromTheMix(void) {
    PadRumble r;
    padRumbleReset(&r);
    int live[2] = { 1, 1 };
    int action[2];
    float amp[2];
    // One motor, both hands: both pulses start at once
    padRumbleAskMix(&r, PAD_RUMBLE_MIX_BOTH, 80, 65535, 0, 0, 0);
    padRumbleStep(&r, live, 0, action, amp);
    CHECK(action[0] == PAD_RUMBLE_APPLY && action[1] == PAD_RUMBLE_APPLY);
    CHECK_NEAR(amp[0], 0.8, 1e-6);
    CHECK_NEAR(amp[1], 0.8, 1e-6);
    // Stopping is the host asking for nothing
    padRumbleAskMix(&r, PAD_RUMBLE_MIX_BOTH, 80, 0, 0, 0, 0);
    padRumbleStep(&r, live, 10 * MS, action, amp);
    CHECK(action[0] == PAD_RUMBLE_STOP && action[1] == PAD_RUMBLE_STOP);
    // Intensity 0 never starts one
    padRumbleAskMix(&r, PAD_RUMBLE_MIX_BOTH, 0, 65535, 65535, 65535, 65535);
    padRumbleStep(&r, live, 20 * MS, action, amp);
    CHECK(action[0] == PAD_RUMBLE_KEEP && action[1] == PAD_RUMBLE_KEEP);
}

int main(void) {
    testClamps();
    testSplit();
    testBoth();
    testTriggers();
    testIntensity();
    testPulsesFromTheMix();
    return checksDone("xr_gamepad_rumble");
}
