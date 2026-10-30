// See xr_lightgun.h. GPLv3.
#include "xr_lightgun.h"

#include <string.h>

#include "xr_controller.h"

int lightgunSettingClamp(int setting) {
    return setting == LIGHTGUN_ON || setting == LIGHTGUN_OFF ? setting : LIGHTGUN_AUTO;
}

int lightgunActive(int setting, int modelKind) {
    switch (lightgunSettingClamp(setting)) {
        case LIGHTGUN_ON: return 1;
        case LIGHTGUN_OFF: return 0;
        default: return modelKind == CONTROLLER_MODEL_GUN;
    }
}

void lightgunPolicy(int setting, int modelKind, int reloadSetting, int recoilSetting,
                    LightgunPolicy* out) {
    memset(out, 0, sizeof(*out));
    if (!lightgunActive(setting, modelKind)) {
        return;
    }
    out->active = 1;
    out->sleepOff = 1;
    out->clicksRaw = 1;
    out->gripGrabOnly = 1;
    out->reload = reloadSetting != 0;
    out->recoil = recoilSetting != 0;
}

int lightgunSleepOn(const LightgunPolicy* p, int sleepSetting) {
    return p->sleepOff ? 0 : sleepSetting != 0;
}

void lightgunClickConfig(const LightgunPolicy* p, PressFixCfg* cfg, float* holdPx) {
    if (!p->clicksRaw) {
        return;
    }
    cfg->stabilise = 0;
    cfg->doublePx = 0.0f;
    cfg->doubleNs = 0;
    *holdPx = 0.0f;
}

int lightgunTriggerMayGrab(const LightgunPolicy* p) {
    return !p->gripGrabOnly;
}

void lightgunReloadReset(LightgunReload* r) {
    memset(r, 0, sizeof(*r));
}

int lightgunButtons(LightgunReload* r, const LightgunPolicy* p, int wanted, int hit, int offPicture,
                    int buttons) {
    int left = (wanted & LIGHTGUN_BTN_LEFT) != 0;
    int pressed = left && !r->prevLeft;
    r->prevLeft = left;
    r->edge = 0;
    if (!left || !p->reload) {
        r->latched = 0;
        return buttons;
    }
    if (pressed) {
        // Only a pull that starts clear of the picture and of everything of
        // ours is a reload; a pull on the picture is a shot
        r->latched = !hit && offPicture;
        r->edge = r->latched;
    }
    if (r->latched) {
        return (buttons & ~LIGHTGUN_BTN_LEFT) | LIGHTGUN_BTN_RIGHT;
    }
    return buttons;
}

int lightgunRecoilDue(const LightgunPolicy* p, const LightgunReload* r, int prevButtons, int buttons) {
    if (!p->recoil) {
        return 0;
    }
    int shot = (buttons & LIGHTGUN_BTN_LEFT) && !(prevButtons & LIGHTGUN_BTN_LEFT);
    return shot || r->edge;
}
