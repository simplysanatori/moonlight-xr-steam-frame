#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: panel, new in the native port (see panel.vert). The
// texture holds premultiplied, gamma encoded RGBA as upstream's art and glow
// swapchains do; the pipeline blends ONE, ONE_MINUS_SRC_ALPHA. Through an
// _SRGB view the blend happens on decoded values, where the compositor
// blending layers would also work; see docs/dev/reports/shaders.md. GPLv3.
//
// Set 0: 0 u_texture (trilinear, clamp). Spec constant 0: OUT_LINEAR.
//
// Native (lane text-clarity): the picture's own filter (picture.x 1), for the
// curved or 3D picture's image (a full mip chain) drawn into an eye. A plain
// trilinear read of a 2560 wide desktop shrunk 1.8 times into the eye (3 m
// wide at 3 m on the Frame's 3408 per eye) takes 84 % of its colour from the
// half size level, whose texels are already two desktop pixels each: small
// text came out well below what the eye's pixels can hold. Instead each
// eye pixel averages four reads spread over its own footprint (the
// supersample pass's 2 x 2 box), from the top level while a read covers less
// than two texels and from the level a read covers otherwise. Magnified (a
// texel larger than the eye's pixel) it is the one bilinear read of before.
// Measured (tests/shaders text, 3 m wide at 3 m): MTF50 0.22 -> 0.28 cycles
// a desktop pixel, 3 px line grids 0.22 -> 0.41, grey text 11.9 -> 3.0 RMS
// from the ideal.
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 shape;
    vec4 uvRect;
    vec4 color;
    vec4 picture;   // x filter (0 plain, 1 the picture's), yzw spare
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

// r: texels an eye pixel along the footprint's longer side
vec4 footprint(vec2 uv, vec2 dx, vec2 dy, float r) {
    if (r <= 1.0) {
        return textureLod(u_texture, uv, 0.0);
    }
    // Each read spans half the footprint each way. Just past 1:1 the reads
    // close in on the centre (together at 1, apart from 1.5 on), so the
    // filter does not turn softer than the single read of before there
    float lod = max(log2(r) - 1.0, 0.0);
    float spread = 0.25 * clamp((r - 1.0) * 2.0, 0.0, 1.0);
    vec2 a = spread * dx, b = spread * dy;
    vec4 t0 = textureLod(u_texture, uv - a - b, lod);
    vec4 t1 = textureLod(u_texture, uv + a - b, lod);
    vec4 t2 = textureLod(u_texture, uv - a + b, lod);
    vec4 t3 = textureLod(u_texture, uv + a + b, lod);
    return 0.25 * (t0 + t1 + t2 + t3);
}

void main() {
    vec4 c;
    if (pc.picture.x > 0.5) {
        vec2 dx = dFdx(v_uv), dy = dFdy(v_uv);
        vec2 size = vec2(textureSize(u_texture, 0));
        float r = max(length(dx * size), length(dy * size));
        c = footprint(v_uv, dx, dy, r);
    }
    else {
        c = texture(u_texture, v_uv);
    }
    c *= pc.color;
    if (OUT_LINEAR) {
        // Back out of premultiplied to decode, then premultiply again in
        // linear, so the blend sums light rather than encoded values
        vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
        c.rgb = srgbToLinear(clamp(straight, 0.0, 1.0)) * c.a;
    }
    fragColor = c;
}
