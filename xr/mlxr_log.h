// Moonlight XR Native: minimal logging until xr_log.c is ported (rollover,
// verbosity). Lines go to stdout, which the launcher redirects to
// ~/Games/MoonlightXRNative/moonlight.log. GPLv3.
#pragma once
void mlxr_log(char level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define MLOG_I(...) mlxr_log('I', __VA_ARGS__)
#define MLOG_W(...) mlxr_log('W', __VA_ARGS__)
#define MLOG_E(...) mlxr_log('E', __VA_ARGS__)
