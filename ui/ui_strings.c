// See strings.h. GPLv3.
#include "ui_strings.h"

static const char* const TEXT[UIS_COUNT] = {
#define X(id, key, en) en,
    UI_STRINGS(X)
#undef X
};
static const char* const KEYS[UIS_COUNT] = {
#define X(id, key, en) key,
    UI_STRINGS(X)
#undef X
};

const char* ui_str(UiStr id) {
    return (unsigned)id < (unsigned)UIS_COUNT ? TEXT[id] : "";
}

const char* ui_str_key(UiStr id) {
    return (unsigned)id < (unsigned)UIS_COUNT ? KEYS[id] : "";
}
