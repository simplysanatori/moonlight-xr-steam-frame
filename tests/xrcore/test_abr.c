// ABR-lite's decision (xr_abr.c): which bitrate a stream starts with on each network path.
// Native (lane wave4b-abr).
#include "check.h"
#include "xr_abr.h"

#include <string.h>

static void testPick(void) {
    const int none[MLXR_ABR_KINDS] = { 0, 0, 0, 0 };
    const int ov[MLXR_ABR_KINDS] = { 60000, 150000, 0, 200000 };
    MlxrAbrPick p;

    // no override anywhere: the launcher's bitrate, whichever path
    for (int k = MLXR_ABR_UNKNOWN; k < MLXR_ABR_KINDS; k++) {
        p = mlxr_abr_pick(k, 98000, none);
        CHECK(p.kbps == 98000 && !p.overridden);
    }
    // each path takes its own setting
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 98000, ov);
    CHECK(p.kbps == 60000 && p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_ADAPTER, 98000, ov);
    CHECK(p.kbps == 150000 && p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_USB, 98000, ov);
    CHECK(p.kbps == 200000 && p.overridden);
    // a path whose setting is 0 keeps the launcher's
    p = mlxr_abr_pick(MLXR_ABR_WIRED, 98000, ov);
    CHECK(p.kbps == 98000 && !p.overridden);
    // an unknown path (no choice was made) ignores every override
    p = mlxr_abr_pick(MLXR_ABR_UNKNOWN, 98000, ov);
    CHECK(p.kbps == 98000 && !p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_KINDS, 98000, ov);
    CHECK(p.kbps == 98000 && !p.overridden);
    p = mlxr_abr_pick(-7, 98000, ov);
    CHECK(p.kbps == 98000 && !p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 98000, NULL);
    CHECK(p.kbps == 98000 && !p.overridden);
}

static void testLimits(void) {
    int ov[MLXR_ABR_KINDS] = { 400, 900000, 500, 500000 };
    MlxrAbrPick p = mlxr_abr_pick(MLXR_ABR_WIFI, 98000, ov);   // under the floor: held at 500 kbps
    CHECK(p.kbps == 500 && p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_ADAPTER, 98000, ov);            // over the cap: 500 Mbps
    CHECK(p.kbps == 500000 && p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_WIRED, 98000, ov);              // exactly the floor
    CHECK(p.kbps == 500 && p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_USB, 98000, ov);                // exactly the cap
    CHECK(p.kbps == 500000 && p.overridden);
    ov[0] = -5;                                                // a negative one is no override
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 98000, ov);
    CHECK(p.kbps == 98000 && !p.overridden);
    // the launcher's own figure is held to the same range; nothing at all stays nothing
    const int none[MLXR_ABR_KINDS] = { 0, 0, 0, 0 };
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 900000, none);
    CHECK(p.kbps == 500000 && !p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 100, none);
    CHECK(p.kbps == 500 && !p.overridden);
    p = mlxr_abr_pick(MLXR_ABR_WIFI, 0, none);
    CHECK(p.kbps == 0 && !p.overridden);
}

static void testWords(void) {
    char buf[96];
    MlxrAbrPick o = { 60000, 1 }, l = { 98000, 0 };
    mlxr_abr_line(MLXR_ABR_WIFI, &o, buf, sizeof(buf));
    CHECK(strcmp(buf, "bitrate for Wi-Fi: 60000 kbps (override)") == 0);
    mlxr_abr_line(MLXR_ABR_ADAPTER, &l, buf, sizeof(buf));
    CHECK(strcmp(buf, "bitrate for Steam Frame Wireless Adapter: 98000 kbps (launcher)") == 0);
    mlxr_abr_line(MLXR_ABR_WIRED, &l, buf, sizeof(buf));
    CHECK(strcmp(buf, "bitrate for Wired network: 98000 kbps (launcher)") == 0);
    mlxr_abr_line(MLXR_ABR_USB, &o, buf, sizeof(buf));
    CHECK(strcmp(buf, "bitrate for USB cable: 60000 kbps (override)") == 0);
    mlxr_abr_line(MLXR_ABR_UNKNOWN, &l, buf, sizeof(buf));
    CHECK(strcmp(buf, "bitrate for unknown path: 98000 kbps (launcher)") == 0);
    char tiny[12];
    mlxr_abr_line(MLXR_ABR_WIFI, &o, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
}

int main(void) {
    testPick();
    testLimits();
    testWords();
    return checksDone("abr");
}
