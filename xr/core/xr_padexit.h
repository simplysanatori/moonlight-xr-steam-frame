// The disconnect chord of gamepad mode, Moonlight's: Start, Select (Back) and
// both bumpers (L1, R1) held together, and no other button. Plain arithmetic
// over the settled pad state, edge triggered:
// it fires on the frame the chord completes and not again until everything
// has been let go. GPLv3.
#ifndef XR_PADEXIT_H
#define XR_PADEXIT_H

#include "xr_gamepad.h"

#define PADEXIT_BUTTONS (PAD_START | PAD_BACK | PAD_LB | PAD_RB)
// The pad's buttons as the host gets them (the bits above are held flags)
#define PADEXIT_BUTTON_BITS 0xFFFF

typedef struct {
    // 0 until the chord has been let go after it fired
    int armed;
} PadExit;

void padExitReset(PadExit* e);
// Feed every frame's settled pad state. Returns 1 on the frame the chord
// completes (and only then).
int padExitUpdate(PadExit* e, const PadState* s);
// While 1, the pad must be reported at rest: the chord fired and its buttons
// are still down
int padExitLatched(const PadExit* e);

#endif
