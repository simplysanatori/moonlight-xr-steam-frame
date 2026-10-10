// Moonlight XR Native: the real depth engine (depth/depth_engine.h, lane D)
// behind xr_depth.c's table. Its own library (mlxr_xrdepth), built only when
// LiteRT is, so only what runs the model links it. GPLv3.
#include "xr_depth.h"

static const XrDepthEngineOps REAL_OPS = {
    mlxr_depth_model_spec, mlxr_depth_start, mlxr_depth_stop, mlxr_depth_state, mlxr_depth_wait_ready,
    mlxr_depth_size, mlxr_depth_set_rate, mlxr_depth_set_params, mlxr_depth_reset, mlxr_depth_due,
    mlxr_depth_submit, mlxr_depth_acquire, mlxr_depth_release, mlxr_depth_stats,
};

const XrDepthEngineOps* xrDepthRealOps(void) {
    return &REAL_OPS;
}
