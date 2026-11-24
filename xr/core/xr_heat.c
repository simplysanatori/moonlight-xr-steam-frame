// See xr_heat.h. Native (lane heat-guard). GPLv3.
#define _GNU_SOURCE
#include "xr_heat.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xr_shared.h"

int heatZoneKind(const char* type) {
    if (type == NULL) {
        return HEAT_ZONE_OTHER;
    }
    if (strncmp(type, "cpu", 3) == 0) {
        return HEAT_ZONE_CPU;
    }
    if (strncmp(type, "gpu", 3) == 0) {
        return HEAT_ZONE_GPU;
    }
    return HEAT_ZONE_OTHER;
}

int heatParseMilli(const char* text, int* milli) {
    if (text == NULL) {
        return 0;
    }
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    char* end = NULL;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (end == text || errno != 0) {
        return 0;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0' || v < HEAT_SANE_MIN_MILLI || v > HEAT_SANE_MAX_MILLI) {
        return 0;
    }
    *milli = (int)v;
    return 1;
}

int heatCoolKind(const char* type) {
    if (type == NULL) {
        return HEAT_ZONE_OTHER;
    }
    if (strncmp(type, "cpufreq-cpu", 11) == 0) {
        return HEAT_ZONE_CPU;
    }
    if (strncmp(type, "devfreq-", 8) == 0 && heatDevfreqIsGpu(type + 8)) {
        return HEAT_ZONE_GPU;
    }
    return HEAT_ZONE_OTHER;
}

int heatDevfreqIsGpu(const char* name) {
    size_t n = name != NULL ? strlen(name) : 0;
    return n > 4 && strcmp(name + n - 4, ".gpu") == 0;
}

// A file holding one whole number between min and max
static int parseWhole(const char* text, long long min, long long max, long long* out) {
    if (text == NULL) {
        return 0;
    }
    char* end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, 10);
    if (end == text || errno != 0) {
        return 0;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0' || v < min || v > max) {
        return 0;
    }
    *out = v;
    return 1;
}

int heatParseState(const char* text, int* state) {
    long long v;
    if (!parseWhole(text, 0, HEAT_STATE_MAX, &v)) {
        return 0;
    }
    *state = (int)v;
    return 1;
}

int heatParseFreqMhz(const char* text, int* mhz) {
    long long v;
    if (!parseWhole(text, HEAT_GPU_HZ_MIN, HEAT_GPU_HZ_MAX, &v)) {
        return 0;
    }
    *mhz = (int)((v + 500000) / 1000000);
    return 1;
}

float heatHottest(const HeatReading* r) {
    if (r == NULL || !r->valid) {
        return 0.0f;
    }
    if (r->haveCpu && r->haveGpu) {
        return r->cpuC > r->gpuC ? r->cpuC : r->gpuC;
    }
    return r->haveCpu ? r->cpuC : r->haveGpu ? r->gpuC : 0.0f;
}

// The first line of a small file, without its newline. 0 when it cannot be read.
static int readLine(const char* path, char* out, size_t cap) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    int ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) {
        return 0;
    }
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return 1;
}

// One cooling device (1.3.4): its cur_state opened when it is the CPU's or the GPU's
static void coolOpen(HeatSensor* s, const char* root, const char* name) {
    char path[700], type[64];
    snprintf(path, sizeof(path), "%s/%s/type", root, name);
    if (s->coolCount >= HEAT_COOL_MAX || !readLine(path, type, sizeof(type))) {
        return;
    }
    int kind = heatCoolKind(type);
    if (kind == HEAT_ZONE_OTHER) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%s/cur_state", root, name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    s->coolKind[s->coolCount] = kind;
    s->coolFd[s->coolCount] = fd;
    s->coolCount++;
    if (kind == HEAT_ZONE_CPU) {
        s->cpuCools++;
    }
    else {
        s->gpuCools++;
    }
}

// The GPU's clock (1.3.4), by its folder's name in the devfreq class
static void freqOpen(HeatSensor* s) {
    const char* env = getenv("MLXR_DEVFREQ_ROOT");
    const char* root = env != NULL && env[0] != '\0' ? env : "/sys/class/devfreq";
    DIR* d = opendir(root);
    if (d == NULL) {
        return;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL && s->freqCount == 0) {
        if (!heatDevfreqIsGpu(e->d_name)) {
            continue;
        }
        char path[900];
        snprintf(path, sizeof(path), "%s/%s/cur_freq", root, e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            s->freqFd = fd;
            s->freqCount = 1;
        }
    }
    closedir(d);
}

