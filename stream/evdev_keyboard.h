// Moonlight XR Native: physical keyboards (USB or Bluetooth) read from evdev
// and forwarded to the host. The app has no window under SteamVR, so it reads
// /dev/input/event* directly (the steamos user is in the input group) and
// grabs the keyboard while it is forwarding, so keys reach only the host. GPLv3.
#pragma once

// vk: Windows virtual-key code; modifiers: MODIFIER_* from Limelight.h.
typedef void (*EvKbdSend)(short vk, int down, char modifiers, void* user);

int evkbd_start(EvKbdSend send, void* user);
// Forward (and grab) only while active; going inactive releases held keys.
void evkbd_set_active(int active);
void evkbd_stop(void);
// Moonlight's keyboard disconnect combo, Ctrl+Alt+Shift+Q (moonlight-qt's
// KeyComboQuit; either side's modifiers): while forwarding, the Q never
// reaches the host, every held key is let go there, then fn(user) is called
// on the keyboard thread
void evkbd_set_disconnect_hook(void (*fn)(void*), void* user);

// Linux KEY_* code to Windows VK, 0 when unmapped (exposed for tests).
int evkbd_vk(int linuxKey, int* extended);
