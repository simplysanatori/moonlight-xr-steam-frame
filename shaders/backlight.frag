#version 450
// Moonlight XR Native (issue #7): the glow image of the Glow edge's Backlight, as
// a backlit television lights the wall behind it: the light of the nearest point
// of the picture's edge, mixing with its neighbours' further out, fading out over
// a reach. GPLv3.
//
// Set 0: 0 the spread zones (glow_spread.frag), bilinear, clamped.
// Output: the glow image (GLOW_TEX square, _UNORM), gamma encoded, in the two
// ways the glow goes up: a quad layer's bytes are decoded as they stand, the
// panel pass (panel.frag) divides by alpha before it decodes; either way the eye
// gets the same linear light. Alpha is the light's own brightness, so what is
// behind (a void's colour, a room, the cameras) is lit and not darkened. Over a
// black void, which nothing can darken, the panel pass gets it opaque: its
// division by alpha then loses nothing, where dim light in eight bits a channel
// left rings at the rim.
//
// The light is FramePlayerX's glow_round and glow_line (https://github.com/remotesojourner/frameplayerx,
// crates/fp-render/shaders/scene.wgsl), Copyright 2026 remotesojourner, Apache-2.0, itself a modified version of
// FramePlayer, Copyright (c) 2026 yellkell. Changed: drawn into the glow image, not per eye pixel, so it bends with a
// curved screen; one bilinear read of the spread zones; zones that follow the picture's shape; the bezel; premultiplied
// output over any background.
layout(push_constant) uniform PC {
    vec4 size;   // the glow image's size in screen widths and heights (x, y), the screen's height over its width, zones across
    vec4 light;  // reach and bezel in screen widths, level, 1 where the panel pass takes it (2: over black)
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

const float ZONES = 64.0;   // GLOW_ZONES

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(1e-8)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    // In screen widths, from the screen's middle
    vec2 half_ = vec2(0.5, 0.5 * pc.size.z);
    vec2 p = (v_plain - 0.5) * pc.size.xy * 2.0 * half_;
    vec2 d = abs(p) - half_;
    // Out past the screen's edge, negative inside
    float off = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    // How far round the edge its nearest point is, in zones, clockwise from the top left
    float a = pc.size.w, b = 0.5 * ZONES - a;
    vec2 e = clamp(p / (2.0 * half_) + 0.5, 0.0, 1.0);
    float pos;
    if (d.x > d.y) {
        pos = p.x > 0.0 ? a + e.y * b : 2.0 * a + b + (1.0 - e.y) * b;
    } else {
        pos = p.y < 0.0 ? e.x * a : a + b + (1.0 - e.x) * a;
    }
    float t = clamp((off - pc.light.y) / max(pc.light.x, 1e-4), 0.0, 1.0);
    vec3 light = texture(u_texture, vec2((pos + 1.0) / (ZONES + 2.0), (2.0 * t + 0.5) / 3.0)).rgb;
    light = clamp(light * (pc.light.z * (1.0 - t * t * (3.0 - 2.0 * t))), 0.0, 1.0);
    float most = max(light.r, max(light.g, light.b));
    float al = most > 0.0 ? (pc.light.w > 1.5 ? 1.0 : linearToSrgb(vec3(most)).r) : 0.0;
    vec3 c = pc.light.w > 0.5 ? linearToSrgb(light / max(al, 1e-4)) * al : linearToSrgb(light);
    vec4 o = vec4(most > 0.0 ? c : vec3(0.0), al);
    if (pc.light.y > 0.0) {
        // The bezel, black, its edge as sharp as the image's texels allow
        float texel = max(fwidth(off), 1e-5);
        o = mix(o, vec4(0.0, 0.0, 0.0, 1.0), clamp((pc.light.y - off) / texel + 0.5, 0.0, 1.0));
    }
    fragColor = o;
}
