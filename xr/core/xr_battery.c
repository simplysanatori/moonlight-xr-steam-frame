// See xr_battery.h. Native (lane wave3-hud). GPLv3.
#include "xr_battery.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int mlxr_battery_parse(const char* capacityText, const char* statusText, MlxrBattery* b) {
    memset(b, 0, sizeof(*b));
    if (capacityText == NULL) {
        return 0;
    }
    while (*capacityText != '\0' && isspace((unsigned char)*capacityText)) {
        capacityText++;
    }
    char* end = NULL;
    long v = strtol(capacityText, &end, 10);
    if (end == capacityText || v < 0 || v > 100) {
        return 0;
    }
    b->present = 1;
    b->percent = (int)v;
    if (statusText != NULL) {
        while (*statusText != '\0' && isspace((unsigned char)*statusText)) {
            statusText++;
        }
        b->charging = strncasecmp(statusText, "Charging", 8) == 0;
        b->full = strncasecmp(statusText, "Full", 4) == 0;
    }
    return 1;
}

// The first line of a small file, without its newline. 0 when it cannot be read.
static int readLine(const char* dir, const char* name, char* out, size_t cap) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
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

int mlxr_battery_find(const char* root, char* out, size_t cap) {
    out[0] = '\0';
    const char* env = getenv("MLXR_BATTERY_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, cap, "%s", env);
        return 1;
    }
    if (root == NULL) {
        root = "/sys/class/power_supply";
    }
    DIR* d = opendir(root);
    if (d == NULL) {
        return 0;
    }
    char best[600] = "";
    int bestRank = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char dir[600], line[64];
        snprintf(dir, sizeof(dir), "%s/%s", root, e->d_name);
        if (!readLine(dir, "type", line, sizeof(line)) || strcasecmp(line, "Battery") != 0) {
            continue;
        }
        // A gamepad's or a mouse's cell is scope Device
        if (readLine(dir, "scope", line, sizeof(line)) && strcasecmp(line, "Device") == 0) {
            continue;
        }
        if (!readLine(dir, "capacity", line, sizeof(line))) {
            continue;
        }
        int rank = strncmp(e->d_name, "max1720x", 8) == 0 ? 2 : 1;
        if (rank > bestRank || (rank == bestRank && strcmp(dir, best) < 0)) {
            bestRank = rank;
            snprintf(best, sizeof(best), "%s", dir);
        }
    }
    closedir(d);
    if (bestRank == 0) {
        return 0;
    }
    snprintf(out, cap, "%s", best);
    return 1;
}

int mlxr_battery_read(const char* dir, MlxrBattery* b) {
    memset(b, 0, sizeof(*b));
    char cap[32], status[32] = "";
    if (dir == NULL || !readLine(dir, "capacity", cap, sizeof(cap))) {
        return 0;
    }
    readLine(dir, "status", status, sizeof(status));
    return mlxr_battery_parse(cap, status, b);
}

void mlxr_battery_text(const MlxrBattery* b, char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (b != NULL && b->present) {
        snprintf(buf, cap, "%d%%", b->percent);
    }
}
