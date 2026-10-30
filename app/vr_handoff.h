// Moonlight XR Native (lane pcvr-steamvr): the VR hand-off without the flat launcher. The app list's "VR"
// and the Host sheet's PC VR end the host's session (main.c) and then ask Steam, through
// packaging/steam_cef.py next to this program, to stream SteamVR from the PC to this headset; with the
// flat launcher (--exit-to-caller) the PC VR exit code asks it to instead. No network, no OpenXR. GPLv3.
#pragma once
#include <stddef.h>

// steam_cef.py: $MLXR_STEAM_CEF, else the directory of this program (/proc/self/exe). Writes the path to
// out and returns 1 when the file is there, 0 when not.
int vr_steam_cef_path(char* out, size_t cap);
// The command: python3 -I CEF --pcvr --pc PCNAME (no --pc for an empty name), NULL-terminated in argv
// (cap entries at least 7). Returns the number of arguments.
int vr_handoff_argv(const char* cef, const char* pcName, const char** argv, int cap);
// Starts the command and leaves it running after this process ends; its output goes to logPath (appended;
// NULL or unopenable: nowhere). LD_LIBRARY_PATH is not passed on. Returns its pid, or -1.
int vr_handoff_spawn(const char* cef, const char* pcName, const char* logPath);
