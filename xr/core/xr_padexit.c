// See xr_padexit.h. GPLv3.
#include "xr_padexit.h"

void padExitReset(PadExit* e) { e->armed = 1; }

// Moonlight Qt's own test (gamepad.cpp): exactly these four buttons down
static int chordDown(const PadState* s) {
    return (s->buttons & PADEXIT_BUTTON_BITS) == PADEXIT_BUTTONS;
}

static int allLetGo(const PadState* s) {
    return !(s->buttons & PADEXIT_BUTTONS);
}

int padExitUpdate(PadExit* e, const PadState* s) {
    if (!e->armed) {
        if (allLetGo(s)) e->armed = 1;
        return 0;
    }
    if (chordDown(s)) {
        e->armed = 0;
        return 1;
    }
    return 0;
}

int padExitLatched(const PadExit* e) { return !e->armed; }
