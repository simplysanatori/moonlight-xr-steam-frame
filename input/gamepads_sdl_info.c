// Describing and reading SDL2 game controllers for input/gamepads_sdl.c (the app) and
// input/gamepad_probe.c (the probe). GPLv3.
#include "gamepads_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Limelight.h"

// SDL_GameControllerButton order -> Moonlight flags (moonlight-qt k_ButtonMap)
static const int kButtonMap[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    BACK_FLAG, SPECIAL_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    LB_FLAG, RB_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    MISC_FLAG,
    PADDLE1_FLAG, PADDLE2_FLAG, PADDLE3_FLAG, PADDLE4_FLAG,
    TOUCHPAD_FLAG,
};
#define BUTTONS ((int)(sizeof(kButtonMap) / sizeof(kButtonMap[0])))

GpSdlKind gpSdlKind(SDL_GameControllerType type) {
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360: return GP_SDL_XBOX360;
    case SDL_CONTROLLER_TYPE_XBOXONE: return GP_SDL_XBOXONE;
    case SDL_CONTROLLER_TYPE_PS3: return GP_SDL_PS3;
    case SDL_CONTROLLER_TYPE_PS4: return GP_SDL_PS4;
    case SDL_CONTROLLER_TYPE_PS5: return GP_SDL_PS5;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return GP_SDL_SWITCH_PRO;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return GP_SDL_JOYCON_LEFT;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return GP_SDL_JOYCON_RIGHT;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return GP_SDL_JOYCON_PAIR;
    case SDL_CONTROLLER_TYPE_UNKNOWN: return GP_SDL_UNKNOWN;
    default: return GP_SDL_OTHER;
    }
}

const char* gpSdlTypeName(SDL_GameControllerType type) {
    switch (type) {
    case SDL_CONTROLLER_TYPE_UNKNOWN: return "UNKNOWN";
    case SDL_CONTROLLER_TYPE_XBOX360: return "XBOX360";
    case SDL_CONTROLLER_TYPE_XBOXONE: return "XBOXONE";
    case SDL_CONTROLLER_TYPE_PS3: return "PS3";
    case SDL_CONTROLLER_TYPE_PS4: return "PS4";
    case SDL_CONTROLLER_TYPE_PS5: return "PS5";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return "NINTENDO_SWITCH_PRO";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "NINTENDO_SWITCH_JOYCON_LEFT";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "NINTENDO_SWITCH_JOYCON_RIGHT";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "NINTENDO_SWITCH_JOYCON_PAIR";
    case SDL_CONTROLLER_TYPE_VIRTUAL: return "VIRTUAL";
    case SDL_CONTROLLER_TYPE_AMAZON_LUNA: return "AMAZON_LUNA";
    case SDL_CONTROLLER_TYPE_GOOGLE_STADIA: return "GOOGLE_STADIA";
    case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD: return "NVIDIA_SHIELD";
    default: return "OTHER";
    }
}

void gpSdlApplyHints(void) {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"); // SIGTERM must still end the app
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); // no window
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0"); // positional A/B/X/Y, as moonlight-qt
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
}

int gpSdlLoadMappings(void (*found)(void* user, const char* path, int count), void* user) {
    const char* home = getenv("HOME");
    const char* env = getenv("MLXR_GAMECONTROLLERDB");
    char paths[3][512];
    int n = 0, total = 0;
    if (env && *env) snprintf(paths[n++], sizeof(paths[0]), "%s", env);
    if (home) {
        // The flat launcher's (moonlight-qt's MappingManager) cached database, then the client's own dir
        snprintf(paths[n++], sizeof(paths[0]), "%s/.cache/Moonlight XR Native/Moonlight XR Native/gamecontrollerdb.txt", home);
        snprintf(paths[n++], sizeof(paths[0]), "%s/.config/moonlight-xr/gamecontrollerdb.txt", home);
    }
    for (int i = 0; i < n; i++) {
        int r = SDL_GameControllerAddMappingsFromFile(paths[i]);
        if (r >= 0) {
            total += r;
            if (found) found(user, paths[i], r);
        }
    }
    return total;
}

void gpSdlDescribe(SDL_GameController* c, GpInfo* info) {
    memset(info, 0, sizeof(*info));
    const char* name = SDL_GameControllerName(c);
    snprintf(info->name, sizeof(info->name), "%s", name ? name : "<unnamed>");
    SDL_Joystick* js = SDL_GameControllerGetJoystick(c);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), info->guid, sizeof(info->guid));
    const char* serial = SDL_GameControllerGetSerial(c);
    if (serial) snprintf(info->serial, sizeof(info->serial), "%s", serial);
    char* mapping = SDL_GameControllerMapping(c);
    if (mapping) {
        snprintf(info->mapping, sizeof(info->mapping), "%s", mapping);
        SDL_free(mapping);
    }
    info->vendor = SDL_GameControllerGetVendor(c);
    info->product = SDL_GameControllerGetProduct(c);
    gpClassify(info, gpSdlKind(SDL_GameControllerGetType(c)));
    for (int i = 0; i < BUTTONS; i++)
        if (SDL_GameControllerHasButton(c, (SDL_GameControllerButton)i)) info->buttons |= (uint32_t)kButtonMap[i];
    if (SDL_GameControllerGetBindForAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS
        || SDL_GameControllerGetBindForAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS)
        info->caps |= LI_CCAP_ANALOG_TRIGGERS; // as moonlight-qt: an axis binding means analogue
    if (SDL_GameControllerHasRumble(c)) info->caps |= LI_CCAP_RUMBLE;
    if (SDL_GameControllerHasRumbleTriggers(c)) info->caps |= LI_CCAP_TRIGGER_RUMBLE;
}

static short stickY(short v) { return (short)-(v < -32767 ? -32767 : v); } // as moonlight-qt

void gpSdlReadPacket(SDL_GameController* c, PadOutPacket* pk) {
    memset(pk, 0, sizeof(*pk));
    for (int i = 0; i < BUTTONS; i++)
        if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)i)) pk->buttonFlags |= kButtonMap[i];
    pk->leftStickX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    pk->leftStickY = stickY(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    pk->rightStickX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
    pk->rightStickY = stickY(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    int lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    int rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    pk->leftTrigger = (unsigned char)(lt < 0 ? 0 : lt * 255L / 32767);
    pk->rightTrigger = (unsigned char)(rt < 0 ? 0 : rt * 255L / 32767);
}
