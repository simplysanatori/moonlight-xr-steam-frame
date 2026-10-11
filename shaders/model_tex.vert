#version 450
// Moonlight XR Native (new, no upstream program): the textured controller
// model, the Steam Frame's own controllers as tools/frame-models converts them
// from SteamVR (MXFM). Lighting runs in the model's own space: the pass hands
// over the camera, the key light and the room's up already turned into it, so
// the vertex stage moves nothing but the position. GPLv3.
//
// Vertex buffer 0, MLXR_MODEL_TEX_VERTEX_FLOATS (12) floats a vertex:
// position 3, normal 3, tangent 4 (w: the bitangent's sign), uv 2.
// Clip space converted from GL's as in room.vert.
layout(push_constant) uniform PC {
    mat4 mvp;     // viewProj * model * part
    vec4 eye;     // camera position, model space
    vec4 key;     // xyz: toward the key light, model space
    vec4 up;      // xyz: the room's up, model space
    vec4 params;  // x: debug view, y: base colour gain
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;
layout(location = 3) out vec2 v_uv;

void main() {
    v_position = a_position;
    v_normal = a_normal;
    v_tangent = a_tangent;
    v_uv = a_uv;
    vec4 p = pc.mvp * vec4(a_position, 1.0);
    gl_Position = vec4(p.x, -p.y, 0.5 * (p.z + p.w), p.w);
}
