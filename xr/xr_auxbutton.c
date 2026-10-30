// Native (lane a11y-2): clicks for the head and eye pointer that are not a controller's trigger.
//
// The headset's side button (the Steam Frame's "Aux" button, which SteamVR uses to open its
// dashboard): read from its evdev device (input/auxbutton.c) and, while the Accessibility tab's
// Headset button row is on, the session is focused and streaming and the head or the eyes point,
// taken from SteamVR with EVIOCGRAB so a press clicks here instead of opening the Steam home. The
// grab is let go whenever any of that stops, at the session's end, and for 15 s after a 3 s hold
// (with a toast), so the user can always reach the Steam home; the kernel lets it go with the
// process too. Nothing here asks for more permission than the user has: an unreadable device is
// logged once and the tab greys its row.
//
// Also an OpenXR button for the same gestures, named in [vr] assist_click_path ("<interaction
// profile> <input path>"), for a path mlxr-hello --button-probe finds; empty by default.
//
// The gestures are xr/core/xr_assistclick.c's; what they do to the mouse buttons is played out
// here once a frame (xrAssistFrame) and read by the input pass. GPLv3.
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "xr_renderer.h"
#include "xr_native.h"

// How long the button stays with SteamVR after the 3 s hold
#define AUX_ESCAPE_NS 15000000000LL

void xrAuxButtonLook(XrCtx* ctx) {
    auxDevFind("/sys", "/dev/input", &ctx->auxInfo);
    int state = !ctx->auxInfo.found ? ACCESS_BUTTON_MISSING
              : !ctx->auxInfo.readable ? ACCESS_BUTTON_UNREADABLE : ACCESS_BUTTON_OK;
    if (state != ctx->auxState) {
        if (state == ACCESS_BUTTON_OK) {
            LOGI("headset button: %s (%s) found", ctx->auxInfo.path, AUXBTN_DEVICE_NAME);
        }
        else if (state == ACCESS_BUTTON_UNREADABLE) {
            LOGW("headset button: %s not readable (%s); see the user guide, Accessibility", ctx->auxInfo.path,
                 strerror(ctx->auxInfo.err));
        }
        else {
            LOGI("headset button: no %s input device with KEY_SELECT", AUXBTN_DEVICE_NAME);
        }
    }
    ctx->auxState = state;
}

void xrAuxButtonRelease(XrCtx* ctx, const char* why) {
    if (ctx->auxDev.fd >= 0) {
        auxDevClose(&ctx->auxDev);
        LOGEV("headset button: given back to SteamVR (%s)", why);
    }
    // a drag the button held is let go with it
    if (ctx->auxGest.down || ctx->auxGest.dragging) {
        assistPlayStart(&ctx->assistPlay, ASSIST_DRAG_UP, 0);
    }
    auxClickReset(&ctx->auxGest);
}

void xrAssistAction(XrCtx* ctx, int action, int64_t now) {
    if (action == ASSIST_NONE) {
        return;
    }
    // where the click goes is settled when it starts: the picture, or our own menus, which have
    // only a press (a double or right click there is one press)
    if (action == ASSIST_LEFT || action == ASSIST_DOUBLE || action == ASSIST_RIGHT || action == ASSIST_DRAG_DOWN) {
        ctx->assistUi = ctx->hoverKind != HOVER_SCREEN;
        if (ctx->assistUi && (action == ASSIST_DOUBLE || action == ASSIST_RIGHT)) {
            action = ASSIST_LEFT;
        }
    }
    assistPlayStart(&ctx->assistPlay, action, now);
    LOGEV("assist %s on %s", assistActionName(action), ctx->assistUi ? "our UI" : "the picture");
}

void xrAssistFrame(XrCtx* ctx, int64_t now) {
    int pointing = ctx->pointerSource != POINTER_SOURCE_CONTROLLER;
    if (!pointing && assistPlayBusy(&ctx->assistPlay, now)) {
        assistPlayReset(&ctx->assistPlay);
    }
    int press = 0;
    ctx->assistButtons = assistPlayButtons(&ctx->assistPlay, now, &press);
    ctx->assistPress = press;
}

static void gesture(XrCtx* ctx, int action, int64_t now) {
    if (action == ASSIST_ESCAPE) {
        assistPlayStart(&ctx->assistPlay, ASSIST_ESCAPE, now);
        if (ctx->auxDev.fd >= 0) {
            auxDevClose(&ctx->auxDev);
            ctx->auxEscapeUntilNs = now + AUX_ESCAPE_NS;
            xrPanelsNotice(ctx, ui_str(UIS_AUX_RETURNED));
            LOGEV("headset button: held 3 s, given back to SteamVR for %d s", (int)(AUX_ESCAPE_NS / 1000000000LL));
        }
        return;
    }
    xrAssistAction(ctx, action, now);
}

