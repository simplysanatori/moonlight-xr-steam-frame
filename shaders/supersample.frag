#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: supersample, new in the native port (no upstream
// program). Upstream asked the compositor to filter a layer drawn smaller
// than its texture (XR_FB_composition_layer_settings, NORMAL or QUALITY
// SUPER_SAMPLING); SteamVR on the Frame does not offer that extension, so
// the app does it: each destination pixel averages taps x taps bilinear
// reads spread evenly over its footprint in the source, the box the depth
// downscale uses, generalised to any ratio. Taps 2 is "normal", 4 "quality".
// GPLv3.
//
// Set 0: 0 the source (an eye image, RGBA, bilinear clamp).
// Output: the swapchain. Spec constant 0: OUT_LINEAR. The source holds
// gamma encoded values (a _UNORM image written by the warp with OUT_LINEAR
// off), so the average is taken on encoded values, as a compositor filtering
// an _SRGB texture without decoding would; see REPORT.md.
layout(push_constant) uniform PC {
    vec4 srcRect;   // region of the source in uv: x0, y0, w, h (e.g. one eye)
    vec4 params;    // xy destination size in pixels, z taps per axis, w spare
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 srcTexel = 1.0 / vec2(textureSize(u_src, 0));
    vec2 base = pc.srcRect.xy + v_plain * pc.srcRect.zw;
    vec2 foot = pc.srcRect.zw / pc.params.xy;
    int taps = int(pc.params.z);
    // Held half a texel inside the region, so an eye never reads its
    // neighbour across the seam of a double wide image
    vec2 lo = pc.srcRect.xy + 0.5 * srcTexel;
    vec2 hi = pc.srcRect.xy + pc.srcRect.zw - 0.5 * srcTexel;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < taps; y++) {
        for (int x = 0; x < taps; x++) {
            vec2 off = ((vec2(float(x), float(y)) + 0.5) / float(taps) - 0.5) * foot;
            sum += texture(u_src, clamp(base + off, lo, hi));
        }
    }
    fragColor = encodeOut(sum / float(taps * taps));
}
