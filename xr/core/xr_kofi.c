// The Ko-fi sheet's "Open page": see xr_kofi.h. GPLv3.
#include "xr_kofi.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int onPath(const char* path, const char* name) {
    if (!path || !*path) {
        return 0;
    }
    const char* p = path;
    while (*p) {
        const char* end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char full[1024];
        // an empty entry means the current directory in a shell; a program is not looked for there
        if (len > 0 && len + 1 + strlen(name) < sizeof full) {
            snprintf(full, sizeof full, "%.*s/%s", (int)len, p, name);
            struct stat st;
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode) && access(full, X_OK) == 0) {
                return 1;
            }
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

XrKofiOpener xrKofiChooseOpener(const char* path) {
    if (onPath(path, "steam")) {
        return XR_KOFI_OPEN_STEAM;
    }
    if (onPath(path, "xdg-open")) {
        return XR_KOFI_OPEN_XDG;
    }
    return XR_KOFI_OPEN_NONE;
}

int xrKofiUrlOk(const char* url) {
    if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
        return 0;
    }
    size_t len = strlen(url);
    if (len > 400) {
        return 0;
    }
    for (const char* c = url; *c; c++) {
        unsigned char u = (unsigned char)*c;
        if (u <= ' ' || u == 0x7F || u == '"' || u == '\'' || u == '\\' || u == '`') {
            return 0;
        }
    }
    return 1;
}

int xrKofiBuildArgv(XrKofiOpener opener, const char* url, char* out, size_t cap, char** argv) {
    if (opener == XR_KOFI_OPEN_NONE || !xrKofiUrlOk(url) || !out || !argv) {
        return 0;
    }
    const char* prog = opener == XR_KOFI_OPEN_STEAM ? "steam" : "xdg-open";
    const char* prefix = opener == XR_KOFI_OPEN_STEAM ? "steam://openurl/" : "";
    int n = snprintf(out, cap, "%s%c%s%s", prog, 0, prefix, url);
    size_t progLen = strlen(prog);
    size_t total = progLen + 1 + strlen(prefix) + strlen(url) + 1;
    if (n < 0 || (size_t)n + 1 > cap || total > cap) {
        return 0;
    }
    argv[0] = out;
    argv[1] = out + progLen + 1;
    argv[2] = NULL;
    argv[3] = NULL;
    return 2;
}

int xrKofiGuardAllow(XrKofiGuard* guard, double nowSec) {
    if (!guard) {
        return 0;
    }
    if (guard->launched && nowSec - guard->lastSec < XR_KOFI_GUARD_SEC && nowSec >= guard->lastSec) {
        return 0;
    }
    guard->launched = 1;
    guard->lastSec = nowSec;
    return 1;
}
