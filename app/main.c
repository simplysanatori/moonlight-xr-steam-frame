// Moonlight XR Native: entry point. GPLv3.
//
//   moonlight-xr --file clip.mp4 [--seconds N]       play a local file on the screen (test path)
//
// Streaming from a host arrives with the backend lane in M1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../stream/file_source.h"
#include "../stream/frame_slot.h"
#include "../xr/mlxr_log.h"
#include "../xr/xr_session.h"

static FrameSlot g_slot;

static void statsHook(void* user, char* buf, int len) {
    (void)user;
    snprintf(buf, (size_t)len, "decoded %llu, dropped %llu, last decode %.1f ms",
             (unsigned long long)g_slot.produced, (unsigned long long)g_slot.dropped, g_slot.lastDecodeMs);
}

static void pointerHook(void* user, int x, int y, int w, int h) {
    (void)user;
    static int n;
    if (n++ % 30 == 0) MLOG_I("pointer %d,%d of %dx%d", x, y, w, h);
}

static void buttonHook(void* user, int button, int down) {
    (void)user;
    MLOG_I("button %d %s", button, down ? "down" : "up");
}

static void scrollHook(void* user, int amount) {
    (void)user;
    MLOG_I("scroll %d", amount);
}

int main(int argc, char** argv) {
    const char* file = NULL;
    double seconds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else {
            fprintf(stderr, "usage: %s --file VIDEO [--seconds N]\n", argv[0]);
            return 2;
        }
    }
    if (!file) {
        fprintf(stderr, "nothing to show: pass --file (host streaming lands later in M1)\n");
        return 2;
    }
    frame_slot_init(&g_slot);
    FileSource* src = file_source_start(file, &g_slot);
    if (!src) return 1;
    XrAppConfig cfg = {0};
    cfg.video = &g_slot;
    cfg.screenDistance = 2.0f;
    cfg.screenWidth = 2.4f;
    cfg.seconds = seconds;
    cfg.hooks = (XrAppHooks){NULL, pointerHook, buttonHook, scrollHook, statsHook};
    int rc = xr_app_run(&cfg);
    file_source_stop(src);
    frame_slot_destroy(&g_slot);
    return rc;
}
