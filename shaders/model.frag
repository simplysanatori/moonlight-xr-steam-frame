#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: MODEL_FRAGMENT_SRC from xr_shaders.c. Spec constant
// 0: OUT_LINEAR. GPLv3.
#include "common.glsl"
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = encodeOut(vec4(v_color, 1.0));
}
