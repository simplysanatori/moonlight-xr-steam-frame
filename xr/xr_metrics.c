// The compositor's timing from the runtime (XR_META_performance_metrics, which
// SteamVR on the Frame offers): its CPU and GPU frame times and the frames it
// dropped, for the stats, the perf CSV and the log, beside the app's own. The
// counters are enumerated once per session and every one the runtime lists is
// written to the log at the first reading, so the names and units it really
// uses are on record.
//
// Native (lane wave3-hud). PLAN 3.14. GPLv3.
#include "xr_renderer.h"
#include "xr_native.h"

#define METRICS_MAX 32
#define METRICS_PREFIX "/perfmetrics_meta/"

static struct {
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA enumerate;
    PFN_xrSetPerformanceMetricsStateMETA setState;
    PFN_xrQueryPerformanceMetricsCounterMETA query;
    int on;
    int count;
    int described;
    XrPath paths[METRICS_MAX];
    char names[METRICS_MAX][72];
} g;

void xrMetricsProbe(XrCtx* ctx) {
    memset(&g, 0, sizeof(g));
    if (!ctx->metricsSupported) {
        return;
    }
    xrGetInstanceProcAddr(ctx->instance, "xrEnumeratePerformanceMetricsCounterPathsMETA",
                          (PFN_xrVoidFunction*)&g.enumerate);
    xrGetInstanceProcAddr(ctx->instance, "xrSetPerformanceMetricsStateMETA", (PFN_xrVoidFunction*)&g.setState);
    xrGetInstanceProcAddr(ctx->instance, "xrQueryPerformanceMetricsCounterMETA", (PFN_xrVoidFunction*)&g.query);
    if (g.enumerate == NULL || g.setState == NULL || g.query == NULL) {
        LOGW("performance metrics offered but their entry points are missing");
        ctx->metricsSupported = 0;
    }
}

void xrMetricsStart(XrCtx* ctx) {
    g.on = 0;
    g.count = 0;
    g.described = 0;
    if (!ctx->metricsSupported || ctx->session == XR_NULL_HANDLE) {
        return;
    }
    uint32_t n = 0;
    if (XR_FAILED(g.enumerate(ctx->instance, 0, &n, NULL)) || n == 0) {
        LOGW("performance metrics: the runtime lists no counters");
        return;
    }
    if (n > METRICS_MAX) {
        n = METRICS_MAX;
    }
    uint32_t got = 0;
    if (XR_FAILED(g.enumerate(ctx->instance, n, &got, g.paths)) || got == 0) {
        LOGW("performance metrics: the counters could not be listed");
        return;
    }
    char line[900];
    size_t used = 0;
    line[0] = '\0';
    for (uint32_t i = 0; i < got; i++) {
        uint32_t len = 0;
        char path[sizeof(g.names[0])];
        path[0] = '\0';
        if (XR_FAILED(xrPathToString(ctx->instance, g.paths[i], sizeof(path), &len, path))) {
            continue;
        }
        const char* name = strncmp(path, METRICS_PREFIX, sizeof(METRICS_PREFIX) - 1) == 0
                               ? path + sizeof(METRICS_PREFIX) - 1 : path;
        snprintf(g.names[g.count], sizeof(g.names[0]), "%s", name);
        g.paths[g.count] = g.paths[i];
        if (used < sizeof(line)) {
            int w = snprintf(line + used, sizeof(line) - used, "%s%s", used ? ", " : "", g.names[g.count]);
            if (w > 0) used += (size_t)w;
        }
        g.count++;
    }
    XrPerformanceMetricsStateMETA st = { XR_TYPE_PERFORMANCE_METRICS_STATE_META };
    st.enabled = XR_TRUE;
    if (XR_FAILED(g.setState(ctx->session, &st))) {
        LOGW("performance metrics: the runtime would not switch them on");
        return;
    }
    g.on = g.count > 0;
    LOGEV("performance metrics on (XR_META_performance_metrics), %d counters: %s", g.count, line);
}

void xrMetricsStop(XrCtx* ctx) {
    if (g.on && ctx->session != XR_NULL_HANDLE) {
        XrPerformanceMetricsStateMETA st = { XR_TYPE_PERFORMANCE_METRICS_STATE_META };
        st.enabled = XR_FALSE;
        g.setState(ctx->session, &st);
    }
    g.on = 0;
}

int xrMetricsSample(XrCtx* ctx, MlxrCompositorMetrics* out) {
    memset(out, 0, sizeof(*out));
    if (!g.on || ctx->session == XR_NULL_HANDLE) {
        return 0;
    }
    char all[900];
    size_t used = 0;
    all[0] = '\0';
    for (int i = 0; i < g.count; i++) {
        XrPerformanceMetricsCounterMETA c = { XR_TYPE_PERFORMANCE_METRICS_COUNTER_META };
        if (XR_FAILED(g.query(ctx->session, g.paths[i], &c))) {
            continue;
        }
        float v;
        if (c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META) {
            v = c.floatValue;
        }
        else if (c.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META) {
            v = (float)c.uintValue;
        }
        else {
            continue;
        }
        mlxr_metric_set(out, g.names[i], v);
        if (!g.described && used < sizeof(all)) {
            int w = snprintf(all + used, sizeof(all) - used, "%s%s=%g (unit %d)", used ? ", " : "", g.names[i],
                             (double)v, (int)c.counterUnit);
            if (w > 0) used += (size_t)w;
        }
    }
    if (!g.described) {
        g.described = 1;
        LOGEV("performance metrics, first reading: %s", all[0] ? all : "none valid yet");
    }
    return out->valid;
}
