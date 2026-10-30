// Moonlight XR Native: the Host sheet on the input and layer side (PLAN 4b.1,
// 4b.2): where it hangs, what a point on it is, what a press does, the macros
// read from macros.ini, and the one key event a frame that plays a chosen
// sequence out through the keyboard's own path. The sheet's art is
// ui/panel_host.c, its cells and zones xr/core/xr_hostkeys.c. GPLv3.
#ifndef XR_HOST_H
#define XR_HOST_H

#include "xr_renderer.h"

// Reads macros.ini again (a missing file is no macros)
void xrHostLoadMacros(XrCtx* ctx);
// The sheet where it hangs now, and its size
XrPosef hostSheetPose(XrCtx* ctx, float* outWidth, float* outHeight);
// Opens the sheet (reading macros.ini first) or puts it away
void xrHostOpen(XrCtx* ctx);
void xrHostClose(XrCtx* ctx);
// What a point (u, v of the sheet) is on, an XR_HOST_ZONE_ or a cell
int xrHostZone(XrCtx* ctx, float u, float v);
// A press at (u, v). Returns the zone it landed on (XR_HOST_ZONE_NONE for
// none), having done what it asks: a cell plays its keys or asks first, Send
// keys puts the keyboard up in chord mode, Close puts the sheet away. 1 in
// *tick if the press is one to click for.
int xrHostPress(XrCtx* ctx, float u, float v, int* tick);
// Native (lane wave4-apollo, PC page): a key the keyboard typed, by its table code, while "Send text to
// PC" has it. Returns 1 if the field took it (so it does not go to the host), 0 when nothing is
// composing. Enter sends what was typed (hostActKind XR_HOSTACT_COPY) and puts the keyboard away.
int xrHostComposeFeed(XrCtx* ctx, int code);
// Once an input pass: lets the sequence playing put its next event in the key
// slot of out. live is 0 when nothing is being read, which lets go of it.
void xrHostFrame(XrCtx* ctx, float* out, int64_t now, int live);

#endif
