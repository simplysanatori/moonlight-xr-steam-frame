#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: FRAGMENT_SRC from xr_shaders.c, the "XR warp", which
// is also the colour pass: video sampling, picture grade, tint and, in 3D,
// the occlusion aware gather warp. GPLv3.
//
// Gather warp. Each output pixel samples the color frame shifted by a
// disparity derived from the depth map. u_disparity is signed per eye and
// zero in mono, which makes this exactly the old passthrough. The transform
// (here the video crop rectangle) is applied after the shift since the
// shift is defined in frame space.
//
// Set 0 (push descriptors):
//   0..2  video (see video.glsl)
//   3     u_depth    RGBA8, depth in alpha: the raw map or the upsample
//   4     u_offsets  RGBA8, the offset search's result (only read with
//                    occlusion on)
// Spec constant 0: OUT_LINEAR (common.glsl).
//
// Added for the native port, off (0) reproduces upstream exactly:
//   extra.x  sharpen strength 0..1 (contrast adaptive, on the video taps
//            before the grade). Upstream asked the compositor for this
//            through XR_FB_composition_layer_settings, which SteamVR on the
//            Frame does not offer.
layout(push_constant) uniform PC {
    vec4 vidRect;
    vec4 vidChroma;
    vec4 grade;    // u_grade
    vec4 tintOn;   // rgb u_tint, a u_gradeOn
    vec4 dispA;    // u_disparity, u_eyeIndex, u_convergence, u_dispTexels
    vec4 dispB;    // u_lowResWidth, u_frameWidth, u_srcInset, u_edgeFade
    vec4 modes;    // u_occlusion, u_depthCubic, u_showDepth, u_barTest
    vec4 extra;    // x sharpen, yzw spare
} pc;

#include "common.glsl"
#include "grade.glsl"
#include "video.glsl"

layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(set = 0, binding = 4) uniform sampler2D u_offsets;

layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

#define u_disparity pc.dispA.x
#define u_eyeIndex pc.dispA.y
#define u_convergence pc.dispA.z
#define u_dispTexels pc.dispA.w
#define u_lowResWidth pc.dispB.x
#define u_frameWidth pc.dispB.y
#define u_srcInset pc.dispB.z
#define u_edgeFade pc.dispB.w
#define u_occlusion pc.modes.x
#define u_depthCubic pc.modes.y
#define u_showDepth pc.modes.z
#define u_barTest pc.modes.w

// The depth here is a quarter of the frame wide, so a texel of it spans
// four pixels, and a bilinear read joins texels with straight segments.
// A cubic B spline is smooth across texel boundaries, done as four bilinear
// fetches. Every read of the depth in a pixel is on that pixel's row, so
// the vertical half of the spline is worked out once in main, into
// depthRow: the two fetch rows in uv and the share of the first.
vec2 depthSize;
float depthTexelX;
vec3 depthRow;

vec3 splineAxis(float t) {
    float base = floor(t);
    float f = t - base;
    float f2 = f * f;
    float g0 = 5.0 + f * (-3.0 + f * (-3.0 + 2.0 * f));
    float w1 = 4.0 + f2 * (-6.0 + 3.0 * f);
    return vec3(base - 0.5 + w1 / g0, base + 1.5 + f2 * f / (6.0 - g0), g0 / 6.0);
}

float depthAt(float x) {
    if (u_depthCubic < 0.5) {
        return texture(u_depth, vec2(x, v_plain.y)).a;
    }
    vec3 sx = splineAxis(x * depthSize.x - 0.5);
    vec2 hx = sx.xy * depthTexelX;
    float a00 = texture(u_depth, vec2(hx.x, depthRow.x)).a;
    float a10 = texture(u_depth, vec2(hx.y, depthRow.x)).a;
    float a01 = texture(u_depth, vec2(hx.x, depthRow.y)).a;
    float a11 = texture(u_depth, vec2(hx.y, depthRow.y)).a;
    return mix(mix(a11, a01, sx.z), mix(a10, a00, sx.z), depthRow.z);
}

