// Moonlight XR Native (lane pcvr-steamvr): see vr_handoff.h. GPLv3.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   // POSIX_SPAWN_SETSID
#endif
#include "vr_handoff.h"

#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char** environ;

int vr_steam_cef_path(char* out, size_t cap) {
    if (cap == 0) return 0;
    out[0] = '\0';
    const char* env = getenv("MLXR_STEAM_CEF");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, cap, "%s", env);
    }
    else {
        char exe[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) return 0;
        exe[n] = '\0';
        char* slash = strrchr(exe, '/');
        if (slash == NULL) return 0;
        *slash = '\0';
        snprintf(out, cap, "%s/steam_cef.py", exe);
    }
    return access(out, R_OK) == 0;
}

int vr_handoff_argv(const char* cef, const char* pcName, const char** argv, int cap) {
    if (cap < 7) return 0;
    int n = 0;
    argv[n++] = "python3";
    argv[n++] = "-I";
    argv[n++] = cef;
    argv[n++] = "--pcvr";
    if (pcName != NULL && pcName[0] != '\0') {
        argv[n++] = "--pc";
        argv[n++] = pcName;
    }
    argv[n] = NULL;
    return n;
}

int vr_handoff_spawn(const char* cef, const char* pcName, const char* logPath) {
    const char* argv[8];
    vr_handoff_argv(cef, pcName, argv, 8);
    // the environment without LD_LIBRARY_PATH (the client's libraries are not python's)
    int count = 0;
    while (environ[count] != NULL) count++;
    char** env = calloc((size_t)count + 1, sizeof(char*));
    if (env == NULL) return -1;
    int m = 0;
    for (int i = 0; i < count; i++) {
        if (strncmp(environ[i], "LD_LIBRARY_PATH=", 16) != 0) env[m++] = environ[i];
    }
    env[m] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    const char* out = logPath != NULL && logPath[0] != '\0' ? logPath : "/dev/null";
    posix_spawn_file_actions_addopen(&fa, 1, out, O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
#ifdef POSIX_SPAWN_SETSID
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);   // its own session: it outlives this one
#endif
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "python3", &fa, &at, (char* const*)argv, env);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    free(env);
    return rc == 0 ? (int)pid : -1;
}
