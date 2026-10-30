// See xr_bugzip.h. Native (lane wave3-hud). GPLv3.
#define _GNU_SOURCE
#include "xr_bugzip.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

// The script takes a few seconds (it asks the system for the Wi-Fi names to mask and for the Vulkan
// driver); this is the most it is given
#define BUGZIP_TIMEOUT_MS 180000
#define BUGZIP_OUTPUT_MAX 65536

int xrBugzipParse(const char* output, char* path, size_t cap) {
    int found = 0;
    if (output == NULL || cap == 0) {
        return 0;
    }
    const char* p = output;
    while (*p != '\0') {
        const char* eol = strchr(p, '\n');
        size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);
        if (len > 6 && strncmp(p, "wrote ", 6) == 0) {
            size_t n = len - 6;
            while (n > 0 && (p[6 + n - 1] == '\r' || p[6 + n - 1] == ' ')) {
                n--;
            }
            if (n > 0 && n < cap) {
                memcpy(path, p + 6, n);
                path[n] = '\0';
                found = 1;
            }
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    return found;
}

static int readable(const char* path) {
    return access(path, R_OK) == 0;
}

int xrBugzipFindScript(char* out, size_t cap) {
    const char* env = getenv("MLXR_BUG_REPORT_SCRIPT");
    if (env != NULL && env[0] != '\0' && readable(env)) {
        snprintf(out, cap, "%s", env);
        return 1;
    }
    char exe[480];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char* slash = strrchr(exe, '/');
        if (slash != NULL) {
            *slash = '\0';
            static const char* const WHERE[] = { "%s/bug-report.sh", "%s/../tools/bug-report.sh",
                                                 "%s/../../tools/bug-report.sh" };
            for (size_t i = 0; i < sizeof(WHERE) / sizeof(WHERE[0]); i++) {
                char path[560];
                snprintf(path, sizeof(path), WHERE[i], exe);
                if (readable(path)) {
                    snprintf(out, cap, "%s", path);
                    return 1;
                }
            }
        }
    }
    const char* home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        char path[560];
        snprintf(path, sizeof(path), "%s/Games/MoonlightXRNative/app/bug-report.sh", home);
        if (readable(path)) {
            snprintf(out, cap, "%s", path);
            return 1;
        }
    }
    return 0;
}

static int64_t nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// The last line with something on it, for the log when the script said why it failed
static void lastLine(const char* text, char* out, size_t cap) {
    out[0] = '\0';
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) {
        len--;
    }
    size_t start = len;
    while (start > 0 && text[start - 1] != '\n') {
        start--;
    }
    size_t n = len - start;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, text + start, n);
    out[n] = '\0';
}

