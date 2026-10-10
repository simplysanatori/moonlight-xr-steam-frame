#version 450
// Moonlight XR Native: VERTEX_SRC from xr_shaders.c. Upstream drew a strip
// of four client side vertices, NDC (-1,-1) at uv (0,0); this is one
// attribute-less triangle over the target. Draw with 3 vertices. GPLv3.
//
// Origin: GL's NDC y = -1 is the framebuffer's row 0 (bottom left origin)
// and Vulkan's is also row 0 (top left origin), so in both APIs v = 0 is
// memory row 0 of the target and of every texture read at v_plain. The
// passes therefore read and write the same rows in the same order as
// upstream's and nothing is flipped; only the meaning of "row 0" differs
// (upstream: the picture's bottom after the SurfaceTexture flip; here: the
// picture's top, which is also what the Vulkan compositor shows first).
layout(location = 0) out vec2 v_plain;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_plain = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
