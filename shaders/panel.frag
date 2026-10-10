#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: panel, new in the native port (see panel.vert). The
// texture holds premultiplied, gamma encoded RGBA as upstream's art and glow
// swapchains do; the pipeline blends ONE, ONE_MINUS_SRC_ALPHA. Through an
// _SRGB view the blend happens on decoded values, where the compositor
// blending layers would also work; see REPORT.md. GPLv3.
//
// Set 0: 0 u_texture (bilinear, clamp). Spec constant 0: OUT_LINEAR.
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 shape;
    vec4 uvRect;
    vec4 color;
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 c = texture(u_texture, v_uv) * pc.color;
    if (OUT_LINEAR) {
        // Back out of premultiplied to decode, then premultiply again in
        // linear, so the blend sums light rather than encoded values
        vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
        c.rgb = srgbToLinear(clamp(straight, 0.0, 1.0)) * c.a;
    }
    fragColor = c;
}
