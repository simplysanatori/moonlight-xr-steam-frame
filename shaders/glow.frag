#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: GLOW_FRAGMENT_SRC from xr_shaders.c, the ambilight
// glow quad. The quad is larger than the screen, so the middle of it covers
// the picture and only the border is ever seen; the clamp carries the edge
// texels of the 32x32 colour texture outward. GPLv3.
//
// Set 0: 0 u_texture (the glow edge output, or the raw colour sample),
// bilinear, clamp to edge (the spline relies on both).
// Output: the glow swapchain (GLOW_TEX 256 square), premultiplied alpha.
// Spec constant 0: OUT_LINEAR.
layout(push_constant) uniform PC {
    vec4 params;   // u_intensity, u_blur, u_feather (1 borderless, 0 the glow as upstream), u_scale
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

// 32.0 is AMBI_SAMPLE_TEX, kept in step by hand. The scale, how much larger
// than the screen the glow is (GLOW_SCALE, or GLOW_SCALE_WIDE for the Wide
// Glow edge outside a room), comes from the draw.
const float size = 32.0;

// A cubic B spline over the colour texture, done as four bilinear fetches
vec3 spline(vec2 fuv) {
    vec2 tc = fuv * size - 0.5;
    vec2 base = floor(tc);
    vec2 f = tc - base;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    vec2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    vec2 w3 = f3 / 6.0;
    vec2 g0 = w0 + w1;
    vec2 g1 = w2 + w3;
    vec2 h0 = (base - 0.5 + w1 / g0) / size;
    vec2 h1 = (base + 1.5 + w3 / g1) / size;
    vec3 c00 = texture(u_texture, vec2(h0.x, h0.y)).rgb;
    vec3 c10 = texture(u_texture, vec2(h1.x, h0.y)).rgb;
    vec3 c01 = texture(u_texture, vec2(h0.x, h1.y)).rgb;
    vec3 c11 = texture(u_texture, vec2(h1.x, h1.y)).rgb;
    return mix(mix(c11, c01, g0.x), mix(c10, c00, g0.x), g0.y);
}

// The average of nine spline evaluations one sample texel apart, as nine
// bilinear fetches
vec3 blurredSpline(vec2 fuv) {
    vec2 tc = fuv * size - 0.5;
    vec2 base = floor(tc);
    vec2 f = tc - base;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    vec2 b0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    vec2 b1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    vec2 b2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    vec2 b3 = f3 / 6.0;
    // Native (lane beam-streak): each fetch's offset is its pair's weights over their sum, written
    // without the thirds both sides of the ratio carried. They cancel, and the thirds divided by
    // thirds sent the optimiser of glslc 2026.1 (spirv-opt's folder) into a segfault at -O.
    vec2 s0 = 2.0 * b0 + b1;
    vec2 s1 = b0 + 2.0 * b1 + 2.0 * b2 + b3;
    vec2 s2 = b2 + 2.0 * b3;
    vec2 g0 = s0 / 3.0;
    vec2 g1 = s1 / 3.0;
    vec2 g2 = s2 / 3.0;
    vec2 h0 = (base - 1.5 + (b0 + b1) / s0) / size;
    vec2 h1 = (base + 0.5 + (b1 + b2 + b3) / s1) / size;
    vec2 h2 = (base + 2.5 + b3 / s2) / size;
    vec3 r0 = texture(u_texture, vec2(h0.x, h0.y)).rgb * g0.x
            + texture(u_texture, vec2(h1.x, h0.y)).rgb * g1.x
            + texture(u_texture, vec2(h2.x, h0.y)).rgb * g2.x;
    vec3 r1 = texture(u_texture, vec2(h0.x, h1.y)).rgb * g0.x
            + texture(u_texture, vec2(h1.x, h1.y)).rgb * g1.x
            + texture(u_texture, vec2(h2.x, h1.y)).rgb * g2.x;
    vec3 r2 = texture(u_texture, vec2(h0.x, h2.y)).rgb * g0.x
            + texture(u_texture, vec2(h1.x, h2.y)).rgb * g1.x
            + texture(u_texture, vec2(h2.x, h2.y)).rgb * g2.x;
    return r0 * g0.y + r1 * g1.y + r2 * g2.y;
}

void main() {
    float scale = pc.params.w > 1.0 ? pc.params.w : 1.7;
    vec2 uv = v_plain;
    vec2 fuv = (uv - 0.5) * scale + 0.5;
    // Distance out into the border, 0 at the screen edge and 1 at the rim
    vec2 d = max(abs(uv - 0.5) - 0.5 / scale, 0.0) / (0.5 - 0.5 / scale);
    float t = min(length(d), 1.0);
    float s = t * t * (3.0 - 2.0 * t);
    if (pc.params.z > 0.5) {
        // Native (lane borderless), the Glow edge's Soft and Wide, after Lobs1994's fork of
        // Moonlight XR. The picture's own border fades out (warp.frag), so this glow sits
        // under it and is drawn from the raw colour sample (bound here instead of the
        // lifted copy), at the picture's own brightness: where the picture thins out, the
        // same colour is underneath and there is no seam. Past the screen each point takes
        // the colour of the nearest point on the screen's edge, averaged along that edge
        // over a stretch that widens with the distance, so the glow softens as it goes and
        // its brightness falls to nothing at the rim. The glow level sets how far it
        // carries rather than how dark it is, and turns it off at the bottom.
        vec2 edge = clamp(fuv, 0.0, 1.0);
        vec2 past = fuv - edge;
        float dist = length(past);
        // Along the edge: straight up or down beside the screen, on the slant round a corner
        vec2 along = vec2(-past.y, past.x) / max(dist, 1e-5);
        float reachUv = min(0.03 + 0.4 * dist, 0.45);
        vec3 sum = vec3(0.0);
        float wsum = 0.0;
        for (int i = -4; i <= 4; i++) {
            float f = float(i) * 0.25;
            float w = exp(-2.2 * f * f);
            sum += w * texture(u_texture, clamp(edge + along * (f * reachUv), 0.0, 1.0)).rgb;
            wsum += w;
        }
        vec3 spread = mix(spline(fuv), sum / wsum, smoothstep(0.0, 0.1, dist));
        float level = clamp(pc.params.x, 0.0, 1.0);
        float carry = mix(2.2, 0.8, level);
        float af = pow(1.0 - s, carry) * min(1.0, level * 4.0);
        // Premultiplied, which is what the runtime composites the art as
        fragColor = encodeOut(vec4(spread * af, af));
        return;
    }
    vec3 color;
    if (pc.params.y > 0.5) {
        color = blurredSpline(fuv);
    } else {
        color = spline(fuv);
    }
    float fall = (1.0 - s) * (1.0 - s);
    float a = fall * pc.params.x;
    // Premultiplied, which is what the runtime composites the art as
    fragColor = encodeOut(vec4(color * a, a));
}
