#version 450
#extension GL_GOOGLE_include_directive : require
// Moonlight XR Native (new, no upstream program): the textured controller
// model's fragment stage. Spec constant 0: OUT_LINEAR. GPLv3.
//
// The Frame's plastic is near black (base colour about sRGB 15 of 255) and
// fairly glossy, so, as under SteamVR, its shape shows in its highlights: a
// key light from above and behind the viewer, a sky/ground fill (lit by the
// ambient occlusion), GGX highlights from the key and a reflection of the
// same sky with Schlick's Fresnel, which rims the edges and runs along the
// grip's indent. All of it in linear light, stored as the bytes every pass
// here stores (writeLinear, as encodeOut would). Specular aliasing is held
// down by widening the roughness by the normal map's own spread in its mips
// (Toksvig) and by the geometric normal's change across the pixel
// (Kaplanyan).
#include "common.glsl"

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 eye;
    vec4 key;
    vec4 up;
    vec4 params;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_colour;   // sRGB base colour, AO in alpha
layout(set = 0, binding = 1) uniform sampler2D u_surface;  // normal (OpenGL green), roughness in alpha

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

const vec3 KEY = vec3(1.0, 0.97, 0.92) * 2.6;
const vec3 SKY = vec3(0.60, 0.64, 0.70);
const vec3 GROUND = vec3(0.10, 0.095, 0.09);
const float F0 = 0.04; // plastic
// Matte (the owner found the plastic too shiny): Valve's roughness map is low, so
// it is lifted to a floor and the lobe broadened, the key highlight is toned down
// and the edge reflection of the sky cut to a faint sheen. The shape still reads
// from the broad highlight and the occlusion-dimmed sky fill on the near-black plastic.
const float ROUGH_FLOOR = 0.5;  // GGX roughness (alpha = rough^2) never below this
const float SPEC_GAIN = 0.7;    // the key light's highlight
const float BASE_FLOOR = 0.014;  // linear: the plastic's base colour is never darker than this, so the matte
                                 // diffuse light shows its shape (Valve's texture is sRGB 15, 0.005)
const float ENV_GAIN = 0.25;     // the sky's Fresnel reflection
const float PI = 3.14159265;

vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Linear light out: through an _SRGB view as it is (the hardware encodes),
// through a UNORM one gamma encoded here, the bytes the same either way
vec4 writeLinear(vec3 c) {
    return vec4(OUT_LINEAR ? clamp(c, 0.0, 1.0) : linearToSrgb(c), 1.0);
}

vec3 sky(vec3 dir, vec3 up, float spread) {
    // spread 0: a sharp horizon; 1: the hemisphere's average
    float t = clamp(0.5 + 0.5 * dot(dir, up) / max(spread + 0.15, 0.15), 0.0, 1.0);
    return mix(GROUND, SKY, t);
}

void main() {
    int view = int(pc.params.x + 0.5);
    vec4 base = texture(u_colour, v_uv);
    vec4 surf = texture(u_surface, v_uv);
    // params.y: MLXR_FRAME_MODEL_GAIN, the base colour times this in linear light
    vec3 albedo = max(base.rgb, vec3(BASE_FLOOR)) * (pc.params.y > 0.0 ? pc.params.y : 1.0);
    float ao = base.a;

    vec3 ng = normalize(v_normal);
    if (!gl_FrontFacing) ng = -ng;
    vec3 t = v_tangent.xyz - ng * dot(ng, v_tangent.xyz);
    t = dot(t, t) > 1e-10 ? normalize(t) : vec3(1.0, 0.0, 0.0);
    vec3 b = cross(ng, t) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
    vec3 nt = surf.xyz * 2.0 - 1.0;
    float ntLen = max(length(nt), 1e-3);
    vec3 n = view == 2 ? ng : normalize(t * nt.x + b * nt.y + ng * nt.z);

    if (view == 1) {
        fragColor = vec4(n * 0.5 + 0.5, 1.0); // raw: what the bytes show
        return;
    }
    if (view == 3) {
        fragColor = writeLinear(albedo * ao);
        return;
    }

    vec3 V = normalize(pc.eye.xyz - v_position);
    vec3 L = normalize(pc.key.xyz);
    vec3 U = normalize(pc.up.xyz);
    float NdotV = max(dot(n, V), 1e-4);
    float NdotL = max(dot(n, L), 0.0);

    // Roughness as GGX alpha squared, widened where the normal map's mips
    // average bumps away (Toksvig) and where the surface turns within a pixel
    float rough = mix(ROUGH_FLOOR, 1.0, clamp(surf.a, 0.0, 1.0) * 0.5);
    float a2 = rough * rough * rough * rough;
    a2 += (1.0 - ntLen) / ntLen;
    vec3 dnx = dFdx(ng), dny = dFdy(ng);
    a2 += min(2.0 * (dot(dnx, dnx) + dot(dny, dny)), 0.18);
    a2 = clamp(a2, 2e-4, 1.0);

    // Diffuse: the key, and the sky/ground fill dimmed by the occlusion
    vec3 diffuse = albedo * (KEY * NdotL / PI + sky(n, U, 1.0) * ao);

    // GGX key highlight (Smith, Schlick's Fresnel)
    vec3 H = normalize(L + V);
    float NdotH = max(dot(n, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float k = sqrt(a2) * 0.5;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    vec3 spec = KEY * SPEC_GAIN * (D * G * F / (4.0 * NdotV)); // * NdotL / NdotL

    // The sky's reflection: rougher blurs the horizon, Fresnel (with
    // roughness) rims the edges, the occlusion keeps it out of the holes
    vec3 R = reflect(-V, n);
    float fr = F0 + (max(1.0 - sqrt(sqrt(a2)), F0) - F0) * pow(1.0 - NdotV, 5.0);
    float specAo = clamp(ao * ao * 1.2, 0.0, 1.0);
    vec3 env = sky(R, U, sqrt(sqrt(a2))) * fr * specAo * ENV_GAIN;

    vec3 colour = diffuse + spec + env;
    fragColor = writeLinear(colour);
}
