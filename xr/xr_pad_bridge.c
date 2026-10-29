// Moonlight XR Native: gamepad mode's pad to the host (M3). GPLv3.
#include "xr_pad_bridge.h"

#include <pthread.h>

#include "core/xr_padexit.h"

#include "../input/gamepads.h"
#include "../input/pad_out.h"
#include "../input/rumble.h"
#include "mlxr_log.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static PadOut g_out;
static int g_ready, g_flip;
static PadExit g_exit = { 1 };
static void (*g_hook)(void*);
static void* g_hookUser;

void xrPadBridgeSetDisconnectHook(void (*fn)(void* user), void* user) {
    pthread_mutex_lock(&g_lock);
    g_hook = fn;
    g_hookUser = user;
    pthread_mutex_unlock(&g_lock);
}

void xrPadBridgeSetFlip(int flip) {
    pthread_mutex_lock(&g_lock);
    g_flip = flip ? 1 : 0;
    if (g_ready) padOutSetFlipFace(&g_out, g_flip);
    pthread_mutex_unlock(&g_lock);
}

void xrPadBridgeSetRumble(int mix, int intensityPercent) {
    mix = padRumbleMixClamp(mix);
    intensityPercent = padRumbleIntensityClamp(intensityPercent);
    rumbleSetMix(rumbleGlobal(), mix, intensityPercent);
    MLOG_I("rumble: %s, intensity %d%%",
           mix == PAD_RUMBLE_MIX_SPLIT ? "low motor left, high motor right"
                                       : "both controllers get the stronger motor",
           intensityPercent);
}

void xrPadBridgeStart(void) {
    PadOutSink sink;
    padOutUseLimelight(&sink);
    xrPadBridgeStartWithSink(&sink);
}

void xrPadBridgeStartWithSink(const PadOutSink* sink) {
    pthread_mutex_lock(&g_lock);
    padExitReset(&g_exit);
    padOutInit(&g_out, sink);
    padOutSetFlipFace(&g_out, g_flip);
    rumbleSetController(rumbleGlobal(), 0);
    rumbleReset(rumbleGlobal());
    g_ready = 1;
    pthread_mutex_unlock(&g_lock);
    gamepadsStreamStart(); // Native: physical pads announce themselves to this host
    MLOG_I("gamepad: pad output ready (flip face buttons %s)", g_flip ? "on" : "off");
}

void xrPadBridgeFrame(int plugged, const PadState* s, int64_t nowNs) {
    void (*fire)(void*) = NULL;
    void* user = NULL;
    pthread_mutex_lock(&g_lock);
    if (g_ready) {
        PadState rest;
        const PadState* send = s;
        if (plugged && padExitUpdate(&g_exit, s)) {
            // The chord: the host sees the pad at rest first, then the hook
            fire = g_hook;
            user = g_hookUser;
            MLOG_I("gamepad: Disconnected: Start+Select+L1+R1 (disconnect your session)");
        }
        if (padExitLatched(&g_exit)) {
            padRest(&rest);
            send = &rest;
        }
        // Native: the pad takes a number the physical pads (input/gamepads) do not hold,
        // and its packets carry their mask so the host keeps them
        int wasPlugged = g_out.plugged;
        if (plugged && !wasPlugged) {
            int n = gamepadsClaimFramePad();
            padOutSetNumber(&g_out, n, gamepadsAnnouncedMask());
            rumbleSetController(rumbleGlobal(), n);
        } else if (plugged) {
            padOutSetNumber(&g_out, g_out.controllerNumber, gamepadsAnnouncedMask());
        }
        padOutFrame(&g_out, plugged, send, nowNs);
        if (!plugged && wasPlugged) gamepadsReleaseFramePad();
    }
    pthread_mutex_unlock(&g_lock);
    // Outside the lock: the hook takes the launcher's, and stopping the stream
    // (under that one) comes back here for this one
    if (fire) fire(user);
}

void xrPadBridgeStop(void) {
    pthread_mutex_lock(&g_lock);
    if (g_ready) {
        if (g_out.plugged) gamepadsReleaseFramePad();
        padOutUnplug(&g_out);
        rumbleStop(rumbleGlobal());
        g_ready = 0;
    }
    pthread_mutex_unlock(&g_lock);
    gamepadsStreamStop(); // Native: physical pads leave the host before the connection ends
}