// Contrast adaptive sharpening (the CAS shape: the weight shrinks where the
// neighbourhood is already near black or white, so it cannot ring past
// either), on the four video texels around the sample. Native port only.
vec3 sharpened(vec2 tc, vec3 c) {
    vec2 t = pc.vidChroma.zw;
    vec3 n = clamp(videoAt(tc - vec2(0.0, t.y)), 0.0, 1.0);
    vec3 s = clamp(videoAt(tc + vec2(0.0, t.y)), 0.0, 1.0);
    vec3 w = clamp(videoAt(tc - vec2(t.x, 0.0)), 0.0, 1.0);
    vec3 e = clamp(videoAt(tc + vec2(t.x, 0.0)), 0.0, 1.0);
    c = clamp(c, 0.0, 1.0);
    vec3 mn = min(c, min(min(n, s), min(w, e)));
    vec3 mx = max(c, max(max(n, s), max(w, e)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-5), 0.0, 1.0));
    vec3 wt = -amp * mix(0.125, 0.2, clamp(pc.extra.x, 0.0, 1.0));
    return clamp((c + wt * (n + s + w + e)) / (1.0 + 4.0 * wt), 0.0, 1.0);
}

void main() {
    if (u_depthCubic > 0.5) {
        depthSize = vec2(textureSize(u_depth, 0));
        depthTexelX = 1.0 / depthSize.x;
        vec3 sy = splineAxis(v_plain.y * depthSize.y - 0.5);
        depthRow = vec3(sy.xy / depthSize.y, sy.z);
    }
    if (u_showDepth > 0.5) {
        float dd = depthAt(v_plain.x);
        fragColor = encodeOut(vec4(dd, dd, dd, 1.0));
        return;
    }
    vec2 tc = v_plain;
    // The shift dies away over the last few pixels at the side edges, so
    // nothing asks to leave the frame and the edge column is not smeared
    float edgeW = smoothstep(0.0, max(u_edgeFade, 1e-6), min(v_plain.x, 1.0 - v_plain.x));
    float disp = edgeW * u_disparity;
    if (u_occlusion > 0.5) {
        // The offset map already picked the right surface; two Newton steps
        // against the full resolution depth settle the position on it
        int reach = int(ceil(abs(u_dispTexels) * max(u_convergence, 1.0 - u_convergence))) + 2;
        vec2 enc = texture(u_offsets, v_plain).rg;
        float off = (u_eyeIndex < 0.5 ? enc.r : enc.g) - 0.5;
        tc.x = v_plain.x + edgeW * off * 2.0 * float(reach) / u_lowResWidth;
        float h = 1.0 / u_frameWidth;
        for (int i = 0; i < 2; i++) {
            float d0 = depthAt(tc.x);
            float dm = texture(u_depth, vec2(tc.x - h, v_plain.y)).a;
            float dp = texture(u_depth, vec2(tc.x + h, v_plain.y)).a;
            float e = (tc.x - v_plain.x) + disp * (d0 - u_convergence);
            float slope = 1.0 + disp * (dp - dm) / (2.0 * h);
            if (abs(slope) < 0.25) {
                slope = 0.25;
            }
            tc.x -= clamp(e / slope, -4.0 * h, 4.0 * h);
        }
    } else {
        tc.x -= disp * (depthAt(v_plain.x) - u_convergence);
    }
    if (u_barTest > 0.5) {
        float b = 1.0 - step(0.004, abs(tc.x - 0.5));
        fragColor = encodeOut(vec4(b, b, b, 1.0));
        return;
    }
    // Held half a texel inside, so the bilinear tap never reaches past the
    // picture's edge whatever the shift did
    tc.x = clamp(tc.x, u_srcInset, 1.0 - u_srcInset);
    vec2 vtc = videoTc(tc);
    fragColor = vec4(videoAt(vtc), 1.0);
    if (pc.extra.x > 0.0) {
        fragColor.rgb = sharpened(vtc, fragColor.rgb);
    }
    // On the colour alone, after the shift
    if (pc.tintOn.a > 0.5) {
        fragColor.rgb = grade(fragColor.rgb, pc.grade);
    }
    fragColor.rgb *= pc.tintOn.rgb;
    fragColor = encodeOut(fragColor);
}