int xrBugzipRun(const char* script, const char* reportDir, char* result, size_t cap) {
    int fds[2];
    if (pipe(fds) != 0) {
        snprintf(result, cap, "no pipe for bug-report.sh: %s", strerror(errno));
        return -1;
    }
    // The environment the script runs in, with the report directory the caller means
    size_t count = 0;
    while (environ != NULL && environ[count] != NULL) {
        count++;
    }
    char** envp = calloc(count + 2, sizeof(char*));
    char* dirVar = NULL;
    if (envp == NULL) {
        close(fds[0]);
        close(fds[1]);
        snprintf(result, cap, "out of memory");
        return -1;
    }
    size_t m = 0;
    int haveDir = reportDir != NULL && reportDir[0] != '\0' && asprintf(&dirVar, "REPORT_DIR=%s", reportDir) > 0;
    for (size_t i = 0; i < count; i++) {
        if (haveDir && strncmp(environ[i], "REPORT_DIR=", 11) == 0) {
            continue;
        }
        envp[m++] = environ[i];
    }
    if (haveDir) {
        envp[m++] = dirVar;
    }
    envp[m] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(result, cap, "could not start bug-report.sh: %s", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        free(envp);
        free(dirVar);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        int in = open("/dev/null", O_RDONLY);
        if (in >= 0) {
            dup2(in, 0);
        }
        char* argv[] = { "bash", (char*)script, NULL };
        execve("/bin/bash", argv, envp);
        _exit(127);
    }
    close(fds[1]);
    free(envp);
    free(dirVar);

    char* output = malloc(BUGZIP_OUTPUT_MAX + 1);
    size_t have = 0;
    int timedOut = 0;
    if (output == NULL) {
        // nowhere to put what it says: stop it rather than let it block on a full pipe
        kill(pid, SIGKILL);
        output = calloc(1, 1);
    }
    else {
        int64_t deadline = nowMs() + BUGZIP_TIMEOUT_MS;
        for (;;) {
            int64_t left = deadline - nowMs();
            if (left <= 0) {
                timedOut = 1;
                break;
            }
            struct pollfd pfd = { fds[0], POLLIN, 0 };
            int pr = poll(&pfd, 1, left > 1000 ? 1000 : (int)left);
            if (pr < 0 && errno == EINTR) {
                continue;
            }
            if (pr <= 0) {
                continue;
            }
            char chunk[2048];
            ssize_t got = read(fds[0], chunk, sizeof(chunk));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            size_t room = BUGZIP_OUTPUT_MAX - have;
            size_t take = (size_t)got < room ? (size_t)got : room;
            memcpy(output + have, chunk, take);
            have += take;
        }
        output[have] = '\0';
    }
    close(fds[0]);
    if (timedOut) {
        kill(pid, SIGKILL);
    }
    int status = 0;
    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    int exited = w == pid && WIFEXITED(status);
    int code = exited ? WEXITSTATUS(status) : -1;

    int rc = -1;
    if (timedOut) {
        snprintf(result, cap, "bug-report.sh took over %d s and was stopped", BUGZIP_TIMEOUT_MS / 1000);
    }
    else if (exited && code != 0) {
        char last[200];
        lastLine(output, last, sizeof(last));
        snprintf(result, cap, "bug-report.sh failed (exit %d)%s%s", code, last[0] ? ": " : "", last);
    }
    else if (!xrBugzipParse(output, result, cap)) {
        char last[200];
        lastLine(output, last, sizeof(last));
        snprintf(result, cap, "bug-report.sh did not say where the zip is%s%s", last[0] ? ": " : "", last);
    }
    else {
        rc = 0;
    }
    free(output);
    return rc;
}

// ---- off the frame loop

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_state;   // 0 idle, 1 running, 2 finished and not yet polled
static int g_ok;
static char g_result[512];
static char g_dir[512];

static void* worker(void* arg) {
    (void)arg;
    char script[512], result[512];
    int rc;
    if (!xrBugzipFindScript(script, sizeof(script))) {
        snprintf(result, sizeof(result), "bug-report.sh was not found beside the program");
        rc = -1;
    }
    else {
        pthread_mutex_lock(&g_lock);
        char dir[512];
        snprintf(dir, sizeof(dir), "%s", g_dir);
        pthread_mutex_unlock(&g_lock);
        rc = xrBugzipRun(script, dir[0] != '\0' ? dir : NULL, result, sizeof(result));
    }
    pthread_mutex_lock(&g_lock);
    g_ok = rc == 0;
    snprintf(g_result, sizeof(g_result), "%s", result);
    pthread_mutex_unlock(&g_lock);
    atomic_store(&g_state, 2);
    return NULL;
}

int xrBugzipStart(const char* reportDir) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_state, &expected, 1)) {
        return 0;
    }
    pthread_mutex_lock(&g_lock);
    snprintf(g_dir, sizeof(g_dir), "%s", reportDir != NULL ? reportDir : "");
    pthread_mutex_unlock(&g_lock);
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        pthread_mutex_lock(&g_lock);
        g_ok = 0;
        snprintf(g_result, sizeof(g_result), "could not start a thread for bug-report.sh");
        pthread_mutex_unlock(&g_lock);
        atomic_store(&g_state, 2);
        return 1;
    }
    pthread_detach(t);
    return 1;
}

int xrBugzipPoll(int* ok, char* result, size_t cap) {
    if (atomic_load(&g_state) != 2) {
        return 0;
    }
    pthread_mutex_lock(&g_lock);
    *ok = g_ok;
    snprintf(result, cap, "%s", g_result);
    pthread_mutex_unlock(&g_lock);
    atomic_store(&g_state, 0);
    return 1;
}
