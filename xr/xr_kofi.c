// The Ko-fi sheet's "Open page" (PLAN 6.14): runs Steam's browser on the Frame. The choice of
// program, the command line and the once-per-press guard are core/xr_kofi.c; this is the fork.
// GPLv3.
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "xr_kofi.h"
#include "xr_renderer.h"

static XrKofiGuard gGuard;

// Forks the opener twice over so the frame loop never waits on it and never leaves a zombie:
// the first child only starts the second and exits at once, the second becomes the program.
// Steam's runtime points LD_LIBRARY_PATH and LD_PRELOAD at its own libraries while this
// client runs as a game; the steam launcher script and a browser want the host's.
int kofiOpenPage(const char* url) {
    if (!url) {
        url = XR_KOFI_URL;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (!xrKofiGuardAllow(&gGuard, now)) {
        LOGI("Ko-fi page: already opened for this press");
        return 0;
    }
    XrKofiOpener opener = xrKofiChooseOpener(getenv("PATH"));
    char buf[512];
    char* argv[4];
    if (xrKofiBuildArgv(opener, url, buf, sizeof buf, argv) == 0) {
        LOGW("Ko-fi page not opened: %s", opener == XR_KOFI_OPEN_NONE
                ? "neither steam nor xdg-open is on PATH" : "the address was refused");
        return 0;
    }
    pid_t first = fork();
    if (first < 0) {
        LOGW("Ko-fi page not opened: fork failed");
        return 0;
    }
    if (first == 0) {
        pid_t second = fork();
        if (second != 0) {
            _exit(second < 0 ? 1 : 0);
        }
        setsid();
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
            if (null > 2) {
                close(null);
            }
        }
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("LD_PRELOAD");
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(first, &status, 0);
    LOGEV("Ko-fi page opened with %s", argv[0]);
    return 1;
}
