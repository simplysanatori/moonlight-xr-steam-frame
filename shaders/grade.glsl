// Moonlight XR Native: GRADE_GLSL from xr_shaders.c, the same sums as
// pictureGradeApply in xr/core/xr_grade.c. g is u_grade: the offset, the
// contrast gain, the exponent (one over the gamma) and the saturation. GPLv3.
vec3 grade(vec3 c, vec4 g) {
    c = clamp(c, 0.0, 1.0);
    c = clamp((c - 0.5) * g.y + 0.5 + g.x, 0.0, 1.0);
    c = pow(c, vec3(g.z));
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3(l), c, g.w), 0.0, 1.0);
}
