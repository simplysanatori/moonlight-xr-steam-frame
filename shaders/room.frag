#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: ROOM_FRAGMENT_SRC from xr_shaders.c. Picks between the
// part's vertex colour and its atlas (u_texMix 0 or the room's mix), turned
// down by u_dim, plus the wash of the picture's light. GPLv3.
//
// Upstream declares mediump; Vulkan GLSL here is full precision (the output
// is 8 bit either way). Atlases are ASTC UNORM (not _SRGB), sampled as the
// gamma encoded values they hold, as upstream does. A part without an atlas
// binds the 1x1 white the library keeps. Spec constant 0: OUT_LINEAR.
layout(push_constant) uniform PC {
    mat4 viewproj;
    vec4 params;     // u_spillGain, u_texMix, u_dim, spare
} pc;

#include "common.glsl"

layout(set = 0, binding = 1) uniform sampler2D u_room;
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_wash;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 base = mix(v_color, texture(u_room, v_uv).rgb, pc.params.y) * pc.params.z;
    fragColor = encodeOut(vec4(base + v_wash, 1.0));
}
