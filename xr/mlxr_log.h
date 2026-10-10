// Moonlight XR Native: minimal logging until xr_log.c is ported (rollover,
// verbosity). Lines go to stdout, which the launcher redirects to
// ~/Games/MoonlightXRNative/moonlight.log. GPLv3.
#pragma once
void mlxr_logf(char level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define MLOG_I(...) mlxr_logf('I', __VA_ARGS__)
#define MLOG_W(...) mlxr_logf('W', __VA_ARGS__)
#define MLOG_E(...) mlxr_logf('E', __VA_ARGS__)
