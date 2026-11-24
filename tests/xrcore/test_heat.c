// The heat guard (xr_heat.c): the thermal zones' text parsed, the CPU and GPU
// zones found among a fake /sys/class/thermal, and the guard's steps (warn,
// ease, stop, the gaps that keep it from flapping, the 3D on and off, the rate
// given back).
// Native (lane heat-guard).
#define _GNU_SOURCE
#include "check.h"
#include "xr_heat.h"
#include "xr_shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SEC 1000000000LL

static char g_root[256];

static void put(const char* zone, const char* name, const char* text) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s/%s", g_root, zone, name);
    FILE* f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void zone(const char* name, const char* type, const char* temp) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", g_root, name);
    mkdir(path, 0755);
    put(name, "type", type);
    if (temp != NULL) put(name, "temp", temp);
}

static void testParse(void) {
    int m = -1;
    CHECK(heatParseMilli("45200\n", &m) && m == 45200);
    CHECK(heatParseMilli("  91000 ", &m) && m == 91000);
    CHECK(heatParseMilli("-5000\n", &m) && m == -5000);
    // not a number, a number with junk after it, or no reading at all
    CHECK(!heatParseMilli("", &m));
    CHECK(!heatParseMilli("abc\n", &m));
    CHECK(!heatParseMilli("45x\n", &m));
    CHECK(!heatParseMilli(NULL, &m));
    // a zone that is off or broken reads far out of range
    CHECK(!heatParseMilli("-273000\n", &m));
    CHECK(!heatParseMilli("255000\n", &m));

    CHECK(heatZoneKind("cpu0-thermal") == HEAT_ZONE_CPU);
    CHECK(heatZoneKind("cpu7-bottom-thermal") == HEAT_ZONE_CPU);
    CHECK(heatZoneKind("cpuss2-thermal") == HEAT_ZONE_CPU);
    CHECK(heatZoneKind("gpuss-3-thermal") == HEAT_ZONE_GPU);
    CHECK(heatZoneKind("aoss0-thermal") == HEAT_ZONE_OTHER);
    CHECK(heatZoneKind("ddr-thermal") == HEAT_ZONE_OTHER);
    CHECK(heatZoneKind("pm8550-thermal") == HEAT_ZONE_OTHER);
    CHECK(heatZoneKind(NULL) == HEAT_ZONE_OTHER);

    HeatReading r;
    memset(&r, 0, sizeof(r));
    char text[64];
    heatText(&r, text, sizeof(text));
    CHECK(strcmp(text, "heat n/a") == 0);
    CHECK(heatHottest(&r) == 0.0f);
    r.valid = r.haveCpu = r.haveGpu = 1;
    r.cpuC = 91.4f;
    r.gpuC = 82.6f;
    heatText(&r, text, sizeof(text));
    CHECK(strcmp(text, "heat cpu 91 C, gpu 83 C") == 0);
    CHECK_NEAR(heatHottest(&r), 91.4, 1e-4);
    r.haveCpu = 0;
    heatText(&r, text, sizeof(text));
    CHECK(strcmp(text, "heat cpu n/a, gpu 83 C") == 0);
    CHECK_NEAR(heatHottest(&r), 82.6, 1e-4);
}

