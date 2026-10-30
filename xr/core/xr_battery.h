// The headset's battery for the clock strip: the Linux power supply class's
// text files (capacity, status) read and parsed. The Frame's cell is
// /sys/class/power_supply/max1720x_bat_7-36 (docs/probe, PLAN 1.14); a PC has
// none, and then the strip shows no battery. Gamepads that report a battery
// through the same class are marked "scope: Device" and are not the headset.
//
// Native (lane wave3-hud). PLAN 3.7. GPLv3.
#ifndef XR_BATTERY_H
#define XR_BATTERY_H

#include <stddef.h>

typedef struct MlxrBattery {
    int present;    // a battery was read
    int percent;    // 0..100
    int charging;   // the status says "Charging"
    int full;       // "Full"
} MlxrBattery;

// The files' text ("87\n", "Discharging\n") into b. 0 when the capacity is not a number.
int mlxr_battery_parse(const char* capacityText, const char* statusText, MlxrBattery* b);

// The battery's directory under root (default /sys/class/power_supply; the
// environment's MLXR_BATTERY_DIR names one directly): the headset's own cell
// when there is one (type Battery, scope not Device, the Frame's max1720x
// first); out is empty and 0 comes back when there is none.
int mlxr_battery_find(const char* root, char* out, size_t cap);

// Reads the battery in dir. 0 (and b->present 0) when the files are not there.
int mlxr_battery_read(const char* dir, MlxrBattery* b);

// "87%" for the strip; "" when not present
void mlxr_battery_text(const MlxrBattery* b, char* buf, size_t cap);

#endif
