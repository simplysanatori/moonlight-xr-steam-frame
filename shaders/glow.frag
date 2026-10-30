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
    vec4 params;   // u_intensity, u_blur, u_feather, spare
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

// 1.7 is GLOW_SCALE and 32.0 is AMBI_SAMPLE_TEX, both kept in step by hand
const float scale = 1.7;
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
    vec2 wb = (b0 + b1) / 3.0;
    vec2 wd = (b1 + b2 + b3) / 3.0;
    vec2 wf = b3 / 3.0;
    vec2 g0 = (2.0 * b0 + b1) / 3.0;
    vec2 g1 = (b0 + 2.0 * b1 + 2.0 * b2 + b3) / 3.0;
    vec2 g2 = (b2 + 2.0 * b3) / 3.0;
    vec2 h0 = (base - 1.5 + wb / g0) / size;
    vec2 h1 = (base + 0.5 + wd / g1) / size;
    vec2 h2 = (base + 2.5 + wf / g2) / size;
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
    vec2 uv = v_plain;
    vec2 fuv = (uv - 0.5) * scale + 0.5;
    // Native (lane wave3-visual), the Picture tab's Glow edge. u_feather is the
    // depth of the fade into the picture as a fraction of its width and height,
    // 0 for off, which leaves everything below as it always was. On, the quad
    // is drawn over the picture instead of under it, and where it covers the
    // picture it is the glow's colour taken from the border texels, held
    // inward, at an alpha that is 1 on the picture's edge and eases to 0 a
    // feather's depth in (the picture's own edge fade in upstream pull request
    // 35, the same smoothstep, turned round: that fades the picture out over the
    // glow, this fades the glow in over the picture). fuv is 0..1 across the
    // picture, so the picture's edge is where it leaves that range.
    float feather = pc.params.z;
    vec2 depth = min(fuv, 1.0 - fuv);
    bool inside = feather > 0.0 && min(depth.x, depth.y) >= 0.0;
    vec2 sfuv = fuv;
    float ramp = 0.0;
    if (inside) {
        vec2 hold = 1.0 - smoothstep(vec2(0.0), vec2(feather), depth);
        vec2 edge = vec2(fuv.x < 0.5 ? 0.0 : 1.0, fuv.y < 0.5 ? 0.0 : 1.0);
        sfuv = mix(fuv, edge, hold);
        float ft = clamp(min(depth.x, depth.y) / feather, 0.0, 1.0);
        ramp = 1.0 - ft * ft * (3.0 - 2.0 * ft);
    }
    vec3 color;
    if (pc.params.y > 0.5) {
        color = blurredSpline(sfuv);
    } else {
        color = spline(sfuv);
    }
    if (inside) {
        // Premultiplied: at the edge the glow as it is just outside (colour at
        // the glow level), then less of it to nothing
        fragColor = encodeOut(vec4(color * (pc.params.x * ramp), ramp));
        return;
    }
    // Distance out into the border, 0 at the screen edge and 1 at the rim
    vec2 d = max(abs(uv - 0.5) - 0.5 / scale, 0.0) / (0.5 - 0.5 / scale);
    float t = min(length(d), 1.0);
    float s = t * t * (3.0 - 2.0 * t);
    float fall = (1.0 - s) * (1.0 - s);
    float a = fall * pc.params.x;
    // Premultiplied, which is what the runtime composites the art as
    fragColor = encodeOut(vec4(color * a, a));
}