static void testSensor(void) {
    HeatSensor s;
    HeatReading r;
    unsetenv("MLXR_THERMAL_ROOT");
    // a desktop without the zones: nothing found, nothing read
    CHECK(heatSensorOpen(&s, "/nonexistent/thermal") == 0);
    CHECK(!heatSensorRead(&s, &r) && !r.valid);
    heatSensorClose(&s);

    // the Frame's kinds of zone, at idle
    zone("thermal_zone0", "aoss0-thermal\n", "99000\n");
    zone("thermal_zone1", "cpu0-thermal\n", "41300\n");
    zone("thermal_zone2", "cpu7-top-thermal\n", "46100\n");
    zone("thermal_zone3", "cpuss0-thermal\n", "44000\n");
    zone("thermal_zone4", "gpuss-0-thermal\n", "38200\n");
    zone("thermal_zone5", "gpuss-4-thermal\n", "39900\n");
    zone("thermal_zone6", "ddr-thermal\n", "97000\n");
    // a CPU zone without a temp file, and one that reads nothing sane
    zone("thermal_zone7", "cpu3-thermal\n", NULL);
    zone("thermal_zone8", "cpu4-thermal\n", "-273000\n");
    // a cooling device beside them is not a zone
    zone("cooling_device0", "cpu-cluster0\n", NULL);
    put("cooling_device0", "temp", "120000\n");

    CHECK(heatSensorOpen(&s, g_root) == 6);
    CHECK(s.cpuZones == 4 && s.gpuZones == 2);
    CHECK(heatSensorRead(&s, &r) && r.valid);
    // the hottest of each kind; aoss and ddr are neither
    CHECK(r.haveCpu && r.haveGpu);
    CHECK_NEAR(r.cpuC, 46.1, 1e-3);
    CHECK_NEAR(r.gpuC, 39.9, 1e-3);
    // read again from the open files, as the kernel measures again
    put("thermal_zone3", "temp", "92500\n");
    put("thermal_zone4", "temp", "81000\n");
    CHECK(heatSensorRead(&s, &r));
    CHECK_NEAR(r.cpuC, 92.5, 1e-3);
    CHECK_NEAR(r.gpuC, 81.0, 1e-3);
    // a zone gone bad is left out, the rest still read
    put("thermal_zone3", "temp", "garbage\n");
    CHECK(heatSensorRead(&s, &r));
    CHECK_NEAR(r.cpuC, 46.1, 1e-3);
    heatSensorClose(&s);

    // the environment names the root for a test; an empty root reads nothing
    setenv("MLXR_THERMAL_ROOT", g_root, 1);
    CHECK(heatSensorOpen(&s, NULL) == 6);
    heatSensorClose(&s);
    setenv("MLXR_THERMAL_ROOT", "/nonexistent/thermal", 1);
    CHECK(heatSensorOpen(&s, NULL) == 0);
    heatSensorClose(&s);
    unsetenv("MLXR_THERMAL_ROOT");

    // only GPU zones: the CPU is not read, the GPU is
    char only[300];
    snprintf(only, sizeof(only), "%s/gpuonly", g_root);
    mkdir(only, 0755);
    zone("gpuonly/thermal_zone0", "gpuss-1-thermal\n", "77000\n");
    CHECK(heatSensorOpen(&s, only) == 1);
    CHECK(heatSensorRead(&s, &r) && !r.haveCpu && r.haveGpu);
    CHECK_NEAR(heatHottest(&r), 77.0, 1e-3);
    heatSensorClose(&s);
}

