#version 450
// Moonlight XR Native: GLOW_EDGE_FRAGMENT_SRC from xr_shaders.c, the glow's
// copy of the frame colour sample: every texel lifted to a steady luma, and
// the ring of edge texels rolled off where the picture goes dark. The same
// sums as xr/core/xr_glow.c with its constants handed in. GPLv3.
//
// Set 0: 0 u_texture, the 32x32 colour sample (texelFetch only).
// Output: RGBA8_UNORM 32x32.
layout(push_constant) uniform PC {
    vec4 luma;     // u_luma: target, floor, top of the knee; w spare
    vec4 litReach; // u_lit.xy, u_reach, spare
    ivec4 steps;   // x u_steps
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) out vec4 fragColor;

// 32 is AMBI_SAMPLE_TEX. The ring runs round it from texel (0, 0), 31
// texels a side (anticlockwise from the bottom left in GL's picture, from
// the top left here: the same memory order).
const int N = 32;
const int RING = 4 * (N - 1);

vec3 lifted(ivec2 p) {
    vec3 c = texelFetch(u_texture, p, 0).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float s = smoothstep(pc.luma.y, pc.luma.z, l);
    return clamp(c * (mix(l, pc.luma.x, s) / max(l, 1e-5)), 0.0, 1.0);
}

float lit(vec3 c) {
    return smoothstep(pc.litReach.x, pc.litReach.y, max(c.r, max(c.g, c.b)));
}

ivec2 ringTexel(int i) {
    if (i < N - 1) return ivec2(i, 0);
    if (i < 2 * (N - 1)) return ivec2(N - 1, i - (N - 1));
    if (i < 3 * (N - 1)) return ivec2(3 * (N - 1) - i, N - 1);
    return ivec2(0, RING - i);
}

int ringIndex(ivec2 p) {
    if (p.y == 0) return p.x;
    if (p.x == N - 1) return (N - 1) + p.y;
    if (p.y == N - 1) return 3 * (N - 1) - p.x;
    return RING - p.y;
}

// The texel's colour, or where it is dark the first lit one further in
vec3 edgeColour(int i) {
    ivec2 p = ringTexel(i);
    ivec2 inward = ivec2(p.x == 0 ? 1 : (p.x == N - 1 ? -1 : 0), p.y == 0 ? 1 : (p.y == N - 1 ? -1 : 0));
    vec3 c = lifted(p);
    for (int s = 1; s <= pc.steps.x; s++) {
        vec3 deeper = lifted(p + inward * s);
        c = mix(deeper, c, lit(c));
    }
    return c;
}

void main() {
    float u_reach = pc.litReach.z;
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (p.x > 0 && p.y > 0 && p.x < N - 1 && p.y < N - 1) {
        fragColor = vec4(lifted(p), 1.0);
        return;
    }
    int j = ringIndex(p);
    int r = min(int(u_reach), RING / 2 - 1);
    vec3 own = vec3(0.0);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    float carried = 0.0;
    for (int k = -r; k <= r; k++) {
        vec3 c = edgeColour((j + k + RING) % RING);
        float t = float(k) / u_reach;
        float w = (1.0 - t * t) * lit(c);
        sum += c * w;
        weight += w;
        carried = max(carried, w);
        if (k == 0) {
            own = c;
        }
    }
    vec3 nearby = sum / max(weight, 1e-4);
    vec3 c = own + nearby * (carried * (1.0 - lit(own)));
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
