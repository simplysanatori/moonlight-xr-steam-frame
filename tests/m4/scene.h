// Moonlight XR Native, tests/m4: a headset-free XrCtx (CPU atlas, no OpenXR
// session) with helpers to put the UI in a given state, build the frame's
// layers through the ported code and compose them into a picture from one
// eye. GPLv3.
#pragma once
#include "xr_native.h"

typedef struct Scene {
    XrCtx* ctx;
    Settings settings;   // config.ini as the session would hold it
    Settings live;
    XrFrameOut out;
    // the launcher's view when one is up
    XrLauncherView launcher;
    UiPcItem pcs[6];
    UiAppItem apps[7];
    // M5 (tests/m5): images other than the atlas the layers name, looked up
    // first: a quad's swapchain (the glow, the picture) at u, v, and a
    // projection layer's pixel x, y of the picture being composed (the room
    // and the models, drawn from the same eye). Premultiplied; 0 falls back.
    void* hookUser;
    int (*quadTexel)(void* user, XrSwapchain sc, float u, float v, float out[4]);
    int (*projTexel)(void* user, const XrCompositionLayerProjection* layer, int x, int y, float out[4]);
    // the picture's own projection layer (curved, or flat in 3D), when a test has one
    const XrCompositionLayerProjection* curved;
} Scene;

// A context as the session builds it, with settings' defaults, a CPU atlas,
// the panels, and a 2560x1440 picture shown in the void
Scene* scene_new(void);
void scene_free(Scene* s);
// Back to the picture with nothing open
void scene_reset(Scene* s);
// The ray from the eye to a point in the space (draws the beam and the dot)
void scene_ray_to(Scene* s, Vec3 hand, Vec3 target);
// Draws the panels that changed and builds the layers
void scene_frame(Scene* s, const char* stats);
// Composes the layers into a w x h RGBA8 picture from an eye at the origin
// looking down -z with a horizontal field of view of hfovDeg; writes a PNG
// when path is not NULL. Returns the number of layers drawn.
int scene_render(Scene* s, int w, int h, float hfovDeg, const char* path);
// The fake video swapchain the picture's layer names
#define SCENE_VIDEO_SWAPCHAIN ((XrSwapchain)(uintptr_t)0x5151)