// Native (1.3.4): whether a chip is slowed for heat, and the GPU's clock
static void testSlowed(void) {
    CHECK(heatCoolKind("cpufreq-cpu0") == HEAT_ZONE_CPU);
    CHECK(heatCoolKind("cpufreq-cpu7") == HEAT_ZONE_CPU);
    CHECK(heatCoolKind("devfreq-3d00000.gpu") == HEAT_ZONE_GPU);
    CHECK(heatCoolKind("devfreq-1d84000.ufshc") == HEAT_ZONE_OTHER);
    CHECK(heatCoolKind("ath12k_thermal") == HEAT_ZONE_OTHER);
    CHECK(heatCoolKind("cpu-cluster0") == HEAT_ZONE_OTHER);
    CHECK(heatCoolKind(NULL) == HEAT_ZONE_OTHER);
    CHECK(heatDevfreqIsGpu("3d00000.gpu") && !heatDevfreqIsGpu("1d84000.ufshc") && !heatDevfreqIsGpu(".gpu")
          && !heatDevfreqIsGpu("") && !heatDevfreqIsGpu(NULL));
    int v = -1;
    CHECK(heatParseState("0\n", &v) && v == 0);
    CHECK(heatParseState("31\n", &v) && v == 31);
    CHECK(!heatParseState("", &v) && !heatParseState("x\n", &v) && !heatParseState("-1\n", &v)
          && !heatParseState("3x\n", &v) && !heatParseState("99999\n", &v) && !heatParseState(NULL, &v));
    CHECK(heatParseFreqMhz("903000000\n", &v) && v == 903);
    CHECK(heatParseFreqMhz("231000000\n", &v) && v == 231);
    CHECK(!heatParseFreqMhz("0\n", &v) && !heatParseFreqMhz("903 MHz\n", &v) && !heatParseFreqMhz(NULL, &v));

    // the Frame's devices, by type and not by number
    char root[300], freq[300];
    snprintf(root, sizeof(root), "%s/slow", g_root);
    snprintf(freq, sizeof(freq), "%s/devfreq", g_root);
    mkdir(root, 0755);
    mkdir(freq, 0755);
    static const char* const TYPES[] = { "ath12k_thermal\n", "cpufreq-cpu0\n", "cpufreq-cpu2\n", "devfreq-3d00000.gpu\n",
                                         "cpufreq-cpu5\n", "cpufreq-cpu7\n" };
    for (int i = 0; i < 6; i++) {
        char name[64];
        snprintf(name, sizeof(name), "slow/cooling_device%d", i);
        zone(name, TYPES[i], NULL);
        put(name, "cur_state", i == 0 ? "7\n" : "0\n");
    }
    zone("slow/thermal_zone0", "cpu0-thermal\n", "72000\n");
    zone("slow/thermal_zone1", "gpuss-0-thermal\n", "68000\n");
    zone("devfreq/1d84000.ufshc", "x\n", NULL);
    put("devfreq/1d84000.ufshc", "cur_freq", "300000000\n");
    zone("devfreq/3d00000.gpu", "x\n", NULL);
    put("devfreq/3d00000.gpu", "cur_freq", "903000000\n");
    setenv("MLXR_DEVFREQ_ROOT", freq, 1);
    HeatSensor s;
    HeatReading r;
    CHECK(heatSensorOpen(&s, root) == 2);
    CHECK(s.cpuCools == 4 && s.gpuCools == 1 && s.freqCount == 1);
    CHECK(heatSensorRead(&s, &r));
    CHECK(r.haveCpuSlow && r.haveGpuSlow && r.cpuSlow == 0 && r.gpuSlow == 0);
    CHECK(r.haveGpuMhz && r.gpuMhz == 903);
    char buf[HEAT_TEXT_CAP];
    heatText(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "heat cpu 72 C, gpu 68 C, gpu 903 MHz, not slowed") == 0);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset CPU 72 \xC2\xB0" "C, GPU 68 \xC2\xB0" "C at 903 MHz") == 0);
    // read again from the open files: the most slowed of the CPU's
    put("slow/cooling_device2", "cur_state", "3\n");
    put("slow/cooling_device5", "cur_state", "12\n");
    put("devfreq/3d00000.gpu", "cur_freq", "578000000\n");
    CHECK(heatSensorRead(&s, &r) && r.cpuSlow == 12 && r.gpuSlow == 0 && r.gpuMhz == 578);
    heatText(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "heat cpu 72 C, gpu 68 C, gpu 578 MHz, slowed cpu 12") == 0);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset CPU 72 \xC2\xB0" "C, GPU 68 \xC2\xB0" "C at 578 MHz\nCPU slowed for heat") == 0);
    put("slow/cooling_device3", "cur_state", "2\n");
    CHECK(heatSensorRead(&s, &r) && r.gpuSlow == 2);
    heatText(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "heat cpu 72 C, gpu 68 C, gpu 578 MHz, slowed cpu 12, gpu 2") == 0);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strstr(buf, "\nCPU and GPU slowed for heat") != NULL);
    put("slow/cooling_device2", "cur_state", "0\n");
    put("slow/cooling_device5", "cur_state", "0\n");
    CHECK(heatSensorRead(&s, &r) && r.cpuSlow == 0 && r.gpuSlow == 2);
    heatText(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "heat cpu 72 C, gpu 68 C, gpu 578 MHz, slowed gpu 2") == 0);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strstr(buf, "\nGPU slowed for heat") != NULL);
    // the guard reads the heat alone: slowed or not, cool is cool
    HeatGuard g;
    heatGuardStart(&g, NULL);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, 20) == 0 && g.depthCap == 0);
    // files gone bad: left out, the rest still read
    put("slow/cooling_device3", "cur_state", "garbage\n");
    put("devfreq/3d00000.gpu", "cur_freq", "0\n");
    CHECK(heatSensorRead(&s, &r) && r.haveCpuSlow && !r.haveGpuSlow && r.gpuSlow == 0 && !r.haveGpuMhz);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset CPU 72 \xC2\xB0" "C, GPU 68 \xC2\xB0" "C") == 0);
    heatSensorClose(&s);
    CHECK(s.coolCount == 0 && s.freqCount == 0);

    // the clock without a temperature, and neither with a chip slowed
    memset(&r, 0, sizeof(r));
    r.haveGpuMhz = 1;
    r.gpuMhz = 231;
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset GPU 231 MHz") == 0);
    heatText(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "heat n/a, gpu 231 MHz") == 0);
    r.haveGpuMhz = 0;
    r.haveGpuSlow = 1;
    r.gpuSlow = 1;
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "GPU slowed for heat") == 0);
    // a short buffer is not run past
    char tiny[12];
    r.valid = r.haveCpu = r.haveGpu = r.haveGpuMhz = 1;
    heatStatsLine(&r, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
    heatText(&r, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));

    // a machine with neither class shows neither
    setenv("MLXR_DEVFREQ_ROOT", "/nonexistent/devfreq", 1);
    CHECK(heatSensorOpen(&s, "/nonexistent/thermal") == 0);
    CHECK(!heatSensorRead(&s, &r) && !r.haveCpuSlow && !r.haveGpuSlow && !r.haveGpuMhz);
    heatSensorClose(&s);
    // the clock alone is still read where there are no thermal zones, and the guard has nothing to act on
    setenv("MLXR_DEVFREQ_ROOT", freq, 1);
    put("devfreq/3d00000.gpu", "cur_freq", "500000000\n");
    CHECK(heatSensorOpen(&s, "/nonexistent/thermal") == 0);
    CHECK(!heatSensorRead(&s, &r) && !r.valid && r.haveGpuMhz && r.gpuMhz == 500);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, 20) == 0);
    heatSensorClose(&s);
    unsetenv("MLXR_DEVFREQ_ROOT");
}

