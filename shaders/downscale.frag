#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: DOWNSCALE_FRAGMENT_SRC from xr_shaders.c, which feeds
// the depth model: a 4x4 box over each destination pixel. GPLv3.
//
// Set 0: 0..2 video. Output: RGBA8_UNORM at the depth map's size, read back
// for the model (top row first here; upstream's glReadPixels gave bottom
// row first).
layout(push_constant) uniform PC {
    vec4 vidRect;
    vec4 vidChroma;
    vec4 params;   // u_texel.xy (one destination texel), spare, spare
} pc;

#include "video.glsl"

layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 u_texel = pc.params.xy;
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            vec2 off = (vec2(float(x), float(y)) - 1.5) * (0.25 * u_texel);
            sum += video(v_plain + off);
        }
    }
    fragColor = vec4(sum * (1.0 / 16.0), 1.0);
}
