// Moonlight XR Native (lane wave1-burst): config.ini's [stream] values the stream session
// (stream/session.c) reads when a stream starts. xrApplySettings (xr_prefs.c) sets them from the
// settings; until it has, the defaults hold. GPLv3.
#pragma once

#define MLXR_RFI_IDR_MS_DEFAULT 500 // ui/settings.c's default for [stream] rfi_idr_ms

/** [stream] rfi_idr_ms: at most one IDR frame request of the client's own per this many ms (0: no cap). */
void xrStreamPrefsSetRfiIdrMs(int ms);
int xrStreamPrefsRfiIdrMs(void);
