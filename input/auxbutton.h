// Native (lane a11y-2): the Steam Frame headset's side button (the "Aux" button, which SteamVR uses
// to open its dashboard) as a click button for the head and eye pointer. Linux sees it as KEY_SELECT
// on the gpio-keys input device. This finds that device by its name and its key capability (never
// by an event number), opens it read-only and non-blocking, and, while the app wants the button,
// takes it for itself with EVIOCGRAB so SteamVR does not see the presses. Closing the file (or the
// process ending, a crash included) gives it back: the kernel drops a grab with the file. Nothing
// here changes permissions or asks for more: a device the user cannot read is reported and left.
// GPLv3.
#ifndef MLXR_AUXBUTTON_H
#define MLXR_AUXBUTTON_H

#include <stddef.h>
#include <stdint.h>

// linux/input.h is left to the .c file and the tests: its KEY_MAX and the like would clash with
// the renderer's own names, which take this header through xr_renderer.h
struct input_event;

#define AUXBTN_DEVICE_NAME "gpio-keys"
#define AUXBTN_KEY 0x161   // KEY_SELECT

typedef struct {
    int found;          // a gpio-keys device with KEY_SELECT
    int readable;       // the user can open it for reading
    int err;            // errno of the failed open (EACCES: no permission), 0 when readable
    char path[64];      // /dev/input/eventN
    char sysName[64];   // eventN
} AuxDevInfo;

// Looks through <sysRoot>/class/input/event* (sysRoot "/sys") for the device; path is
// <devRoot>/eventN (devRoot "/dev/input"). Returns 1 when found (readable or not), 0 when not.
int auxDevFind(const char* sysRoot, const char* devRoot, AuxDevInfo* out);

// Whether a sysfs capabilities/key line ("1f0000 0 0 ..." hex words, the highest first) has the key
int auxDevKeyInCaps(const char* caps, int key);

typedef struct {
    int fd;            // -1 closed
    int grabbed;
    int (*grab)(int fd, int on);   // NULL: EVIOCGRAB; tests put a fake here. Returns 0 on success.
} AuxDev;

void auxDevInit(AuxDev* d);
// Opens the device read-only and non-blocking, and grabs it when grab is set. 0, or -errno.
int auxDevOpen(AuxDev* d, const char* path, int grab);
// Opens an fd someone else made (tests: a pipe's read end)
void auxDevAdopt(AuxDev* d, int fd, int grab);
// Lets the grab go and closes. Safe to call when closed.
void auxDevClose(AuxDev* d);

// The KEY_SELECT presses (1) and releases (0) in a run of events, in order, into edges (at most
// cap); repeats (value 2) and other keys are skipped. Returns how many.
int auxDevParse(const struct input_event* ev, int n, int* edges, int cap);
// Reads what is waiting without blocking and parses it. Returns how many edges, or -1 when the
// device went away (then closed).
int auxDevRead(AuxDev* d, int* edges, int cap);

#endif
