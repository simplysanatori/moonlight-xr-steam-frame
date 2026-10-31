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

// Native (lane flat-input): the start decision keeps any figure up to 500 Mbps (the owner saw 150 Mbps at most), and
// says where it came from
static void testStart(void) {
    const int none[MLXR_ABR_KINDS] = { 0, 0, 0, 0 };
    char buf[256];
    // --bitrate 300000 (the flat launcher's Video bitrate or its Custom bitrate): asked for as it is
    MlxrAbrStart st = mlxr_abr_start(MLXR_ABR_WIFI, 300000, 0, 150000, none);
    CHECK(st.kbps == 300000 && st.source == MLXR_BITRATE_FROM_ARG && st.asked == 300000);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strcmp(buf, "stream bitrate: 300000 kbps (300 Mbps) over Wi-Fi, from the flat launcher's Video bitrate (--bitrate)") == 0);
    // --bitrate wins over config.ini's bitrate_kbps, which wins over the default curve
    st = mlxr_abr_start(MLXR_ABR_WIRED, 0, 300000, 150000, none);
    CHECK(st.kbps == 300000 && st.source == MLXR_BITRATE_FROM_CONFIG);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strcmp(buf, "stream bitrate: 300000 kbps (300 Mbps) over a wired network, from config.ini [stream] bitrate_kbps") == 0);
    st = mlxr_abr_start(MLXR_ABR_WIRED, 250000, 300000, 150000, none);
    CHECK(st.kbps == 250000 && st.source == MLXR_BITRATE_FROM_ARG);
    st = mlxr_abr_start(MLXR_ABR_UNKNOWN, 0, 0, 98000, none);
    CHECK(st.kbps == 98000 && st.source == MLXR_BITRATE_FROM_DEFAULT);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strcmp(buf, "stream bitrate: 98000 kbps (98 Mbps) over an unknown network path, from the default for this resolution "
                      "and frame rate") == 0);
    // a connection's own row (300 Mbps over the USB cable) replaces the Video bitrate there, and only there
    const int usb[MLXR_ABR_KINDS] = { 0, 0, 0, 300000 };
    st = mlxr_abr_start(MLXR_ABR_USB, 120000, 0, 150000, usb);
    CHECK(st.kbps == 300000 && st.source == MLXR_BITRATE_FROM_PATH);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strcmp(buf, "stream bitrate: 300000 kbps (300 Mbps) over the USB cable, from config.ini [stream] bitrate_usb "
                      "(Bitrate over the USB cable)") == 0);
    st = mlxr_abr_start(MLXR_ABR_WIFI, 120000, 0, 150000, usb);
    CHECK(st.kbps == 120000 && st.source == MLXR_BITRATE_FROM_ARG);
    // the 500 Mbps cap and the 500 kbps floor, said in the line; odd figures shown exactly
    st = mlxr_abr_start(MLXR_ABR_ADAPTER, 900000, 0, 0, none);
    CHECK(st.kbps == 500000 && st.asked == 900000);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "500000 kbps (500 Mbps) over the Steam Frame Wireless Adapter") != NULL);
    CHECK(strstr(buf, ", asked for 900000 kbps, held to the 500 Mbps cap") != NULL);
    st = mlxr_abr_start(MLXR_ABR_WIFI, 123456, 0, 0, none);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "123456 kbps (123.456 Mbps)") != NULL && strstr(buf, "held") == NULL);
    st = mlxr_abr_start(MLXR_ABR_WIFI, 87500, 0, 0, none);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "87500 kbps (87.5 Mbps)") != NULL);
    st = mlxr_abr_start(MLXR_ABR_WIFI, 200, 0, 0, none);
    mlxr_abr_start_line(&st, buf, sizeof(buf));
    CHECK(st.kbps == 500 && strstr(buf, "held to the 500 kbps floor") != NULL);
    // every figure from 150 to 500 Mbps survives as it is
    for (int k = 150000; k <= 500000; k += 500) {
        st = mlxr_abr_start(MLXR_ABR_WIFI, k, 0, 0, none);
        if (st.kbps != k) { CHECK(st.kbps == k); break; }
    }
    CHECK(mlxr_abr_key(MLXR_ABR_ADAPTER) && strcmp(mlxr_abr_key(MLXR_ABR_ADAPTER), "bitrate_adapter") == 0);
    CHECK(mlxr_abr_key(MLXR_ABR_UNKNOWN) == NULL);
    char tiny[16];
    mlxr_abr_start_line(&st, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
}

int main(void) {
    testStart();
    testPick();
    testLimits();
    testWords();
    return checksDone("abr");
}
