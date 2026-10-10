// Moonlight XR Native: gamepad mode's pad to the host (M3). GPLv3.
#include "xr_pad_bridge.h"

#include <pthread.h>

#include "../input/pad_out.h"
#include "../input/rumble.h"
#include "mlxr_log.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static PadOut g_out;
static int g_ready, g_flip;

void xrPadBridgeSetFlip(int flip) {
    pthread_mutex_lock(&g_lock);
    g_flip = flip ? 1 : 0;
    if (g_ready) padOutSetFlipFace(&g_out, g_flip);
    pthread_mutex_unlock(&g_lock);
}

void xrPadBridgeStart(void) {
    pthread_mutex_lock(&g_lock);
    PadOutSink sink;
    padOutUseLimelight(&sink);
    padOutInit(&g_out, &sink);
    padOutSetFlipFace(&g_out, g_flip);
    rumbleSetController(rumbleGlobal(), 0);
    rumbleReset(rumbleGlobal());
    g_ready = 1;
    pthread_mutex_unlock(&g_lock);
    MLOG_I("gamepad: pad output ready (flip face buttons %s)", g_flip ? "on" : "off");
}

void xrPadBridgeFrame(int plugged, const PadState* s, int64_t nowNs) {
    pthread_mutex_lock(&g_lock);
    if (g_ready) padOutFrame(&g_out, plugged, s, nowNs);
    pthread_mutex_unlock(&g_lock);
}

void xrPadBridgeStop(void) {
    pthread_mutex_lock(&g_lock);
    if (g_ready) {
        padOutUnplug(&g_out);
        rumbleStop(rumbleGlobal());
        g_ready = 0;
    }
    pthread_mutex_unlock(&g_lock);
}
