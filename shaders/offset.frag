#version 450
// Moonlight XR Native: OFFSET_FRAGMENT_SRC from xr_shaders.c, the offset
// search that inverts the warp once per video frame for both eyes at the
// upsample's resolution. GPLv3.
//
// A source pixel at offset t from this one lands here with error
//     e(t) = t + disp * (d(here + t) - convergence)
// so every zero crossing of e is a source that genuinely lands on this
// pixel. More than one crossing means two surfaces compete for this pixel,
// and the nearest one wins, which is what occlusion means.
//
// Native (lane wave5-depth, PLAN 5.2): disocclusion. Where the depth climbs
// across a silhouette the source texels on the climb land spread apart, each
// over e(t) - e(t - 1) texels of the eye's picture: a gap the shift opened,
// which the plain inversion fills by stretching the edge of the nearer
// surface across it, the "fur" on one side of a subject in one eye (#36,
// #46). A pixel whose surface comes from a segment spread wider than
// u_holeStretch is a hole: what was on the climb keeps its own size next to
// the nearer surface (the climb shifted as the nearer surface is), and the
// rest of the gap takes the background beyond the foot of the climb, a texel
// clear of it. 0 turns this off.
//
// Set 0: 3 u_depth (the upsample's output, depth in alpha; texelFetch only).
// Output: RGBA8_UNORM, r the left eye's offset and g the right's, each in
// texels over 2 * reach, biased by 0.5; b and a 1 where the left and the
// right eye's pixel is a hole (the warp then takes the offset as it is).
layout(push_constant) uniform PC {
    vec4 params;   // u_dispTexels, u_convergence, u_holeStretch, spare
    vec2 share;    // the left and the right eye's share of the larger eye's shift (3D Eye Balance)
} pc;

layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

ivec2 depthSize;
int row;
int col;
float conv;

float depthAt(int t) {
    return texelFetch(u_depth, ivec2(clamp(col + t, 0, depthSize.x - 1), row), 0).a;
}

// e(t): where the source t texels away lands, relative to this pixel
float landing(int t, float disp) {
    return float(t) + disp * (depthAt(t) - conv);
}

void main() {
    float u_dispTexels = pc.params.x;
    float u_convergence = pc.params.y;
    float u_holeStretch = pc.params.z;
    depthSize = textureSize(u_depth, 0);
    conv = u_convergence;
    // gl_FragCoord's origin is the top left here and the bottom left in GL;
    // both are memory row 0, the same row of u_depth
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    col = x;
    row = y;
    int reach = int(ceil(abs(u_dispTexels) * max(u_convergence, 1.0 - u_convergence))) + 2;
    vec2 result = vec2(0.0);
    vec2 holes = vec2(0.0);
    for (int eye = 0; eye < 2; eye++) {
        // Each eye's share of the larger eye's shift: 1 and 1 when the eyes
        // are balanced, and so exactly as before
        float disp = (eye == 0) ? u_dispTexels * pc.share.x : -u_dispTexels * pc.share.y;
        float here = depthAt(0);
        float bestD = -1.0;
        float bestOff = -disp * (here - u_convergence);
        float bestSpan = 1.0;
        int bestT = 0;
        float pd = depthAt(-reach);
        float pe = float(-reach) + disp * (pd - u_convergence);
        for (int t = -reach + 1; t <= reach; t++) {
            float cd = depthAt(t);
            float ce = float(t) + disp * (cd - u_convergence);
            float span = ce - pe;
            if (pe * ce <= 0.0 && abs(span) > 1e-6) {
                float f = clamp(-pe / span, 0.0, 1.0);
                float rd = pd + f * (cd - pd);
                if (rd > bestD) {
                    bestD = rd;
                    bestOff = float(t - 1) + f;
                    bestSpan = span;
                    bestT = t;
                }
            }
            pd = cd;
            pe = ce;
        }
        if (u_holeStretch > 0.0 && bestD >= 0.0 && bestSpan > u_holeStretch) {
            // Toward the background: the end of the segment with the farther
            // depth. Out that way to the foot of the climb, and the other way
            // to its top, while the segments stay spread.
            int dir = depthAt(bestT) < depthAt(bestT - 1) ? 1 : -1;
            float fdir = float(dir);
            int tFar = dir > 0 ? bestT : bestT - 1;
            int tNear = dir > 0 ? bestT - 1 : bestT;
            for (int k = 0; k < 2 * reach; k++) {
                if ((landing(tFar + dir, disp) - landing(tFar, disp)) * fdir <= u_holeStretch) {
                    break;
                }
                tFar += dir;
            }
            for (int k = 0; k < 2 * reach; k++) {
                if ((landing(tNear, disp) - landing(tNear - dir, disp)) * fdir <= u_holeStretch) {
                    break;
                }
                tNear -= dir;
            }
            // The climb shifted as its top is: where that puts this pixel's
            // source, if it is still on the climb, else the background
            float kept = -disp * (depthAt(tNear) - u_convergence);
            if ((kept - float(tNear)) * fdir >= 0.0 && (float(tFar) - kept) * fdir >= 0.0) {
                bestOff = kept;
            } else {
                bestOff = float(tFar + dir);
            }
            bestOff = clamp(bestOff, -float(reach), float(reach));
            holes[eye] = 1.0;
        }
        result[eye] = bestOff;
    }
    fragColor = vec4(result / (2.0 * float(reach)) + 0.5, holes);
}
