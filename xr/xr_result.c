// Moonlight XR Native: XrResult names and the checked call, from upstream's
// xr_session.c (Gilleece, GPLv3), on their own so the renderer core links
// without the session host.
#include "xr_renderer.h"

#include <openxr/openxr_reflection.h>

const char* xrResultName(XrResult res) {
    switch (res) {
#define XR_RESULT_NAME_CASE(name, value) case name: return #name;
        XR_LIST_ENUM_XrResult(XR_RESULT_NAME_CASE)
#undef XR_RESULT_NAME_CASE
        default:
            return "unknown XrResult";
    }
}

int checkXr(XrResult res, const char* what) {
    if (XR_FAILED(res)) {
        LOGE("%s failed: %d %s", what, res, xrResultName(res));
        return 0;
    }
    return 1;
}

