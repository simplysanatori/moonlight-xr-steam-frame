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
// Set 0: 3 u_depth (the upsample's output, depth in alpha; texelFetch only).
// Output: RGBA8_UNORM, r the left eye's offset and g the right's, each in
// texels over 2 * reach, biased by 0.5.
layout(push_constant) uniform PC {
    vec4 params;   // u_dispTexels, u_convergence, u_leftShare, u_rightShare
} pc;

layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

void main() {
    float u_dispTexels = pc.params.x;
    float u_convergence = pc.params.y;
    ivec2 sz = textureSize(u_depth, 0);
    // gl_FragCoord's origin is the top left here and the bottom left in GL;
    // both are memory row 0, the same row of u_depth
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    int reach = int(ceil(abs(u_dispTexels) * max(u_convergence, 1.0 - u_convergence))) + 2;
    vec2 result = vec2(0.0);
    for (int eye = 0; eye < 2; eye++) {
        // Each eye's share of the larger eye's shift: 1 and 1 when the eyes
        // are balanced, and so exactly as before
        float disp = (eye == 0) ? u_dispTexels * pc.params.z : -u_dispTexels * pc.params.w;
        float here = texelFetch(u_depth, ivec2(x, y), 0).a;
        float bestD = -1.0;
        float bestOff = -disp * (here - u_convergence);
        float pd = texelFetch(u_depth, ivec2(clamp(x - reach, 0, sz.x - 1), y), 0).a;
        float pe = float(-reach) + disp * (pd - u_convergence);
        for (int t = -reach + 1; t <= reach; t++) {
            float cd = texelFetch(u_depth, ivec2(clamp(x + t, 0, sz.x - 1), y), 0).a;
            float ce = float(t) + disp * (cd - u_convergence);
            float span = ce - pe;
            if (pe * ce <= 0.0 && abs(span) > 1e-6) {
                float f = clamp(-pe / span, 0.0, 1.0);
                float rd = pd + f * (cd - pd);
                if (rd > bestD) {
                    bestD = rd;
                    bestOff = float(t - 1) + f;
                }
            }
            pd = cd;
            pe = ce;
        }
        result[eye] = bestOff;
    }
    fragColor = vec4(result / (2.0 * float(reach)) + 0.5, 0.0, 1.0);
}
