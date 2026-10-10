#version 450
// Moonlight XR Native: MODEL_VERTEX_SRC from xr_shaders.c, the controller
// model, painted from its vertex colours and lit per vertex by one light
// fixed in the room over a floor of ambient. GPLv3.
//
// Vertex buffer 0, ROOM_MESH_VERTEX_FLOATS (11) floats a vertex as the .room
// file has them: position 3, normal 3, uv 2 (unused), colour 3.
// Clip space converted from GL's as in room.vert.
layout(push_constant) uniform PC {
    mat4 viewproj;   // u_viewproj
    mat4 model;      // u_model
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 0) out vec3 v_color;

void main() {
    vec3 n = normalize(mat3(pc.model) * a_normal);
    float lit = 0.45 + 0.55 * max(dot(n, vec3(-0.303, 0.808, 0.505)), 0.0);
    v_color = a_color * lit;
    vec4 p = pc.viewproj * (pc.model * vec4(a_position, 1.0));
    gl_Position = vec4(p.x, -p.y, 0.5 * (p.z + p.w), p.w);
}