int heatSensorOpen(HeatSensor* s, const char* root) {
    memset(s, 0, sizeof(*s));
    freqOpen(s);
    if (root == NULL) {
        const char* env = getenv("MLXR_THERMAL_ROOT");
        root = env != NULL && env[0] != '\0' ? env : "/sys/class/thermal";
    }
    snprintf(s->root, sizeof(s->root), "%s", root);
    DIR* d = opendir(root);
    if (d == NULL) {
        return 0;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "cooling_device", 14) == 0) {
            coolOpen(s, root, e->d_name);
            continue;
        }
        if (strncmp(e->d_name, "thermal_zone", 12) != 0 || s->count >= HEAT_ZONES_MAX) {
            continue;
        }
        char path[700], type[64];
        snprintf(path, sizeof(path), "%s/%s/type", root, e->d_name);
        if (!readLine(path, type, sizeof(type))) {
            continue;
        }
        int kind = heatZoneKind(type);
        if (kind == HEAT_ZONE_OTHER) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s/temp", root, e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        s->kind[s->count] = kind;
        s->fd[s->count] = fd;
        s->count++;
        if (kind == HEAT_ZONE_CPU) {
            s->cpuZones++;
        }
        else {
            s->gpuZones++;
        }
    }
    closedir(d);
    return s->count;
}

// A sysfs attribute read again from its start is measured again
static int readAgain(int fd, char* buf, size_t cap) {
    ssize_t n = pread(fd, buf, cap - 1, 0);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    return 1;
}

int heatSensorRead(HeatSensor* s, HeatReading* r) {
    memset(r, 0, sizeof(*r));
    char buf[32];
    for (int i = 0; i < s->coolCount; i++) {
        int state;
        if (!readAgain(s->coolFd[i], buf, sizeof(buf)) || !heatParseState(buf, &state)) {
            continue;
        }
        if (s->coolKind[i] == HEAT_ZONE_CPU) {
            if (state > r->cpuSlow) r->cpuSlow = state;
            r->haveCpuSlow = 1;
        }
        else {
            if (state > r->gpuSlow) r->gpuSlow = state;
            r->haveGpuSlow = 1;
        }
    }
    r->haveGpuMhz = s->freqCount > 0 && readAgain(s->freqFd, buf, sizeof(buf)) && heatParseFreqMhz(buf, &r->gpuMhz);
    int cpuMax = 0, gpuMax = 0;
    for (int i = 0; i < s->count; i++) {
        int milli;
        if (!readAgain(s->fd[i], buf, sizeof(buf)) || !heatParseMilli(buf, &milli)) {
            continue;
        }
        if (s->kind[i] == HEAT_ZONE_CPU) {
            if (!r->haveCpu || milli > cpuMax) cpuMax = milli;
            r->haveCpu = 1;
        }
        else {
            if (!r->haveGpu || milli > gpuMax) gpuMax = milli;
            r->haveGpu = 1;
        }
    }
    r->cpuC = (float)cpuMax / 1000.0f;
    r->gpuC = (float)gpuMax / 1000.0f;
    r->valid = r->haveCpu || r->haveGpu;
    return r->valid;
}

void heatSensorClose(HeatSensor* s) {
    for (int i = 0; i < s->count; i++) {
        if (s->fd[i] >= 0) close(s->fd[i]);
    }
    for (int i = 0; i < s->coolCount; i++) {
        close(s->coolFd[i]);
    }
    if (s->freqCount > 0) {
        close(s->freqFd);
    }
    s->count = s->cpuZones = s->gpuZones = 0;
    s->coolCount = s->cpuCools = s->gpuCools = s->freqCount = 0;
}