static void testLimits(void) {
    HeatLimits l;
    heatLimitsDefault(&l);
    CHECK(l.warnC == HEAT_WARN_C && l.easeC == HEAT_EASE_C && l.stopC == HEAT_STOP_C && l.act);
    // the defaults in order, with the restore under the ease and the warning's gap
    CHECK(HEAT_WARN_C <= HEAT_EASE_C && HEAT_EASE_C <= HEAT_STOP_C);
    CHECK(HEAT_EASE_C - HEAT_RESTORE_GAP_C < HEAT_WARN_C);
    CHECK(heatLimitsParse(&l, "60,65,70") && l.warnC == 60 && l.easeC == 65 && l.stopC == 70);
    CHECK(heatLimitsParse(&l, " 50 , 55 , 55 ") && l.stopC == 55);
    // out of order, out of range, too few or too many: untouched
    CHECK(!heatLimitsParse(&l, "70,65,80") && l.warnC == 50);
    CHECK(!heatLimitsParse(&l, "20,65,80"));
    CHECK(!heatLimitsParse(&l, "60,65,200"));
    CHECK(!heatLimitsParse(&l, "60,65"));
    CHECK(!heatLimitsParse(&l, "60,65,70,75"));
    CHECK(!heatLimitsParse(&l, NULL));
    CHECK(l.warnC == 50 && l.easeC == 55 && l.stopC == 55);

    setenv("MLXR_HEAT_LIMITS", "70,75,80", 1);
    setenv("MLXR_HEAT_GUARD", "0", 1);
    heatLimitsFromEnv(&l);
    CHECK(l.warnC == 70 && l.easeC == 75 && l.stopC == 80 && !l.act);
    unsetenv("MLXR_HEAT_LIMITS");
    unsetenv("MLXR_HEAT_GUARD");
    heatLimitsFromEnv(&l);
    CHECK(l.warnC == HEAT_WARN_C && l.act);
}

