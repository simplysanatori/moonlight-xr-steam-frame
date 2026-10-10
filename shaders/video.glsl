// Moonlight XR Native: the decoded video as the passes read it. Replaces
// upstream's samplerExternalOES plus u_texmatrix. GPLv3.
//
// Include after the push constant block `pc`, whose first two members are
//   vec4 vidRect;    picture region in texture uv: x0, y0, w, h
//   vec4 vidChroma;  xy: chroma sample shift in uv (three-plane only),
//                    zw: one luma texel in uv
// The pass's uv (0..1 over the picture, v = 0 at the top row in memory)
// becomes vidRect.xy + uv * vidRect.zw, which stands in for u_texmatrix:
// no SurfaceTexture transform exists here, only the crop a padded decode
// buffer needs.
//
// VIDEO_NV12 = 0: three R8 planes (software decode), set 0 bindings 0, 1, 2
//   = Y, U, V, BT.709 limited range converted here, the same sums as
//   xr/shaders/yuv420p.frag.
// VIDEO_NV12 = 1: one G8_B8R8_2PLANE_420 image at binding 0 through an
//   immutable sampler with a VkSamplerYcbcrConversion (BT.709, narrow range),
//   which hands back RGB; bindings 1 and 2 are unused.
#ifndef VIDEO_NV12
#define VIDEO_NV12 0
#endif

#if VIDEO_NV12
layout(set = 0, binding = 0) uniform sampler2D videoTex;
vec3 videoAt(vec2 tc) {
    return texture(videoTex, tc).rgb;
}
#else
layout(set = 0, binding = 0) uniform sampler2D videoY;
layout(set = 0, binding = 1) uniform sampler2D videoU;
layout(set = 0, binding = 2) uniform sampler2D videoV;
vec3 videoAt(vec2 tc) {
    vec2 ctc = tc + pc.vidChroma.xy;
    float y = (texture(videoY, tc).r - 16.0 / 255.0) * (255.0 / 219.0);
    float u = (texture(videoU, ctc).r - 128.0 / 255.0) * (255.0 / 224.0);
    float v = (texture(videoV, ctc).r - 128.0 / 255.0) * (255.0 / 224.0);
    return vec3(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u);
}
#endif

// From the pass's frame uv, the way upstream did texmatrix * vec4(tc, 0, 1)
vec2 videoTc(vec2 uv) {
    return pc.vidRect.xy + uv * pc.vidRect.zw;
}

vec3 video(vec2 uv) {
    return videoAt(videoTc(uv));
}