// snprintf onto the end of buf
__attribute__((format(printf, 3, 4))) static void add(char* buf, size_t cap, const char* fmt, ...) {
    size_t n = strlen(buf);
    if (n + 1 >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
}

void heatText(const HeatReading* r, char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    if (r == NULL || !r->valid) {
        snprintf(buf, cap, "heat n/a");
    }
    else {
        char cpu[16] = "n/a", gpu[16] = "n/a";
        if (r->haveCpu) snprintf(cpu, sizeof(cpu), "%.0f C", (double)r->cpuC);
        if (r->haveGpu) snprintf(gpu, sizeof(gpu), "%.0f C", (double)r->gpuC);
        snprintf(buf, cap, "heat cpu %s, gpu %s", cpu, gpu);
    }
    if (r == NULL) {
        return;
    }
    if (r->haveGpuMhz) {
        add(buf, cap, ", gpu %d MHz", r->gpuMhz);
    }
    if (!r->haveCpuSlow && !r->haveGpuSlow) {
        return;
    }
    if (r->cpuSlow == 0 && r->gpuSlow == 0) {
        add(buf, cap, ", not slowed");
        return;
    }
    add(buf, cap, ", slowed");
    if (r->cpuSlow > 0) {
        add(buf, cap, " cpu %d", r->cpuSlow);
    }
    if (r->gpuSlow > 0) {
        add(buf, cap, "%s gpu %d", r->cpuSlow > 0 ? "," : "", r->gpuSlow);
    }
}

void heatStatsLine(const HeatReading* r, char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (r == NULL) {
        return;
    }
    int cpu = r->valid && r->haveCpu, gpu = r->valid && r->haveGpu;
    if (cpu || gpu || r->haveGpuMhz) {
        add(buf, cap, "Headset");
    }
    // U+00B0 DEGREE SIGN, which the bundled Noto Sans has
    if (cpu) {
        add(buf, cap, " CPU %.0f \xC2\xB0" "C", (double)r->cpuC);
    }
    if (gpu || r->haveGpuMhz) {
        add(buf, cap, "%s GPU", cpu ? "," : "");
    }
    if (gpu) {
        add(buf, cap, " %.0f \xC2\xB0" "C", (double)r->gpuC);
    }
    if (r->haveGpuMhz) {
        add(buf, cap, "%s %d MHz", gpu ? " at" : "", r->gpuMhz);
    }
    // On a line of its own: with it the line above would not fit the window
    if (r->cpuSlow > 0 || r->gpuSlow > 0) {
        add(buf, cap, "%s%s", buf[0] ? "\n" : "", r->cpuSlow > 0 && r->gpuSlow > 0 ? HEAT_TEXT_SLOWED_BOTH
                                                 : r->cpuSlow > 0                  ? HEAT_TEXT_SLOWED_CPU
                                                                                   : HEAT_TEXT_SLOWED_GPU);
    }
}

void heatLimitsDefault(HeatLimits* l) {
    l->warnC = HEAT_WARN_C;
    l->easeC = HEAT_EASE_C;
    l->stopC = HEAT_STOP_C;
    l->hystC = HEAT_HYST_C;
    l->restoreGapC = HEAT_RESTORE_GAP_C;
    l->holdNs = HEAT_HOLD_NS;
    l->act = 1;
}

int heatLimitsParse(HeatLimits* l, const char* text) {
    if (text == NULL) {
        return 0;
    }
    int w, e, s;
    char tail;
    if (sscanf(text, " %d , %d , %d %c", &w, &e, &s, &tail) != 3) {
        return 0;
    }
    if (w < 30 || s > 150 || w > e || e > s) {
        return 0;
    }
    l->warnC = w;
    l->easeC = e;
    l->stopC = s;
    return 1;
}

void heatLimitsFromEnv(HeatLimits* l) {
    heatLimitsDefault(l);
    heatLimitsParse(l, getenv("MLXR_HEAT_LIMITS"));
    const char* on = getenv("MLXR_HEAT_GUARD");
    if (on != NULL && on[0] != '\0' && atoi(on) == 0) {
        l->act = 0;
    }
}

void heatGuardStart(HeatGuard* g, const HeatLimits* lim) {
    memset(g, 0, sizeof(*g));
    if (lim != NULL) {
        g->lim = *lim;
    }
    else {
        heatLimitsDefault(&g->lim);
    }
}

static int clampRate(int perSecond) {
    return perSecond < DEPTH_RATE_MIN ? DEPTH_RATE_MIN : perSecond;
}

int heatGuardStep(HeatGuard* g, int64_t nowNs, const HeatReading* r, int stereoOn, int modelLive, int rate) {
    if (r == NULL || !r->valid || !g->lim.act) {
        return 0;
    }
    const HeatLimits* l = &g->lim;
    float hot = heatHottest(r);
    int acts = 0;
    // Gone back under the gap: the warning may be said again
    if (g->warned && hot < (float)(l->warnC - l->hystC)) {
        g->warned = 0;
    }
    if (stereoOn && hot >= (float)l->stopC) {
        acts |= HEAT_ACT_STOP;
        g->stops++;
        // The stop's own toast says it is hot
        g->warned = 1;
        g->warnedNs = nowNs;
    }
    else if (hot >= (float)l->warnC && (!g->warned || nowNs - g->warnedNs >= HEAT_REWARN_NS)) {
        // said once, and again every HEAT_REWARN_NS while it stays this hot
        acts |= HEAT_ACT_WARN;
        g->warned = 1;
        g->warnedNs = nowNs;
    }
    int held = g->depthCap == 0 || nowNs - g->movedNs >= l->holdNs;
    if (!(acts & HEAT_ACT_STOP) && modelLive && hot >= (float)l->easeC && held) {
        int cur = g->depthCap > 0 ? g->depthCap : rate;
        int next = clampRate(cur / 2);
        if (next < cur) {
            if (g->depthCap == 0) {
                g->capFrom = rate;
            }
            g->depthCap = next;
            g->movedNs = nowNs;
            acts |= HEAT_ACT_EASE;
        }
    }
    else if (g->depthCap > 0 && hot < (float)(l->easeC - l->restoreGapC) && held) {
        int next = g->depthCap * 2;
        g->depthCap = next >= g->capFrom ? 0 : next;
        g->movedNs = nowNs;
        acts |= HEAT_ACT_RESTORE;
    }
    return acts;
}

int heatDepthRate(int target, int cap) {
    return cap > 0 && cap < target ? cap : target;
}