static HeatReading at(float cpu, float gpu) {
    HeatReading r;
    memset(&r, 0, sizeof(r));
    r.valid = r.haveCpu = r.haveGpu = 1;
    r.cpuC = cpu;
    r.gpuC = gpu;
    return r;
}

static void testWarn(void) {
    HeatGuard g;
    heatGuardStart(&g, NULL);
    HeatReading r = at(60, 50);
    int64_t t = 0;
    CHECK(heatGuardStep(&g, t, &r, 1, 1, 20) == 0);
    // the GPU alone reaching the level is enough
    r = at(70, 90);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == HEAT_ACT_WARN);
    // said once: still hot, hotter, or just under the level is not news
    r = at(70, 92);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(88, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(90, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    // under the gap and back: said again
    r = at(85.5f, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(90, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(84.9f, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(90.2f, 70);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 1, 1, 20) == HEAT_ACT_WARN);

    // nothing read: nothing changes, whatever was said before
    HeatReading none;
    memset(&none, 0, sizeof(none));
    heatGuardStart(&g, NULL);
    CHECK(heatGuardStep(&g, t, &none, 1, 1, 20) == 0);
    CHECK(heatGuardStep(&g, t, NULL, 1, 1, 20) == 0);
    CHECK(!g.warned && g.depthCap == 0 && g.stops == 0);

    // a guard told not to act (MLXR_HEAT_GUARD=0) never does
    HeatLimits l;
    heatLimitsDefault(&l);
    l.act = 0;
    heatGuardStart(&g, &l);
    r = at(105, 105);
    CHECK(heatGuardStep(&g, t, &r, 1, 1, 20) == 0);
    CHECK(g.depthCap == 0 && g.stops == 0);
}

static void testStatsLine(void) {
    char buf[64];
    HeatReading r = at(72.4f, 68.6f);
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset CPU 72 \xC2\xB0" "C, GPU 69 \xC2\xB0" "C") == 0);
    r.haveGpu = 0;
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset CPU 72 \xC2\xB0" "C") == 0);
    r.haveCpu = 0;
    r.haveGpu = 1;
    heatStatsLine(&r, buf, sizeof(buf));
    CHECK(strcmp(buf, "Headset GPU 69 \xC2\xB0" "C") == 0);
    HeatReading none;
    memset(&none, 0, sizeof(none));
    heatStatsLine(&none, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
    heatStatsLine(NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
}

static void testWarnRepeats(void) {
    // Retest 2026-11-15 03:19: warned at 92 C, then 89-98 C for minutes; the warning comes
    // back every HEAT_REWARN_NS while it stays at or over the level
    HeatGuard g;
    heatGuardStart(&g, NULL);
    HeatReading r = at(92, 88);
    int64_t t = 10 * SEC;
    CHECK(heatGuardStep(&g, t, &r, 1, 0, 20) == HEAT_ACT_WARN);
    int64_t said = t;
    int warns = 0;
    while (t < said + HEAT_REWARN_NS - 5 * SEC) {
        if (heatGuardStep(&g, t += 5 * SEC, &r, 1, 0, 20) & HEAT_ACT_WARN) warns++;
    }
    CHECK(warns == 0);
    CHECK(heatGuardStep(&g, said + HEAT_REWARN_NS, &r, 1, 0, 20) == HEAT_ACT_WARN);
    // dipping just under the level (not under the gap) and back: not again before its time
    r = at(89, 80);
    CHECK(heatGuardStep(&g, said + HEAT_REWARN_NS + 5 * SEC, &r, 1, 0, 20) == 0);
    r = at(91, 80);
    CHECK(heatGuardStep(&g, said + HEAT_REWARN_NS + 10 * SEC, &r, 1, 0, 20) == 0);
    // under the level when its time comes: nothing; back over it: said
    r = at(89, 80);
    CHECK(heatGuardStep(&g, said + 2 * HEAT_REWARN_NS, &r, 1, 0, 20) == 0);
    r = at(90, 80);
    CHECK(heatGuardStep(&g, said + 2 * HEAT_REWARN_NS + 5 * SEC, &r, 1, 0, 20) == HEAT_ACT_WARN);
}

static void testEase(void) {
    HeatGuard g;
    heatGuardStart(&g, NULL);
    int64_t t = 100 * SEC;
    // at the ease level with the model running: warned, and the rate halved
    HeatReading r = at(95, 80);
    CHECK(heatGuardStep(&g, t, &r, 1, 1, 20) == (HEAT_ACT_WARN | HEAT_ACT_EASE));
    CHECK(g.depthCap == 10 && heatDepthRate(20, g.depthCap) == 10);
    // still that hot: held HEAT_HOLD_NS before the next cut
    CHECK(heatGuardStep(&g, t + 5 * SEC, &r, 1, 1, 10) == 0);
    CHECK(heatGuardStep(&g, t + HEAT_HOLD_NS - 1, &r, 1, 1, 10) == 0);
    CHECK(heatGuardStep(&g, t + HEAT_HOLD_NS, &r, 1, 1, 10) == HEAT_ACT_EASE);
    CHECK(g.depthCap == 5);
    // the floor: no cut below DEPTH_RATE_MIN, and saying nothing about it
    t += HEAT_HOLD_NS;
    CHECK(heatGuardStep(&g, t + HEAT_HOLD_NS, &r, 1, 1, 5) == 0);
    CHECK(g.depthCap == DEPTH_RATE_MIN);
    // between the restore level and the ease level it stays where it is
    r = at(88, 80);
    CHECK(heatGuardStep(&g, t + 2 * HEAT_HOLD_NS, &r, 1, 1, 5) == 0);
    CHECK(g.depthCap == 5);
    // cooled under the restore level: doubled back a step a hold
    r = at(84, 80);
    t += 2 * HEAT_HOLD_NS;
    CHECK(heatGuardStep(&g, t, &r, 1, 1, 5) == HEAT_ACT_RESTORE);
    CHECK(g.depthCap == 10);
    CHECK(heatGuardStep(&g, t + 5 * SEC, &r, 1, 1, 10) == 0);
    CHECK(heatGuardStep(&g, t + HEAT_HOLD_NS, &r, 1, 1, 10) == HEAT_ACT_RESTORE);
    // back to what it was: the cap lifted
    CHECK(g.depthCap == 0 && heatDepthRate(20, g.depthCap) == 20);
    CHECK(heatGuardStep(&g, t + 3 * HEAT_HOLD_NS, &r, 1, 1, 20) == 0);

    // the restore goes on with the 3D off, the cut does not happen without the model
    heatGuardStart(&g, NULL);
    t = 0;
    r = at(96, 70);
    CHECK(heatGuardStep(&g, t, &r, 1, 0, 20) == HEAT_ACT_WARN);
    CHECK(g.depthCap == 0);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 1, 1, 20) == HEAT_ACT_EASE);
    CHECK(g.depthCap == 10);
    r = at(60, 50);
    CHECK(heatGuardStep(&g, t += HEAT_HOLD_NS, &r, 0, 0, 10) == HEAT_ACT_RESTORE);
    CHECK(g.depthCap == 0);

    // the first cut halves the rate the model runs at now, a governor's cut included
    heatGuardStart(&g, NULL);
    r = at(97, 70);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, 12) & HEAT_ACT_EASE);
    CHECK(g.depthCap == 6);
    // a rate already at the floor is not cut
    heatGuardStart(&g, NULL);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, DEPTH_RATE_MIN) == HEAT_ACT_WARN);
    CHECK(g.depthCap == 0);

    CHECK(heatDepthRate(20, 0) == 20);
    CHECK(heatDepthRate(8, 10) == 8);
    CHECK(heatDepthRate(20, 5) == 5);
}

