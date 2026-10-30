// The headset's battery for the clock strip (xr_battery.c): the sysfs text parsed,
// and the right power supply found among a fake /sys/class/power_supply.
// Native (lane wave3-hud).
#define _GNU_SOURCE
#include "check.h"
#include "xr_battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[256];

static void put(const char* dir, const char* name, const char* text) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s/%s", g_root, dir, name);
    FILE* f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void supply(const char* dir, const char* type, const char* scope, const char* capacity,
                   const char* status) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", g_root, dir);
    mkdir(path, 0755);
    put(dir, "type", type);
    if (scope != NULL) put(dir, "scope", scope);
    if (capacity != NULL) put(dir, "capacity", capacity);
    if (status != NULL) put(dir, "status", status);
}

static void testParse(void) {
    MlxrBattery b;
    // what the Frame's cell printed in the probe: 98 and Discharging
    CHECK(mlxr_battery_parse("98\n", "Discharging\n", &b) == 1);
    CHECK(b.present && b.percent == 98 && !b.charging && !b.full);
    CHECK(mlxr_battery_parse("57", "Charging", &b) == 1);
    CHECK(b.percent == 57 && b.charging && !b.full);
    CHECK(mlxr_battery_parse("100\n", "Full\n", &b) == 1);
    CHECK(b.percent == 100 && !b.charging && b.full);
    CHECK(mlxr_battery_parse("100\n", "Not charging\n", &b) == 1);
    CHECK(!b.charging && !b.full);
    CHECK(mlxr_battery_parse("0\n", "Unknown\n", &b) == 1);
    CHECK(b.present && b.percent == 0);
    // no status file is still a percentage
    CHECK(mlxr_battery_parse("42\n", NULL, &b) == 1);
    CHECK(b.percent == 42 && !b.charging);
    // not a battery reading
    CHECK(mlxr_battery_parse("", "Charging", &b) == 0 && !b.present);
    CHECK(mlxr_battery_parse("abc\n", "Charging", &b) == 0 && !b.present);
    CHECK(mlxr_battery_parse("101\n", "Charging", &b) == 0);
    CHECK(mlxr_battery_parse("-3\n", "Charging", &b) == 0);
    CHECK(mlxr_battery_parse(NULL, "Charging", &b) == 0);

    char text[16];
    mlxr_battery_parse("87\n", "Discharging\n", &b);
    mlxr_battery_text(&b, text, sizeof(text));
    CHECK(strcmp(text, "87%") == 0);
    memset(&b, 0, sizeof(b));
    mlxr_battery_text(&b, text, sizeof(text));
    CHECK(text[0] == '\0');
}

static void testFind(void) {
    char dir[600];
    MlxrBattery b;
    unsetenv("MLXR_BATTERY_DIR");
    // a desktop: no power supplies at all, or only the wall
    CHECK(!mlxr_battery_find("/nonexistent/power_supply", dir, sizeof(dir)));
    CHECK(dir[0] == '\0');
    supply("AC", "Mains", NULL, NULL, NULL);
    CHECK(!mlxr_battery_find(g_root, dir, sizeof(dir)));
    // a gamepad's cell (scope Device) is not the headset's
    supply("hid-0003-0001-battery", "Battery", "Device", "55\n", "Discharging\n");
    CHECK(!mlxr_battery_find(g_root, dir, sizeof(dir)));
    // the Frame: its cell and a charger that says Unknown
    supply("pm8550b-charger", "Unknown", NULL, NULL, "Charging\n");
    supply("max1720x_bat_7-36", "Battery", NULL, "98\n", "Discharging\n");
    CHECK(mlxr_battery_find(g_root, dir, sizeof(dir)));
    char want[600];
    snprintf(want, sizeof(want), "%s/max1720x_bat_7-36", g_root);
    CHECK(strcmp(dir, want) == 0);
    CHECK(mlxr_battery_read(dir, &b) == 1);
    CHECK(b.present && b.percent == 98 && !b.charging);
    put("max1720x_bat_7-36", "capacity", "99\n");
    put("max1720x_bat_7-36", "status", "Charging\n");
    CHECK(mlxr_battery_read(dir, &b) == 1);
    CHECK(b.percent == 99 && b.charging);
    // the Frame's cell wins over another that is not scope Device (a laptop's)
    supply("BAT0", "Battery", NULL, "10\n", "Discharging\n");
    CHECK(mlxr_battery_find(g_root, dir, sizeof(dir)));
    CHECK(strcmp(dir, want) == 0);
    // read of a directory with no files
    CHECK(mlxr_battery_read("/nonexistent/bat", &b) == 0 && !b.present);
    CHECK(mlxr_battery_read(NULL, &b) == 0);

    // the environment names one directly
    char over[600];
    snprintf(over, sizeof(over), "%s/BAT0", g_root);
    setenv("MLXR_BATTERY_DIR", over, 1);
    CHECK(mlxr_battery_find("/nonexistent", dir, sizeof(dir)));
    CHECK(strcmp(dir, over) == 0);
    CHECK(mlxr_battery_read(dir, &b) == 1 && b.percent == 10);
    unsetenv("MLXR_BATTERY_DIR");
}

int main(void) {
    snprintf(g_root, sizeof(g_root), "/tmp/mlxr-battery-test-XXXXXX");
    if (mkdtemp(g_root) == NULL) {
        fprintf(stderr, "no temporary directory\n");
        return 1;
    }
    testParse();
    testFind();
    char cmd[320];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    if (system(cmd) != 0) {
        fprintf(stderr, "could not remove %s\n", g_root);
    }
    return checksDone("battery");
}
