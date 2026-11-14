// The heat guard (GitHub issue #6): the headset's CPU and GPU temperatures
// read from the kernel's thermal zones every 5 s, and what the session does
// as they climb with the 3D on. The depth model runs on the CPU, and with the
// 3D on for minutes at a time the Frame has been seen to heat into the 90s C
// and reset by itself. A toast first, then the depth model's rate eased, then
// the 3D turned off.
//
// The sensor is the Linux thermal class's text files
// (/sys/class/thermal/thermal_zone*/type and temp, millidegrees C, readable
// without root on the Frame). The zones are picked by type once, at the start,
// and kept open; each reading is one small read a zone. A machine without the
// zones (a desktop, the tests) reads nothing, and then the guard does nothing.
//
// The guard is plain arithmetic on a clock passed in, with no OpenXR and no
// context, so the host tests reach all of it.
//
// Native (lane heat-guard). GPLv3.
#ifndef XR_HEAT_H
#define XR_HEAT_H

#include <stddef.h>
#include <stdint.h>

// The levels, in whole degrees C on the hotter of the CPU and the GPU. Every
// threshold the guard has is here; MLXR_HEAT_LIMITS="warn,ease,stop" replaces
// the three levels for a test (the gaps below follow them). To be tuned after
// a heat test on the headset: the CPU zones' critical trip is 110 C, the
// others' hot trip 110 C, and a lower skin or power limit may exist that the
// zones do not show.
//
// Warn: the toast says it once, and again only after the heat has gone back
// under HEAT_WARN_C - HEAT_HYST_C and come up again
#define HEAT_WARN_C 90
// Ease, with the 3D on and the depth model running: its rate halved (20 maps/s
// to 10), and halved again (to 5, DEPTH_RATE_MIN at the least) when it is
// still this hot HEAT_HOLD_NS later
#define HEAT_EASE_C 95
// Stop, with the 3D on: the 3D turned off for the session (not saved as the
// switch's setting) and a toast saying why
#define HEAT_STOP_C 100
// The gap under the warn level the heat has to fall through before the
// warning can be said again
#define HEAT_HYST_C 5
// The eased rate doubles back, a step a HEAT_HOLD_NS, once the heat is under
// HEAT_EASE_C - HEAT_RESTORE_GAP_C, until it is back to what it was
#define HEAT_RESTORE_GAP_C 10
// The least time between two moves of the eased rate
#define HEAT_HOLD_NS 30000000000LL
// How often the zones are read: the 5 s stats line's cadence
#define HEAT_PERIOD_NS 5000000000LL

// The toasts, PROPOSED wording awaiting the owner's approval. The first line
// is the toast's, the second the quieter line under it. English only until
// the wording is settled, then into the translated strings.
#define HEAT_TEXT_WARN "Your headset is getting hot."
#define HEAT_TEXT_WARN_3D_MORE "Turn off 3D before it shuts down."
#define HEAT_TEXT_WARN_FLAT_MORE "Lower the refresh rate or turn off the glow."
#define HEAT_TEXT_STOP "3D turned off: your headset is too hot."

// Readings outside this range are a zone that is off or broken, not a heat
#define HEAT_SANE_MIN_MILLI (-40000)
#define HEAT_SANE_MAX_MILLI 200000

// What a zone's type makes it
#define HEAT_ZONE_OTHER 0
#define HEAT_ZONE_CPU 1
#define HEAT_ZONE_GPU 2

#define HEAT_ZONES_MAX 64

// The type's word ("cpu4-top-thermal", "cpuss0-thermal", "gpuss-2-thermal")
// to HEAT_ZONE_*: cpu* is the CPU, gpu* the GPU, anything else neither
int heatZoneKind(const char* type);

// A temp file's text ("45200\n") to millidegrees. 0 when it is not a number
// or is outside the sane range.
int heatParseMilli(const char* text, int* milli);

typedef struct {
    int valid;          // at least one of the two was read
    int haveCpu, haveGpu;
    float cpuC, gpuC;   // the hottest zone of each, degrees C
} HeatReading;

// The hotter of the two that were read; 0 when neither was
float heatHottest(const HeatReading* r);

typedef struct {
    int count;
    int cpuZones, gpuZones;
    int kind[HEAT_ZONES_MAX];
    int fd[HEAT_ZONES_MAX];
    char root[400];
} HeatSensor;

// Finds the CPU and GPU zones under root (NULL: MLXR_THERMAL_ROOT, else
// /sys/class/thermal) and opens their temp files. Returns how many were found;
// 0 is a machine without them, which reads nothing.
int heatSensorOpen(HeatSensor* s, const char* root);

// Reads every zone kept: the hottest of each kind into r. A zone that fails
// to read or reads out of range is left out this time. r->valid is 0 when
// nothing was read.
int heatSensorRead(HeatSensor* s, HeatReading* r);

void heatSensorClose(HeatSensor* s);

// "heat cpu 52 C, gpu 48 C" for the stats line, "heat n/a" with nothing read
void heatText(const HeatReading* r, char* buf, size_t cap);

typedef struct {
    int warnC, easeC, stopC;
    int hystC, restoreGapC;
    int64_t holdNs;
    // 0 reads and logs, but never acts (MLXR_HEAT_GUARD=0)
    int act;
} HeatLimits;

void heatLimitsDefault(HeatLimits* l);

// "warn,ease,stop" (whole degrees C) into l, the rest left as it is. 0, and
// l untouched, unless all three are numbers in order (warn <= ease <= stop)
// between 30 and 150.
int heatLimitsParse(HeatLimits* l, const char* text);

// The defaults with MLXR_HEAT_LIMITS and MLXR_HEAT_GUARD applied
void heatLimitsFromEnv(HeatLimits* l);

// What a step did, as bits
#define HEAT_ACT_WARN 1       // say the warning (3D on or off decides which)
#define HEAT_ACT_EASE 2       // the depth rate's cap went down
#define HEAT_ACT_RESTORE 4    // the cap went back up, or was lifted
#define HEAT_ACT_STOP 8       // turn the 3D off and say why

typedef struct {
    HeatLimits lim;
    // The warning has been said and the heat not yet gone back under its gap
    int warned;
    // The depth rate's cap, maps a second; 0 for none
    int depthCap;
    // When the cap last moved, 0 for never
    int64_t movedNs;
    // The rate before the first cut, which a full restore goes back to
    int capFrom;
    // How many times the 3D was turned off this session
    int stops;
} HeatGuard;

void heatGuardStart(HeatGuard* g, const HeatLimits* lim);

// One reading at nowNs. stereoOn: the 3D is on; modelLive: the depth model is
// making maps for it; rate: the rate the model runs at now (under any cap),
// maps a second. Returns HEAT_ACT_* bits; with nothing read, or the guard not
// acting, 0 and nothing changes. At most one move of each kind a step.
int heatGuardStep(HeatGuard* g, int64_t nowNs, const HeatReading* r, int stereoOn, int modelLive, int rate);

// The depth model's rate under the cap: the governor's target, or the cap
// when that is lower
int heatDepthRate(int target, int cap);

#endif