static void testStop(void) {
    HeatGuard g;
    heatGuardStart(&g, NULL);
    int64_t t = 0;
    HeatReading r = at(92, 80);
    CHECK(heatGuardStep(&g, t, &r, 1, 1, 20) == HEAT_ACT_WARN);
    r = at(100, 80);
    // the 3D goes off; the stop says why, so no warning beside it and no cut
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 1, 1, 20) == HEAT_ACT_STOP);
    CHECK(g.stops == 1 && g.depthCap == 0);
    // with the 3D off nothing more happens, however hot
    r = at(104, 90);
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 0, 0, 20) == 0);
    // turned back on while still that hot: off again
    CHECK(heatGuardStep(&g, t += 5 * SEC, &r, 1, 1, 20) == HEAT_ACT_STOP);
    CHECK(g.stops == 2);
    // a jump straight past the warning to the stop says only the stop
    heatGuardStart(&g, NULL);
    r = at(60, 101);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, 20) == HEAT_ACT_STOP);
    // and once cooled, the warning is said again with the 3D off
    r = at(80, 70);
    CHECK(heatGuardStep(&g, 5 * SEC, &r, 0, 0, 20) == 0);
    r = at(91, 70);
    CHECK(heatGuardStep(&g, 10 * SEC, &r, 0, 0, 20) == HEAT_ACT_WARN);
    // the stop level with the 3D off is only the warning's business
    heatGuardStart(&g, NULL);
    r = at(101, 70);
    CHECK(heatGuardStep(&g, 0, &r, 0, 0, 20) == HEAT_ACT_WARN);
    CHECK(g.stops == 0);

    // limits from a test: the same steps at lower heat
    HeatLimits l;
    heatLimitsDefault(&l);
    CHECK(heatLimitsParse(&l, "50,55,60"));
    heatGuardStart(&g, &l);
    r = at(56, 40);
    CHECK(heatGuardStep(&g, 0, &r, 1, 1, 20) == (HEAT_ACT_WARN | HEAT_ACT_EASE));
    r = at(61, 40);
    CHECK(heatGuardStep(&g, 5 * SEC, &r, 1, 1, 10) == HEAT_ACT_STOP);
    r = at(44, 40);
    CHECK(heatGuardStep(&g, HEAT_HOLD_NS, &r, 0, 0, 10) == HEAT_ACT_RESTORE);
}

int main(void) {
    snprintf(g_root, sizeof(g_root), "/tmp/mlxr-heat-test-XXXXXX");
    if (mkdtemp(g_root) == NULL) {
        fprintf(stderr, "no temporary directory\n");
        return 1;
    }
    testParse();
    testSensor();
    testSlowed();
    testLimits();
    testWarn();
    testWarnRepeats();
    testStatsLine();
    testEase();
    testStop();
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    if (system(cmd) != 0) {
        fprintf(stderr, "could not remove %s\n", g_root);
    }
    return checksDone("heat");
}
