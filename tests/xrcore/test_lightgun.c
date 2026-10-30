// The lightgun pointer policy (xr_lightgun.h): when it is on, what it changes,
// the off-picture trigger as a reload, the recoil. New in this fork.
#include "check.h"
#include "xr_controller.h"
#include "xr_lightgun.h"
#include "xr_shared.h"

static LightgunPolicy policyFor(int setting, int kind, int reload, int recoil) {
    LightgunPolicy p;
    lightgunPolicy(setting, kind, reload, recoil, &p);
    return p;
}

static void testActive(void) {
    CHECK(lightgunSettingClamp(LIGHTGUN_AUTO) == LIGHTGUN_AUTO);
    CHECK(lightgunSettingClamp(LIGHTGUN_ON) == LIGHTGUN_ON);
    CHECK(lightgunSettingClamp(LIGHTGUN_OFF) == LIGHTGUN_OFF);
    CHECK(lightgunSettingClamp(-1) == LIGHTGUN_AUTO && lightgunSettingClamp(LIGHTGUN_COUNT) == LIGHTGUN_AUTO);
    // auto: exactly the gun
    for (int kind = 0; kind < CONTROLLER_MODEL_COUNT; kind++) {
        CHECK(lightgunActive(LIGHTGUN_AUTO, kind) == (kind == CONTROLLER_MODEL_GUN));
        CHECK(lightgunActive(LIGHTGUN_ON, kind) == 1);
        CHECK(lightgunActive(LIGHTGUN_OFF, kind) == 0);
    }
    // a setting out of range is auto
    CHECK(lightgunActive(9, CONTROLLER_MODEL_GUN) == 1);
    CHECK(lightgunActive(9, CONTROLLER_MODEL_FRAME) == 0);
    // the model that is drawn: switched off, the stored gun kind is no gun
    CHECK(lightgunActive(LIGHTGUN_AUTO, controllerModelKind(0, CONTROLLER_MODEL_GUN)) == 0);
    CHECK(lightgunActive(LIGHTGUN_AUTO, controllerModelKind(1, CONTROLLER_MODEL_GUN)) == 1);
}

static void testPolicy(void) {
    LightgunPolicy off = policyFor(LIGHTGUN_AUTO, CONTROLLER_MODEL_FRAME, 1, 1);
    // nothing is changed outside the policy, whatever the extras say
    CHECK(!off.active && !off.sleepOff && !off.clicksRaw && !off.gripGrabOnly && !off.reload && !off.recoil);
    CHECK(lightgunSleepOn(&off, 1) == 1 && lightgunSleepOn(&off, 0) == 0);
    CHECK(lightgunTriggerMayGrab(&off) == 1);

    LightgunPolicy gun = policyFor(LIGHTGUN_AUTO, CONTROLLER_MODEL_GUN, 0, 0);
    CHECK(gun.active && gun.sleepOff && gun.clicksRaw && gun.gripGrabOnly);
    CHECK(!gun.reload && !gun.recoil);
    // pointer sleep is off whatever the setting says
    CHECK(lightgunSleepOn(&gun, 1) == 0 && lightgunSleepOn(&gun, 0) == 0);
    CHECK(lightgunTriggerMayGrab(&gun) == 0);
    LightgunPolicy both = policyFor(LIGHTGUN_ON, CONTROLLER_MODEL_FRAME, 1, 1);
    CHECK(both.active && both.reload && both.recoil);
    // forced off with the gun in the hand
    LightgunPolicy forcedOff = policyFor(LIGHTGUN_OFF, CONTROLLER_MODEL_GUN, 1, 1);
    CHECK(!forcedOff.active && !forcedOff.sleepOff && !forcedOff.reload && !forcedOff.recoil);
}

