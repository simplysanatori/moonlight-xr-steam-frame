/* Minimal test macros. GPLv3. */
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_EQ_INT(a, b) do { long long _a = (a), _b = (b); if (_a != _b) { fprintf(stderr, "%s:%d: %s == %s failed: %lld vs %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); g_failures++; } } while (0)
#define CHECK_EQ_STR(a, b) do { const char *_a = (a), *_b = (b); if (!_a || !_b || strcmp(_a, _b)) { fprintf(stderr, "%s:%d: %s == %s failed: \"%s\" vs \"%s\"\n", __FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); g_failures++; } } while (0)
#define CHECK_OK(expr) do { mlxr_error _e = {0}; (void)_e; mlxr_status _s = (expr); if (_s != MLXR_OK) { fprintf(stderr, "%s:%d: %s -> %s\n", __FILE__, __LINE__, #expr, mlxr_status_name(_s)); g_failures++; } } while (0)
#define TEST_DONE() do { if (g_failures) { fprintf(stderr, "%d check(s) failed\n", g_failures); return 1; } printf("ok\n"); return 0; } while (0)

#include <sys/stat.h>
#include <unistd.h>
/* mkdtemp under $TMPDIR (default /tmp); returns static buffer */
static inline const char *make_tmpdir(void)
{
    static char t[512];
    const char *base = getenv("TMPDIR");
    snprintf(t, sizeof t, "%s/mlxr-test-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(t)) { perror("mkdtemp"); exit(2); }
    return t;
}

static inline char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(1 << 20);
    size_t n = fread(b, 1, (1 << 20) - 1, f);
    b[n] = 0;
    fclose(f);
    return b;
}
#endif