void xrAuxButtonFrame(XrCtx* ctx, int64_t now, int focused, int streaming) {
    // looked for once a session for the tab's row, and again every 5 s while it is wanted and not there
    if (ctx->auxState == ACCESS_BUTTON_UNKNOWN) {
        xrAuxButtonLook(ctx);
        ctx->auxLookNs = now;
    }
    const char* why = !ctx->assistHeadsetButton ? "the setting is off"
                    : ctx->pointerSource == POINTER_SOURCE_CONTROLLER ? "a controller points"
                    : !focused ? "the session lost focus"
                    : !streaming ? "no stream"
                    : ctx->padMode ? "gamepad mode"
                    : now < ctx->auxEscapeUntilNs ? "held 3 s" : NULL;
    if (why != NULL) {
        xrAuxButtonRelease(ctx, why);
    }
    else if (ctx->auxDev.fd < 0) {
        if (ctx->auxState != ACCESS_BUTTON_OK && now - ctx->auxLookNs >= 5000000000LL) {
            xrAuxButtonLook(ctx);
            ctx->auxLookNs = now;
        }
        if (ctx->auxState == ACCESS_BUTTON_OK) {
            int err = auxDevOpen(&ctx->auxDev, ctx->auxInfo.path, 1);
            if (err != 0) {
                if (!ctx->auxWarned) {
                    LOGW("headset button: %s could not be opened (%s)", ctx->auxInfo.path, strerror(-err));
                }
                ctx->auxWarned = 1;
                ctx->auxState = ACCESS_BUTTON_UNREADABLE;
            }
            else {
                LOGEV("headset button: %s taken from SteamVR (grab %s): once a click, twice a double click, three "
                      "times a right click, held a drag, held 3 s back to SteamVR for 15 s",
                      ctx->auxInfo.path, ctx->auxDev.grabbed ? "ok" : "refused, SteamVR still sees it");
            }
        }
    }
    if (ctx->auxDev.fd >= 0) {
        int edges[16];
        int n = auxDevRead(&ctx->auxDev, edges, 16);
        gesture(ctx, auxClickTick(&ctx->auxGest, now), now);
        for (int i = 0; i < n; i++) {
            gesture(ctx, auxClickEdge(&ctx->auxGest, edges[i], now), now);
        }
        if (n < 0) {
            LOGW("headset button: %s went away", ctx->auxInfo.path);
            ctx->auxState = ACCESS_BUTTON_UNKNOWN;
        }
    }
    else if (ctx->assistClickBound) {
        // the OpenXR button's presses come in after the sync (xrAssistClickRead); its holds tick here
        gesture(ctx, auxClickTick(&ctx->auxGest, now), now);
    }
    xrAssistFrame(ctx, now);
}

// ---- an OpenXR button (assist_click_path) ------------------------------------------------------

static int splitPath(const XrCtx* ctx, char* profile, size_t pcap, char* path, size_t cap) {
    const char* s = ctx->assistClickPath;
    const char* sp = strchr(s, ' ');
    if (s[0] != '/' || sp == NULL) {
        return 0;
    }
    snprintf(profile, pcap, "%.*s", (int)(sp - s), s);
    while (*sp == ' ') sp++;
    snprintf(path, cap, "%s", sp);
    return path[0] == '/';
}

void xrAssistClickCreate(XrCtx* ctx) {
    char profile[128], path[128];
    ctx->assistClickAction = XR_NULL_HANDLE;
    ctx->assistClickBound = 0;
    if (ctx->assistClickPath[0] == 0) {
        return;
    }
    if (!splitPath(ctx, profile, sizeof profile, path, sizeof path)) {
        LOGW("assist_click_path \"%s\" is not \"<interaction profile> <input path>\": left off", ctx->assistClickPath);
        return;
    }
    XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
    info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strncpy(info.actionName, "assistclick", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, "Click for the head pointer", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    if (XR_FAILED(xrCreateAction(ctx->actionSet, &info, &ctx->assistClickAction))) {
        ctx->assistClickAction = XR_NULL_HANDLE;
        LOGW("assist_click_path: the action could not be made");
    }
}

uint32_t xrAssistClickBind(XrCtx* ctx, const char* profile, XrActionSuggestedBinding* b, uint32_t n, uint32_t cap) {
    char want[128], path[128];
    if (ctx->assistClickAction == XR_NULL_HANDLE || n >= cap
            || !splitPath(ctx, want, sizeof want, path, sizeof path) || strcmp(want, profile) != 0) {
        return n;
    }
    XrPath p = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(ctx->instance, path, &p))) {
        return n;
    }
    b[n].action = ctx->assistClickAction;
    b[n].binding = p;
    LOGI("assist_click_path: %s offered with %s's bindings", path, profile);
    ctx->assistClickBound = 1;   // taken back below if the runtime refuses the set
    return n + 1;
}

void xrAssistClickSuggestAlone(XrCtx* ctx) {
    char profile[128], path[128];
    if (ctx->assistClickAction == XR_NULL_HANDLE || ctx->assistClickBound
            || !splitPath(ctx, profile, sizeof profile, path, sizeof path)) {
        return;
    }
    XrActionSuggestedBinding b;
    b.action = ctx->assistClickAction;
    if (XR_FAILED(xrStringToPath(ctx->instance, path, &b.binding))) {
        LOGW("assist_click_path: %s is not a path", path);
        return;
    }
    XrInteractionProfileSuggestedBinding suggest = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    if (XR_FAILED(xrStringToPath(ctx->instance, profile, &suggest.interactionProfile))) {
        LOGW("assist_click_path: %s is not a profile", profile);
        return;
    }
    suggest.countSuggestedBindings = 1;
    suggest.suggestedBindings = &b;
    XrResult r = xrSuggestInteractionProfileBindings(ctx->instance, &suggest);
    ctx->assistClickBound = XR_SUCCEEDED(r);
    LOGEV("assist_click_path: %s on %s %s (%d)", path, profile, ctx->assistClickBound ? "bound" : "refused", (int)r);
}

void xrAssistClickRead(XrCtx* ctx, int64_t now) {
    if (ctx->assistClickAction == XR_NULL_HANDLE || ctx->session == XR_NULL_HANDLE) {
        return;
    }
    XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = ctx->assistClickAction;
    XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (XR_FAILED(xrGetActionStateBoolean(ctx->session, &get, &st))) {
        return;
    }
    int down = st.isActive && st.currentState;
    if (down != ctx->assistClickLast) {
        ctx->assistClickLast = down;
        if (ctx->pointerSource != POINTER_SOURCE_CONTROLLER && !ctx->padMode) {
            gesture(ctx, auxClickEdge(&ctx->auxGest, down, now), now);
        }
    }
}
