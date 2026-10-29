// padOutUseLimelight against the real moonlight-common-c with no connection up:
// the sends must be harmless (they report an error, nothing more).
#include "check.h"
#include "gamepads.h"
#include "pad_out.h"

int main(void) {
    PadOutSink sink;
    padOutUseLimelight(&sink);
    CHECK(sink.arrival != NULL && sink.state != NULL);
    CHECK(sink.arrival(sink.user, 0, 1, 1, 0xf3ff, 3) != 0);
    CHECK(sink.state(sink.user, 0, 1, 0x1000, 1, 2, 3, 4, 5, 6) != 0);
    PadOut p;
    padOutInit(&p, &sink);
    PadState s;
    padRest(&s);
    s.buttons = PAD_A;
    CHECK(padOutFrame(&p, 1, &s, 0) == 2);
    CHECK(padOutFrame(&p, 0, &s, 1) == 1);
    // The extras (motion, battery, touchpad): harmless with no connection too
    GpExtraSink extra;
    gpUseLimelightExtras(&extra);
    CHECK(extra.motion != NULL && extra.battery != NULL && extra.touch != NULL);
    CHECK(extra.motion(extra.user, 0, LI_MOTION_TYPE_GYRO, 1, 2, 3) != 0);
    CHECK(extra.battery(extra.user, 0, LI_BATTERY_STATE_DISCHARGING, 50) != 0);
    CHECK(extra.touch(extra.user, 0, LI_TOUCH_EVENT_DOWN, 0, 0.5f, 0.5f, 1.0f) != 0);
    return checksDone("input_limelight");
}
