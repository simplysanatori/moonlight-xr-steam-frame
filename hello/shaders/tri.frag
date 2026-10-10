#version 450
// Moonlight XR Native - M0 hello triangle. GPLv3.
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(vColor, 1.0); }
