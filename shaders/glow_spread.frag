#version 450
// Moonlight XR Native (issue #7): the Backlight's zone colours spread round the
// picture's edge, a Gaussian in linear light: row 0 as beside the screen, row 1
// halfway out, row 2 at the glow's reach. GPLv3.
//
// Set 0: 0 the 64 x 1 zones (texelFetch only). Output: RGBA16F, 66 x 3: the 64
// zones with the last one before them and the first one after, so a bilinear
// read runs on round the corner the ring closes at.
//
// The three spreads are FramePlayerX's (https://github.com/remotesojourner/frameplayerx,
// crates/fp-render/src/glow.rs, SPREAD_ZONES and spread_out), Copyright 2026 remotesojourner, Apache-2.0, itself a
// modified version of FramePlayer, Copyright (c) 2026 yellkell. Changed: from Rust on the CPU to a pass; all 64 zones
// kept at each spread, not a half and a quarter of them.
layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(location = 0) out vec4 fragColor;

const int ZONES = 64;   // GLOW_ZONES
const float SIGMA[3] = float[3](1.0, 2.0, 6.0);

void main() {
    int i = int(gl_FragCoord.x) - 1;
    float sigma = SIGMA[clamp(int(gl_FragCoord.y), 0, 2)];
    int reach = int(ceil(3.0 * sigma));
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int k = -reach; k <= reach; k++) {
        float w = exp(-float(k * k) / (2.0 * sigma * sigma));
        sum += w * texelFetch(u_texture, ivec2((i + k + 2 * ZONES) % ZONES, 0), 0).rgb;
        total += w;
    }
    fragColor = vec4(sum / total, 1.0);
}
