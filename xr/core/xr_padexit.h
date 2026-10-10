// The disconnect chord of gamepad mode: Start, Select (Back) and both triggers
// held together. Plain arithmetic over the settled pad state, edge triggered:
// it fires on the frame the chord completes and not again until everything
// has been let go. GPLv3.
#ifndef XR_PADEXIT_H
#define XR_PADEXIT_H

#include "xr_gamepad.h"

// A trigger counts as part of the chord past 80 percent, and as let go under
// 50 percent (a hysteresis so a wobbling trigger cannot re-arm it)
#define PADEXIT_TRIGGER_ON 204
#define PADEXIT_TRIGGER_OFF 128

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
