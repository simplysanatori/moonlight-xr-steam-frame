#version 450
// Moonlight XR Native - M0 hello triangle. GPLv3.
layout(push_constant) uniform Push { mat4 mvp; } pc;
layout(location = 0) out vec3 vColor;
const vec3 kPos[6] = vec3[](
    // the triangle, 1.5 m ahead of the LOCAL origin
    vec3(-0.30, -0.20, 0.0), vec3(0.30, -0.20, 0.0), vec3(0.0, 0.32, 0.0),
    // a small marker square under it so depth and stereo read clearly
    vec3(-0.05, -0.40, 0.0), vec3(0.05, -0.40, 0.0), vec3(0.0, -0.30, 0.0));
const vec3 kCol[6] = vec3[](
    vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2), vec3(0.2, 0.4, 1.0),
    vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0));
void main() {
    vColor = kCol[gl_VertexIndex];
    gl_Position = pc.mvp * vec4(kPos[gl_VertexIndex], 1.0);
}
