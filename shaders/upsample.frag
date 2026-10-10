#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: UPSAMPLE_FRAGMENT_SRC from xr_shaders.c, the joint
// bilateral upsample of the depth map guided by the colour. GPLv3.
//
// Each output pixel weights the 5x5 low resolution depth neighbourhood by
// how closely each neighbour's colour matches the colour here, so the depth
// edge snaps to the colour edge instead of straddling it. The guide rides in
// the rgb of the depth texture, so it is the frame the depth came from.
// Upstream runs it at a quarter of the video on each axis, every video frame.
//
// Set 0: 0..2 video, 3 u_depth (RGBA8: guide rgb, depth a; texelFetch only).
// Output: RGBA8_UNORM, the depth in every channel. No OUT_LINEAR: never a
// swapchain.
layout(push_constant) uniform PC {
    vec4 vidRect;
    vec4 vidChroma;
    vec4 params;   // u_sigmaR, u_sharp, u_depthSize.x, u_depthSize.y
} pc;

#include "video.glsl"

layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

const float SIGMA_S = 1.5;
const float FLAT = 0.05;

void main() {
    float u_sigmaR = pc.params.x;
    float u_sharp = pc.params.y;
    vec2 u_depthSize = pc.params.zw;
    vec3 hi = video(v_plain);
    vec2 lp = v_plain * u_depthSize - 0.5;
    ivec2 base = ivec2(floor(lp));
    float num = 0.0;
    float den = 0.0;
    float dlo = 1.0;
    float dhi = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            ivec2 q = clamp(base + ivec2(dx, dy), ivec2(0), ivec2(u_depthSize) - 1);
            vec4 s = texelFetch(u_depth, q, 0);
            vec2 off = vec2(q) - lp;
            float ws = exp(-dot(off, off) / (2.0 * SIGMA_S * SIGMA_S));
            vec3 cd = hi - s.rgb;
            float wr = exp(-dot(cd, cd) / (2.0 * u_sigmaR * u_sigmaR));
            float w = ws * wr;
            num += w * s.a;
            den += w;
            dlo = min(dlo, s.a);
            dhi = max(dhi, s.a);
        }
    }
    float d = num / max(den, 1e-6);
    // Pushes each texel to whichever side of the local range it is nearer,
    // turning a soft ramp across a silhouette back into a step. Flat
    // neighbourhoods are left alone.
    float span = dhi - dlo;
    if (u_sharp > 0.0 && span >= FLAT) {
        float u = clamp((d - dlo) / max(span, 1e-6), 0.0, 1.0);
        float snapped = dlo + span / (1.0 + exp(-24.0 * (u - 0.5)));
        d = mix(d, snapped, u_sharp);
    }
    fragColor = vec4(d);
}
