#version 450
// Moonlight XR Native: three-plane YUV 4:2:0 (BT.709, limited range, as the
// stream is configured) to linear RGB for an sRGB swapchain. GPLv3.
layout(set = 0, binding = 0) uniform sampler2D texY;
layout(set = 0, binding = 1) uniform sampler2D texU;
layout(set = 0, binding = 2) uniform sampler2D texV;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
    float y = (texture(texY, vUv).r - 16.0 / 255.0) * (255.0 / 219.0);
    float u = (texture(texU, vUv).r - 128.0 / 255.0) * (255.0 / 224.0);
    float v = (texture(texV, vUv).r - 128.0 / 255.0) * (255.0 / 224.0);
    vec3 rgb = vec3(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u);
    outColor = vec4(srgbToLinear(clamp(rgb, 0.0, 1.0)), 1.0);
}
