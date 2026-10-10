#version 450
// Moonlight XR Native: panel, new in the native port. Draws a texture on a
// flat or cylindrical surface into a projection layer. Upstream hands the
// screen, the curved glow and its panels to the compositor as quad and
// cylinder layers; SteamVR on the Frame has no XR_KHR_composition_layer_
// cylinder, so a curved surface has to be drawn by the app. GPLv3.
//
// Attribute-less: draw shape.w * 6 vertices (shape.w segments across).
// Local space: centred on the origin, +x right, +y up, the surface facing
// +z (the viewer); a curved one (radius > 0) bends its sides toward +z
// round an axis at (0, 0, radius), the way upstream's cylinder layer is
// posed (poseOffset(screenPose, {0, 0, radius})). mvp is GL style (the C
// side's xr_math), converted to Vulkan clip space as in room.vert.
layout(push_constant) uniform PC {
    mat4 mvp;       // viewProj * model, GL clip conventions
    vec4 shape;     // width m, height m, radius m (0 flat), segments
    vec4 uvRect;    // region of the texture: x0, y0, w, h (v = 0 is the top)
    vec4 color;     // multiplier, premultiplied (rgb * a, a)
} pc;

layout(location = 0) out vec2 v_uv;

void main() {
    const vec2 corner[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    int segments = int(pc.shape.w);
    int quad = gl_VertexIndex / 6;
    vec2 c = corner[gl_VertexIndex % 6];
    float u = (float(quad) + c.x) / float(segments);
    float v = c.y;
    vec3 pos;
    pos.y = (0.5 - v) * pc.shape.y;
    if (pc.shape.z > 0.0) {
        float a = (u - 0.5) * pc.shape.x / pc.shape.z;
        pos.x = pc.shape.z * sin(a);
        pos.z = pc.shape.z * (1.0 - cos(a));
    } else {
        pos.x = (u - 0.5) * pc.shape.x;
        pos.z = 0.0;
    }
    v_uv = pc.uvRect.xy + vec2(u, v) * pc.uvRect.zw;
    vec4 p = pc.mvp * vec4(pos, 1.0);
    gl_Position = vec4(p.x, -p.y, 0.5 * (p.z + p.w), p.w);
}
