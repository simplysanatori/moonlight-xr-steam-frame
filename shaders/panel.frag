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
// picture.y > 0: sharpen at the eye's pixel scale by that much, an unsharp
// mask against the next coarser level, held inside the range of the pixel's
// own four reads so no edge rings past what the picture had there (see
// sharpened below); SteamVR's own resample of the layer softens it again.
// picture.z > 0: over high contrast the sharpened pixel reaches only that
// share of the way to the reads' darkest or brightest (lane polish-3:
// quality shimmered on white text on black); 0 reaches all the way.
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 shape;
    vec4 uvRect;
    vec4 color;
    vec4 picture;   // x filter (0 plain, 1 the picture's), y sharpen, z its reach over high contrast, w spare
} pc;

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

// The darkest and brightest luma of the reads the last footprint() made
float readLo, readHi;

const vec3 LUMA709 = vec3(0.2126, 0.7152, 0.0722);

float luma(vec4 c) {
    return dot(c.rgb, LUMA709);
}

// r: texels an eye pixel along the footprint's longer side
vec4 footprint(vec2 uv, vec2 dx, vec2 dy, float r) {
    if (r <= 1.0) {
        vec4 c = textureLod(u_texture, uv, 0.0);
        readLo = readHi = luma(c);
        return c;
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
    vec4 y = vec4(luma(t0), luma(t1), luma(t2), luma(t3));
    readLo = min(min(y.x, y.y), min(y.z, y.w));
    readHi = max(max(y.x, y.y), max(y.z, y.w));
    return 0.25 * (t0 + t1 + t2 + t3);
}

// Unsharp mask at the coarser of the eye's pixels and the picture's texels.
// Shrunk: against the level twice the footprint (one read), held inside the
// four reads' range. Magnified: against the four texels round it, held
// inside their range and its own. Measured on lavapipe (tests/shaders text):
// no overshoot on a slanted edge at either size.
// Native (lane polish-3): on luma alone, the one change added to all three
// channels, so the colour is the unsharpened picture's exactly (sharpening
// each channel on its own raised the saturation of ClearType's fringes, 0.45
// to 0.55 of the ideal's). With picture.z the clamp closes in as the reads'
// range nears black to white: a bright stroke's pixel pulled all the way up
// to its brightest read follows that read as the head moves, which is where
// quality shimmered on white text on black.
vec4 sharpened(vec4 c, vec2 uv, float r) {
    vec4 blur;
    float y = luma(c);
    if (r > 1.0) {
        blur = textureLod(u_texture, uv, log2(r) + 1.0);
    } else {
        vec2 t = 1.0 / vec2(textureSize(u_texture, 0));
        vec4 n0 = textureLod(u_texture, uv - vec2(t.x, 0.0), 0.0);
        vec4 n1 = textureLod(u_texture, uv + vec2(t.x, 0.0), 0.0);
        vec4 n2 = textureLod(u_texture, uv - vec2(0.0, t.y), 0.0);
        vec4 n3 = textureLod(u_texture, uv + vec2(0.0, t.y), 0.0);
        blur = 0.25 * (n0 + n1 + n2 + n3);
        vec4 n = vec4(luma(n0), luma(n1), luma(n2), luma(n3));
        readLo = min(y, min(min(n.x, n.y), min(n.z, n.w)));
        readHi = max(y, max(max(n.x, n.y), max(n.z, n.w)));
    }
    float reach = pc.picture.z > 0.0 ? mix(1.0, pc.picture.z, smoothstep(0.1, 0.6, readHi - readLo)) : 1.0;
    float d = clamp(pc.picture.y * (y - luma(blur)), reach * (readLo - y), reach * (readHi - y));
    // and no further than the brightest channel can rise or the darkest fall,
    // so no channel clips and the colour stays as it was
    d = clamp(d, -min(c.r, min(c.g, c.b)), 1.0 - max(c.r, max(c.g, c.b)));
    return vec4(c.rgb + d, c.a);
}

void main() {
    vec4 c;
    if (pc.picture.x > 0.5) {
        vec2 dx = dFdx(v_uv), dy = dFdy(v_uv);
        vec2 size = vec2(textureSize(u_texture, 0));
        float r = max(length(dx * size), length(dy * size));
        c = footprint(v_uv, dx, dy, r);
        if (pc.picture.y > 0.0) {
            c = sharpened(c, v_uv, r);
        }
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
