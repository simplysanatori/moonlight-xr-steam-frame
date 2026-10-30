// The Ko-fi sheet's "Open page" (PLAN 6.14): which program shows the page on the Frame, the
// command line for a URL, and the once-per-press guard. Plain strings and arithmetic, no
// process and no OpenXR, so the tests reach all of it; xr/xr_kofi.c does the forking.
// The sheet opens the page in Steam's own browser: `steam steam://openurl/<url>` (SteamVR shows
// it in the dashboard), or `xdg-open <url>` when `steam` is not on PATH. Used by the flat
// launcher too. GPLv3.

#ifndef XR_KOFI_H
#define XR_KOFI_H

#include <stddef.h>

// This port's author's page (README.md, Support); upstream's is linked from the README
#define XR_KOFI_URL "https://ko-fi.com/sana748653"

typedef enum {
    XR_KOFI_OPEN_NONE = 0,  // neither program is on PATH
    XR_KOFI_OPEN_STEAM,     // steam steam://openurl/<url>
    XR_KOFI_OPEN_XDG        // xdg-open <url>
} XrKofiOpener;

// The opener for this PATH (a colon separated list, NULL or "" for none): `steam` when an
// executable file of that name is in one of its directories, else `xdg-open`, else NONE.
XrKofiOpener xrKofiChooseOpener(const char* path);

// Whether this is a URL the sheet will hand out: http(s) only, no space, quote or control
// character (the program is run without a shell, but the Steam URL is one more argument).
int xrKofiUrlOk(const char* url);

// The command line for the opener, in out (cap bytes) as `count` strings with argv pointing
// into it, NULL ended. Returns the count (2), or 0 when the URL is not ok, the opener is NONE
// or it does not fit. argv must hold 4 entries. argv[0] is the bare program name; the caller
// finds it on PATH (execvp).
int xrKofiBuildArgv(XrKofiOpener opener, const char* url, char* out, size_t cap, char** argv);

// Once per press: the sheet closes on the press that asks, but a held pinch or a second hand
// can edge again within the same frames, and a second browser window is not wanted.
typedef struct {
    int launched;      // 0 until the first allowed call
    double lastSec;    // the clock of the last allowed call
} XrKofiGuard;

#define XR_KOFI_GUARD_SEC 3.0

// 1 when this call may run the opener (and notes it), 0 when one ran less than
// XR_KOFI_GUARD_SEC ago. nowSec is any steadily rising clock.
int xrKofiGuardAllow(XrKofiGuard* guard, double nowSec);

#endif
