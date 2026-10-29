// What the SDL2 game controller source knows about a pad, kept apart from its thread so the
// pad probe (gamepad_probe.c) describes pads exactly as the app does. GPLv3.
#ifndef MLXR_GAMEPADS_SDL_H
#define MLXR_GAMEPADS_SDL_H

#include <SDL.h>

#include "gamepads.h"

// SDL's controller type, as the fork tells them apart, and SDL's own name for it
GpSdlKind gpSdlKind(SDL_GameControllerType type);
const char* gpSdlTypeName(SDL_GameControllerType type);

// The hints the app starts SDL with (no window, positional face buttons, PS4/PS5 rumble...).
// Before SDL_Init
void gpSdlApplyHints(void);

// Loads the game controller databases the app reads (MLXR_GAMECONTROLLERDB, the flat
// launcher's cached one, the client's own). found is called for each file that loaded, with
// the number of mappings it added. Returns the total
int gpSdlLoadMappings(void (*found)(void* user, const char* path, int count), void* user);

// Fills info from an open controller: name, GUID, serial, mapping, ids, buttons, capability
// bits, and the fork's classification (gpClassify)
void gpSdlDescribe(SDL_GameController* c, GpInfo* info);

// The pad's state as the host would get it (Moonlight's units, y up positive)
void gpSdlReadPacket(SDL_GameController* c, PadOutPacket* pk);

#endif
