// The Ko-fi sheet's "Open page" (xr_kofi.c): the command line for a URL, the choice of program
// for a PATH, the once-per-press guard.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "xr_kofi.h"

static void touch(const char* dir, const char* name, int mode) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "w");
    if (f) {
        fputs("#!/bin/sh\n", f);
        fclose(f);
    }
    chmod(path, (mode_t)mode);
}

static void testArgv(void) {
    char buf[512];
    char* argv[4];
    CHECK(xrKofiBuildArgv(XR_KOFI_OPEN_STEAM, XR_KOFI_URL, buf, sizeof buf, argv) == 2);
    CHECK(strcmp(argv[0], "steam") == 0);
    CHECK(strcmp(argv[1], "steam://openurl/https://ko-fi.com/moonlightxr") == 0);
    CHECK(argv[2] == NULL);
    CHECK(xrKofiBuildArgv(XR_KOFI_OPEN_XDG, XR_KOFI_URL, buf, sizeof buf, argv) == 2);
    CHECK(strcmp(argv[0], "xdg-open") == 0 && strcmp(argv[1], "https://ko-fi.com/moonlightxr") == 0);
    CHECK(xrKofiBuildArgv(XR_KOFI_OPEN_NONE, XR_KOFI_URL, buf, sizeof buf, argv) == 0);
    // a buffer too small is refused, never cut
    char tiny[32];
    CHECK(xrKofiBuildArgv(XR_KOFI_OPEN_STEAM, XR_KOFI_URL, tiny, sizeof tiny, argv) == 0);
    // what is not a web address never reaches the program
    CHECK(xrKofiUrlOk(XR_KOFI_URL) && xrKofiUrlOk("http://example.com/a?b=c&d=e"));
    CHECK(!xrKofiUrlOk(NULL) && !xrKofiUrlOk("") && !xrKofiUrlOk("ko-fi.com/moonlightxr"));
    CHECK(!xrKofiUrlOk("file:///etc/passwd") && !xrKofiUrlOk("steam://run/480"));
    CHECK(!xrKofiUrlOk("https://ko-fi.com/a b") && !xrKofiUrlOk("https://ko-fi.com/a\nb"));
    CHECK(!xrKofiUrlOk("https://ko-fi.com/\"x") && !xrKofiUrlOk("https://ko-fi.com/a'b"));
    CHECK(xrKofiBuildArgv(XR_KOFI_OPEN_STEAM, "javascript:alert(1)", buf, sizeof buf, argv) == 0);
}

static void testChoice(void) {
    char tmpl[] = "/tmp/xrkofiXXXXXX";
    char* a = mkdtemp(tmpl);
    char tmpl2[] = "/tmp/xrkofiXXXXXX";
    char* b = mkdtemp(tmpl2);
    CHECK(a && b);
    if (!a || !b) {
        return;
    }
    char path[1100];
    CHECK(xrKofiChooseOpener(NULL) == XR_KOFI_OPEN_NONE);
    CHECK(xrKofiChooseOpener("") == XR_KOFI_OPEN_NONE);
    snprintf(path, sizeof path, "%s:%s", a, b);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_NONE);

    touch(b, "xdg-open", 0755);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_XDG);
    // a file that is not executable does not count
    touch(a, "steam", 0644);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_XDG);
    // steam wins when it is anywhere on PATH, whichever directory holds it
    touch(b, "steam", 0755);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_STEAM);
    snprintf(path, sizeof path, "/nonexistent:%s", a);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_NONE);
    snprintf(path, sizeof path, ":%s::", b);
    CHECK(xrKofiChooseOpener(path) == XR_KOFI_OPEN_STEAM);

    char f[600];
    snprintf(f, sizeof f, "%s/steam", a); unlink(f);
    snprintf(f, sizeof f, "%s/steam", b); unlink(f);
    snprintf(f, sizeof f, "%s/xdg-open", b); unlink(f);
    rmdir(a);
    rmdir(b);
}

static void testGuard(void) {
    XrKofiGuard g;
    memset(&g, 0, sizeof g);
    CHECK(xrKofiGuardAllow(&g, 0.0));          // the very first press, even at clock zero
    CHECK(!xrKofiGuardAllow(&g, 0.0));         // the same frame
    CHECK(!xrKofiGuardAllow(&g, 1.0));
    CHECK(!xrKofiGuardAllow(&g, XR_KOFI_GUARD_SEC - 0.01));
    CHECK(xrKofiGuardAllow(&g, XR_KOFI_GUARD_SEC + 0.01));   // a later press
    CHECK(!xrKofiGuardAllow(&g, XR_KOFI_GUARD_SEC + 0.5));
    CHECK(!xrKofiGuardAllow(NULL, 5.0));
}

int main(void) {
    testArgv();
    testChoice();
    testGuard();
    return checksDone("xr_kofi");
}
