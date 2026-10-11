// Moonlight XR Native (lane perf): the host frame cadence (stream/cadence.h):
// a steady 90 fps host, a 360 Hz capture resampled to 90, and a gap. GPLv3.
#include <stdio.h>
#include <string.h>

#include "../xrcore/check.h"
#include "../../stream/cadence.h"

int main(void) {
    char line[400];
    MlxrCadence c;
    memset(&c, 0, sizeof(c));
    // 90 fps exactly (1000 ticks), arriving with no jitter
    for (uint32_t i = 0; i < 450; i++) mlxr_cadence_add(&c, i, 5000 + i * 1000, 1000000 + (uint64_t)i * 11111);
    CHECK(c.intervals == 449 && c.bucket[2] == 449 && c.grid360 == 0);
    mlxr_cadence_line(&c, line, sizeof(line));
    printf("%s\n", line);
    CHECK(strstr(line, "host frames 90.0 fps") != NULL);
    CHECK(c.intervals == 0 && c.have == 1); // a new window, continuity kept

    // a 360 Hz source picked at 90: gaps of 3, 4 and 5 x 2.78 ms
    uint32_t rtp = 0;
    uint32_t steps[] = { 750, 1000, 1250, 1000 };
    for (uint32_t i = 0; i < 400; i++) {
        rtp += steps[i % 4];
        mlxr_cadence_add(&c, 1000 + i, rtp, 2000000 + (uint64_t)rtp * 1000 / 90);
    }
    CHECK(c.grid360 >= 190); // the 750 and 1250 tick gaps
    CHECK(c.bucket[1] >= 99 && c.bucket[3] >= 99);

    // Native (lane refresh): a steady 120 fps stream (750 ticks) is on the 2.78 ms grid but on its
    // own period too, so it is not counted; resampled 360 Hz gaps (500, 1000) still are
    memset(&c, 0, sizeof(c));
    mlxr_cadence_set_fps(&c, 120);
    for (uint32_t i = 0; i < 240; i++) mlxr_cadence_add(&c, i, i * 750, (uint64_t)i * 8333);
    CHECK(c.intervals == 239 && c.grid360 == 0 && c.bucket[1] == 239);
    mlxr_cadence_line(&c, line, sizeof(line));
    printf("%s\n", line);
    CHECK(strstr(line, "host frames 120.0 fps") != NULL && strstr(line, "off the 8.3 ms one") != NULL);
    CHECK(c.periodTicks == 750); // kept for the next window
    rtp = 240 * 750;
    for (uint32_t i = 0; i < 100; i++) {
        rtp += i % 2 ? 500 : 1000;
        mlxr_cadence_add(&c, 240 + i, rtp, 3000000 + (uint64_t)rtp * 1000 / 90);
    }
    CHECK(c.grid360 == 100);
    // 144 fps (625 ticks) is off the 2.78 ms grid altogether
    memset(&c, 0, sizeof(c));
    mlxr_cadence_set_fps(&c, 144);
    for (uint32_t i = 0; i < 288; i++) mlxr_cadence_add(&c, i, i * 625, (uint64_t)i * 6944);
    CHECK(c.grid360 == 0 && c.bucket[1] == 287);
    mlxr_cadence_line(&c, line, sizeof(line));
    CHECK(strstr(line, "host frames 144.0 fps") != NULL && strstr(line, "off the 6.9 ms one") != NULL);

    // a lost frame (number skipped) is no interval; a long host pause is
    memset(&c, 0, sizeof(c));
    mlxr_cadence_add(&c, 1, 1000, 0);
    mlxr_cadence_add(&c, 3, 3000, 22222);
    CHECK(c.intervals == 0);
    mlxr_cadence_add(&c, 4, 3000 + 9000, 122222);
    CHECK(c.intervals == 1 && c.bucket[6] == 1);
    return checksDone("stream cadence");
}