static void testClicks(void) {
    PressFixCfg normal;
    float hold = 8.0f;
    float holdOut;
    clickSteadyConfig(CLICK_STEADY_STEADY, 0, &normal, &hold);
    CHECK(normal.stabilise == 1 && normal.doublePx > 0.0f && normal.doubleNs > 0 && hold > 0.0f);

    // outside the policy they are left as they were
    PressFixCfg cfg = normal;
    holdOut = hold;
    LightgunPolicy off = policyFor(LIGHTGUN_OFF, CONTROLLER_MODEL_GUN, 0, 0);
    lightgunClickConfig(&off, &cfg, &holdOut);
    CHECK(cfg.stabilise == normal.stabilise && cfg.doublePx == normal.doublePx
          && cfg.doubleNs == normal.doubleNs && holdOut == hold);

    // inside it press stabilise, the double-click wait and the click hold are all off
    LightgunPolicy gun = policyFor(LIGHTGUN_AUTO, CONTROLLER_MODEL_GUN, 0, 0);
    cfg = normal;
    holdOut = hold;
    lightgunClickConfig(&gun, &cfg, &holdOut);
    CHECK(cfg.stabilise == 0 && cfg.doublePx == 0.0f && cfg.doubleNs == 0 && holdOut == 0.0f);
    // the setting the config came from is not touched (the caller copies)
    CHECK(normal.stabilise == 1);
}

// A frame of the buttons: wanted left, on the picture or not
static int frame(LightgunReload* r, const LightgunPolicy* p, int left, int onPicture, int offPicture) {
    int wanted = left ? LIGHTGUN_BTN_LEFT : 0;
    // what the pointer works out: a press only counts on the picture
    int buttons = onPicture ? wanted : 0;
    return lightgunButtons(r, p, wanted, onPicture, offPicture, buttons);
}

static void testReload(void) {
    LightgunPolicy on = policyFor(LIGHTGUN_ON, CONTROLLER_MODEL_GUN, 1, 1);
    LightgunPolicy noReload = policyFor(LIGHTGUN_ON, CONTROLLER_MODEL_GUN, 0, 1);
    LightgunReload r;
    lightgunReloadReset(&r);

    // a pull on the picture is a shot, a left click
    CHECK(frame(&r, &on, 0, 1, 0) == 0);
    CHECK(frame(&r, &on, 1, 1, 0) == LIGHTGUN_BTN_LEFT);
    CHECK(!r.edge && !r.latched);
    CHECK(frame(&r, &on, 1, 1, 0) == LIGHTGUN_BTN_LEFT);
    CHECK(frame(&r, &on, 0, 1, 0) == 0);

    // a pull that starts off the picture is a right click for as long as it is held
    CHECK(frame(&r, &on, 0, 0, 1) == 0);
    CHECK(frame(&r, &on, 1, 0, 1) == LIGHTGUN_BTN_RIGHT);
    CHECK(r.edge && r.latched);
    CHECK(frame(&r, &on, 1, 0, 1) == LIGHTGUN_BTN_RIGHT);
    CHECK(!r.edge && r.latched);
    // ...even when the aim swings back onto the picture meanwhile, and it is no left click
    CHECK(frame(&r, &on, 1, 1, 0) == LIGHTGUN_BTN_RIGHT);
    CHECK(frame(&r, &on, 1, 0, 0) == LIGHTGUN_BTN_RIGHT);
    // letting go lets go of it
    CHECK(frame(&r, &on, 0, 0, 1) == 0);
    CHECK(!r.latched);

    // a pull that starts on the picture is a left click even if the aim then leaves it
    CHECK(frame(&r, &on, 1, 1, 0) == LIGHTGUN_BTN_LEFT);
    CHECK(frame(&r, &on, 1, 0, 1) == 0); // the pointer lets go of the left off the picture itself
    CHECK(!r.latched);
    CHECK(frame(&r, &on, 0, 0, 1) == 0);

    // a pull on a button of ours (neither the picture nor the room) is neither
    CHECK(frame(&r, &on, 1, 0, 0) == 0);
    CHECK(!r.latched);
    CHECK(frame(&r, &on, 0, 0, 0) == 0);

    // another button the pointer holds stays held through a reload pull
    int a = LIGHTGUN_BTN_RIGHT;
    lightgunReloadReset(&r);
    CHECK(lightgunButtons(&r, &on, LIGHTGUN_BTN_LEFT | a, 0, 1, a) == LIGHTGUN_BTN_RIGHT);
    CHECK(lightgunButtons(&r, &on, 0, 0, 1, a) == a);

    // the reload switch off: the buttons are the pointer's own
    lightgunReloadReset(&r);
    CHECK(frame(&r, &noReload, 1, 0, 1) == 0);
    CHECK(!r.latched && !r.edge);
    // and outside the policy altogether
    LightgunPolicy none = policyFor(LIGHTGUN_AUTO, CONTROLLER_MODEL_FRAME, 1, 1);
    CHECK(frame(&r, &none, 0, 0, 1) == 0);
    CHECK(frame(&r, &none, 1, 0, 1) == 0);
    CHECK(!r.latched);

    // a reload pull already held when the switch goes off is let go of
    lightgunReloadReset(&r);
    frame(&r, &on, 1, 0, 1);
    CHECK(r.latched);
    CHECK(frame(&r, &noReload, 1, 0, 1) == 0);
    CHECK(!r.latched);
}

