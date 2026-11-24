#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native (issue #7): the Backlight's zone colours, the light an LED
// strip behind the screen would have: 64 strips round the picture's edge, each
// the mean, in linear light, of 6 x 4 reads from 1 % to 10 % in. GPLv3.
//
// Set 0: 0..2 video. Output: MLXR_GLOW_LIGHT_FORMAT (RGBA16F), 64 x 1, linear.
// The pipeline blends by the blend constant (the share of the new colours this
// draw takes, from the time since the last; 1 replaces them).
//
// The zones, their places and the reads are from FramePlayerX
// (https://github.com/remotesojourner/frameplayerx, crates/fp-render/shaders/scene.wgsl, fs_glow_colours and
// glow_zone_point), Copyright 2026 remotesojourner, Apache-2.0, itself a modified version of FramePlayer,
// Copyright (c) 2026 yellkell. Changed: WGSL to GLSL; the zones across and down follow the picture's shape; the
// colours come through this client's own video read, picture grade and crop (zoom, the left eye's half, the letterbox
// detector's content), not from both eyes; eased on the GPU by blending, in half floats, rather than read back; their
// bar search left out.
layout(push_constant) uniform PC {
    vec4 vidRect;
    vec4 vidChroma;
    vec4 crop;     // the part of the frame, x0 y0 w h in frame uv
    vec4 grade;    // u_grade
    vec4 flags;    // x u_gradeOn, y zones across the top (and the bottom)
} pc;

#include "common.glsl"
#include "grade.glsl"
#include "video.glsl"

layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

const float ZONES = 64.0;   // GLOW_ZONES (xr_glow.h), kept in step by hand

// Where in zone z's strip a read goes: along (0 to 1) the edge, clockwise, and
// inset in from it, in the picture's uv
vec2 zonePoint(float z, float along, float inset) {
    float a = pc.flags.y, d = 0.5 * ZONES - a;
    if (z < a) return vec2((z + along) / a, inset);
    if (z < a + d) return vec2(1.0 - inset, (z - a + along) / d);
    if (z < 2.0 * a + d) return vec2(1.0 - (z - a - d + along) / a, 1.0 - inset);
    return vec2(inset, 1.0 - (z - 2.0 * a - d + along) / d);
}

void main() {
    float z = floor(gl_FragCoord.x);
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 6; j++) {
        for (int i = 0; i < 4; i++) {
            vec2 at = zonePoint(z, (float(j) + 0.5) / 6.0, 0.01 + float(i) * 0.03);
            vec3 c = clamp(video(pc.crop.xy + at * pc.crop.zw), 0.0, 1.0);
            if (pc.flags.x > 0.5) {
                c = grade(c, pc.grade);
            }
            sum += srgbToLinear(c);
        }
    }
    fragColor = vec4(sum * (1.0 / 24.0), 1.0);
}
