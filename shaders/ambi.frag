#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native: AMBI_FRAGMENT_SRC from xr_shaders.c, the "frame
// colour sample" that feeds the ambilight glow and the room's light, and
// that the letterbox detector draws. GPLv3.
//
// 32 square, the same 4x4 box the depth downscale uses. u_crop is the
// region of the frame worth sampling, x0 y0 w h in frame uv. The grade goes
// on each tap, so the sample is the average of what the screen shows.
//
// Set 0: 0..2 video. Output: RGBA8_UNORM 32x32. Upstream blends the new
// sample into the old with GL_CONSTANT_ALPHA; the pipeline here blends with
// VK_BLEND_FACTOR_CONSTANT_ALPHA and the blend constant set per draw (1.0
// for an unseeded sample or the detector, which is a plain overwrite).
layout(push_constant) uniform PC {
    vec4 vidRect;
    vec4 vidChroma;
    vec4 crop;     // u_crop
    vec4 grade;    // u_grade
    vec4 flags;    // x u_gradeOn
} pc;

#include "grade.glsl"
#include "video.glsl"

layout(location = 0) in vec2 v_plain;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 base = pc.crop.xy + v_plain * pc.crop.zw;
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            // Shrunk with the crop, so the box stays inside the content
            vec2 off = (vec2(float(x), float(y)) - 1.5) * (0.25 / 32.0) * pc.crop.zw;
            vec3 c = video(base + off);
            if (pc.flags.x > 0.5) {
                c = grade(c, pc.grade);
            }
            sum += c;
        }
    }
    fragColor = vec4(sum * (1.0 / 16.0), 1.0);
}
