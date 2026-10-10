// Moonlight XR Native: shared pieces of the Vulkan ports of Moonlight XR's
// GL ES programs (xr_shaders.c). GPLv3.
//
// Output encoding. Upstream draws every swapchain with GL_FRAMEBUFFER_SRGB
// disabled, so the gamma encoded values its shaders compute land in the
// SRGB8_ALPHA8 image as they are and the compositor decodes them. A Vulkan
// view of an _SRGB format always encodes on write, so a shader writing into
// one sets OUT_LINEAR and hands the hardware the decoded value: the stored
// bytes are then the ones upstream stored. Through a _UNORM view of the same
// image (XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT) OUT_LINEAR stays false and
// nothing is converted. Alpha is never encoded. Premultiplied colour is
// decoded as it stands, which is what keeps the bytes equal.
layout(constant_id = 0) const bool OUT_LINEAR = false;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec4 encodeOut(vec4 c) {
    if (OUT_LINEAR) {
        c.rgb = srgbToLinear(clamp(c.rgb, 0.0, 1.0));
    }
    return c;
}
