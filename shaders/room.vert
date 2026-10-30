#version 450
// Moonlight XR Native: ROOM_VERTEX_SRC from xr_shaders.c, the 3D room. The
// light the picture throws is worked out per vertex from five taps of the
// frame colour sample. GPLv3.
//
// Vertex buffer 0, ROOM_VERTEX_FLOATS (10) floats a vertex: position 3,
// colour 3, spill 1, uv 2, spare 1.
// Set 0: 0 u_ambi (32x32 colour sample, vertex stage), 1 u_room (fragment).
//
// u_viewproj is the matrix upstream builds (xr_math projectionFromFov times
// viewFromPose): GL clip space, y up, z -1..1. It is turned into Vulkan's
// (y down, z 0..1) here, so the C maths stays upstream's and the image comes
// out top row first the way a Vulkan swapchain is read.
layout(push_constant) uniform PC {
    mat4 viewproj;   // u_viewproj
    vec4 params;     // u_spillGain, u_texMix, u_dim, u_keep (fragment)
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_ambi;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_spill;
layout(location = 3) in vec2 a_uv;
layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_wash;
layout(location = 2) out vec2 v_uv;

void main() {
    // Centre and the middle of each edge (symmetric, so the sample's row
    // order does not matter)
    vec3 lit = texture(u_ambi, vec2(0.5, 0.5)).rgb;
    lit += texture(u_ambi, vec2(0.15, 0.5)).rgb;
    lit += texture(u_ambi, vec2(0.85, 0.5)).rgb;
    lit += texture(u_ambi, vec2(0.5, 0.15)).rgb;
    lit += texture(u_ambi, vec2(0.5, 0.85)).rgb;
    vec3 wash = lit * 0.2;
    v_color = a_color;
    v_wash = wash * (a_spill * pc.params.x);
    v_uv = a_uv;
    vec4 p = pc.viewproj * vec4(a_position, 1.0);
    gl_Position = vec4(p.x, -p.y, 0.5 * (p.z + p.w), p.w);
}