static void testRecoil(void) {
    LightgunPolicy on = policyFor(LIGHTGUN_ON, CONTROLLER_MODEL_GUN, 1, 1);
    LightgunPolicy noRecoil = policyFor(LIGHTGUN_ON, CONTROLLER_MODEL_GUN, 1, 0);
    LightgunPolicy none = policyFor(LIGHTGUN_AUTO, CONTROLLER_MODEL_FRAME, 1, 1);
    LightgunReload r;
    lightgunReloadReset(&r);

    // the left button coming down is a shot, held or released it is not
    CHECK(lightgunRecoilDue(&on, &r, 0, LIGHTGUN_BTN_LEFT) == 1);
    CHECK(lightgunRecoilDue(&on, &r, LIGHTGUN_BTN_LEFT, LIGHTGUN_BTN_LEFT) == 0);
    CHECK(lightgunRecoilDue(&on, &r, LIGHTGUN_BTN_LEFT, 0) == 0);
    CHECK(lightgunRecoilDue(&on, &r, 0, 0) == 0);
    // a right click that is not a reload is not a shot
    CHECK(lightgunRecoilDue(&on, &r, 0, LIGHTGUN_BTN_RIGHT) == 0);
    // a reload pull beginning is, once
    frame(&r, &on, 1, 0, 1);
    CHECK(r.edge);
    CHECK(lightgunRecoilDue(&on, &r, 0, LIGHTGUN_BTN_RIGHT) == 1);
    frame(&r, &on, 1, 0, 1);
    CHECK(lightgunRecoilDue(&on, &r, LIGHTGUN_BTN_RIGHT, LIGHTGUN_BTN_RIGHT) == 0);
    // switched off, or outside the policy
    lightgunReloadReset(&r);
    CHECK(lightgunRecoilDue(&noRecoil, &r, 0, LIGHTGUN_BTN_LEFT) == 0);
    CHECK(lightgunRecoilDue(&none, &r, 0, LIGHTGUN_BTN_LEFT) == 0);
    // the pulse is short and strong
    CHECK(LIGHTGUN_RECOIL_AMPLITUDE >= 0.9f && LIGHTGUN_RECOIL_AMPLITUDE <= 1.0f);
    CHECK(LIGHTGUN_RECOIL_NS >= 20000000LL && LIGHTGUN_RECOIL_NS <= 100000000LL);
}

int main(void) {
    testActive();
    testPolicy();
    testClicks();
    testReload();
    testRecoil();
    return checksDone("xr_lightgun");
}
