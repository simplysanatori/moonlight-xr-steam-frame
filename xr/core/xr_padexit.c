// See xr_padexit.h. GPLv3.
#include "xr_padexit.h"

void padExitReset(PadExit* e) { e->armed = 1; }

static int chordDown(const PadState* s) {
    return (s->buttons & PAD_START) && (s->buttons & PAD_BACK)
        && s->leftTrigger >= PADEXIT_TRIGGER_ON && s->rightTrigger >= PADEXIT_TRIGGER_ON;
}

static int allLetGo(const PadState* s) {
    return !(s->buttons & (PAD_START | PAD_BACK))
        && s->leftTrigger < PADEXIT_TRIGGER_OFF && s->rightTrigger < PADEXIT_TRIGGER_OFF;
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
