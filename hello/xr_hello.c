// Moonlight XR Native - M0: OpenXR probe and two-eye Vulkan triangle.
//
// mlxr-hello --probe          list what SteamVR offers a native Arm64 Vulkan app, then exit
// mlxr-hello [--seconds N] [--rate HZ]
//                             draw a world-locked triangle in both eyes until SteamVR
//                             ends the session (or N seconds pass), logging the refresh rate
//
// Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.

#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static const uint32_t kVertSpv[] =
#include "tri.vert.inc"
    ;
static const uint32_t kFragSpv[] =
#include "tri.frag.inc"
    ;

// ---------------------------------------------------------------- logging

static double nowSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double g_t0;

static void logf_(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[%8.3f] ", nowSec() - g_t0);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);
}
#define LOG(...) logf_(__VA_ARGS__)

static XrInstance g_instance = XR_NULL_HANDLE;

static const char* xrStr(XrResult r) {
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (g_instance != XR_NULL_HANDLE && xrResultToString(g_instance, r, buf) == XR_SUCCESS) return buf;
    snprintf(buf, sizeof(buf), "XrResult %d", (int)r);
    return buf;
}

#define XR_CHECK(call)                                                      \
    do {                                                                    \
        XrResult r_ = (call);                                               \
        if (XR_FAILED(r_)) {                                                \
            LOG("FAIL %s -> %s (line %d)", #call, xrStr(r_), __LINE__);     \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

#define VK_CHECK(call)                                                      \
    do {                                                                    \
        VkResult r_ = (call);                                               \
        if (r_ != VK_SUCCESS) {                                             \
            LOG("FAIL %s -> VkResult %d (line %d)", #call, (int)r_, __LINE__); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

static volatile sig_atomic_t g_quit;
static void onSignal(int s) { (void)s; g_quit = 1; }

// ---------------------------------------------------------------- maths

typedef struct { float m[16]; } Mat4; // column major

static Mat4 matMul(const Mat4* a, const Mat4* b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + row] * b->m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

// Vulkan clip space: y down, z 0..1 (as in the OpenXR SDK's xr_linear.h).
static Mat4 projectionFov(XrFovf fov, float nearZ, float farZ) {
    float l = tanf(fov.angleLeft), r = tanf(fov.angleRight);
    float d = tanf(fov.angleDown), u = tanf(fov.angleUp);
    float w = r - l, h = d - u;
    Mat4 p;
    memset(&p, 0, sizeof(p));
    p.m[0] = 2.0f / w;
    p.m[8] = (r + l) / w;
    p.m[5] = 2.0f / h;
    p.m[9] = (u + d) / h;
    p.m[10] = -farZ / (farZ - nearZ);
    p.m[14] = -(farZ * nearZ) / (farZ - nearZ);
    p.m[11] = -1.0f;
    return p;
}

static Mat4 viewFromPose(XrPosef pose) {
    float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
    // rotation matrix R (column major)
    float R[9] = {
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
        2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
        2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y),
    };
    // view = [R^T | -R^T t]
    Mat4 v;
    memset(&v, 0, sizeof(v));
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) v.m[c * 4 + row] = R[row * 3 + c];
    float tx = pose.position.x, ty = pose.position.y, tz = pose.position.z;
    for (int row = 0; row < 3; row++)
        v.m[12 + row] = -(v.m[0 * 4 + row] * tx + v.m[1 * 4 + row] * ty + v.m[2 * 4 + row] * tz);
    v.m[15] = 1;
    return v;
}

static Mat4 translation(float x, float y, float z) {
    Mat4 t;
    memset(&t, 0, sizeof(t));
    t.m[0] = t.m[5] = t.m[10] = t.m[15] = 1;
    t.m[12] = x;
    t.m[13] = y;
    t.m[14] = z;
    return t;
}

// ---------------------------------------------------------------- extensions

typedef struct {
    const char* name;
    int present;
    int enable; // enable when present
} ExtWant;

static ExtWant g_exts[] = {
    {"XR_KHR_vulkan_enable2", 0, 1},
    {"XR_KHR_vulkan_enable", 0, 0},
    {"XR_KHR_opengl_enable", 0, 0},
    {"XR_MNDX_egl_enable", 0, 0},
    {"XR_FB_display_refresh_rate", 0, 1},
    {"XR_EXT_hand_tracking", 0, 1},
    {"XR_EXT_hand_interaction", 0, 0},
    {"XR_EXT_eye_gaze_interaction", 0, 1},
    {"XR_EXT_user_presence", 0, 1},
    {"XR_VALVE_frame_controller_interaction", 0, 1},
    {"XR_FB_passthrough", 0, 0},
    {"XR_KHR_composition_layer_cylinder", 0, 0},
    {"XR_KHR_composition_layer_depth", 0, 0},
    {"XR_KHR_composition_layer_color_scale_bias", 0, 0},
    {"XR_FB_composition_layer_settings", 0, 0},
    {"XR_KHR_composition_layer_equirect2", 0, 0},
    {"XR_KHR_visibility_mask", 0, 0},
    {"XR_EXT_performance_settings", 0, 0},
    {"XR_KHR_convert_timespec_time", 0, 0},
    {"XR_EXT_debug_utils", 0, 0},
};
#define N_EXTS (sizeof(g_exts) / sizeof(g_exts[0]))

static int hasExt(const char* name) {
    for (size_t i = 0; i < N_EXTS; i++)
        if (!strcmp(g_exts[i].name, name)) return g_exts[i].present;
    return 0;
}

// ---------------------------------------------------------------- binding probe

typedef struct {
    XrActionSet set;
    XrAction pose, flt, bln, vec2, haptic;
    XrPath hands[2];
} ProbeActions;

static XrPath toPath(const char* s) {
    XrPath p = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(g_instance, s, &p))) LOG("  bad path string %s", s);
    return p;
}

static XrAction makeAction(XrActionSet set, const char* name, XrActionType type, XrPath* hands) {
    XrActionCreateInfo ci = {XR_TYPE_ACTION_CREATE_INFO};
    snprintf(ci.actionName, sizeof(ci.actionName), "%s", name);
    snprintf(ci.localizedActionName, sizeof(ci.localizedActionName), "%s", name);
    ci.actionType = type;
    ci.countSubactionPaths = 2;
    ci.subactionPaths = hands;
    XrAction a;
    XR_CHECK(xrCreateAction(set, &ci, &a));
    return a;
}

static XrAction actionForPath(ProbeActions* pa, const char* comp) {
    size_t n = strlen(comp);
#define ENDS(s) (n >= strlen(s) && !strcmp(comp + n - strlen(s), s))
    if (ENDS("/pose")) return pa->pose;
    if (ENDS("/value") || ENDS("/force")) return pa->flt;
    if (ENDS("/click") || ENDS("/touch")) return pa->bln;
    if (ENDS("haptic")) return pa->haptic;
    return pa->vec2; // thumbstick, trackpad
#undef ENDS
}

// Each path is suggested on its own, so one the profile lacks shows up by
// name instead of failing the whole set (the rule the real app follows).
static void probeProfile(ProbeActions* pa, const char* profile, const char* const* comps, int nComps,
                         FILE* out) {
    XrPath prof = toPath(profile);
    int accepted = 0;
    char line[4096] = "";
    fprintf(out, "profile %s\n", profile);
    for (int i = 0; i < nComps; i++) {
        for (int h = 0; h < 2; h++) {
            char full[256];
            snprintf(full, sizeof(full), "/user/hand/%s/%s", h ? "right" : "left", comps[i]);
            XrActionSuggestedBinding b = {actionForPath(pa, comps[i]), toPath(full)};
            XrInteractionProfileSuggestedBinding s = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            s.interactionProfile = prof;
            s.countSuggestedBindings = 1;
            s.suggestedBindings = &b;
            XrResult r = xrSuggestInteractionProfileBindings(g_instance, &s);
            fprintf(out, "  %-6s %-32s %s\n", h ? "right" : "left", comps[i],
                    XR_SUCCEEDED(r) ? "ok" : xrStr(r));
            if (XR_SUCCEEDED(r)) accepted++;
        }
    }
    snprintf(line, sizeof(line), "profile %s: %d of %d paths accepted", profile, accepted, nComps * 2);
    LOG("%s", line);
}

static void probeBindings(FILE* out) {
    ProbeActions pa;
    XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(si.actionSetName, "probe");
    strcpy(si.localizedActionSetName, "probe");
    XR_CHECK(xrCreateActionSet(g_instance, &si, &pa.set));
    pa.hands[0] = toPath("/user/hand/left");
    pa.hands[1] = toPath("/user/hand/right");
    pa.pose = makeAction(pa.set, "pose", XR_ACTION_TYPE_POSE_INPUT, pa.hands);
    pa.flt = makeAction(pa.set, "float", XR_ACTION_TYPE_FLOAT_INPUT, pa.hands);
    pa.bln = makeAction(pa.set, "bool", XR_ACTION_TYPE_BOOLEAN_INPUT, pa.hands);
    pa.vec2 = makeAction(pa.set, "vec2", XR_ACTION_TYPE_VECTOR2F_INPUT, pa.hands);
    pa.haptic = makeAction(pa.set, "haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, pa.hands);

    static const char* const frame[] = {
        "input/aim/pose", "input/grip/pose", "input/trigger/value", "input/trigger/click",
        "input/trigger/touch", "input/squeeze/value", "input/squeeze/click", "input/squeeze/force",
        "input/grip/value", "input/grip/click", "input/thumbstick", "input/thumbstick/click",
        "input/thumbstick/touch", "input/trackpad", "input/trackpad/click", "input/trackpad/touch",
        "input/trackpad/force", "input/a/click", "input/a/touch", "input/b/click", "input/b/touch",
        "input/x/click", "input/x/touch", "input/y/click", "input/y/touch", "input/menu/click",
        "input/view/click", "input/system/click", "input/bumper/click", "input/bumper/value",
        "input/dpad_up/click", "input/dpad_down/click", "input/dpad_left/click",
        "input/dpad_right/click", "input/thumbrest/touch", "output/haptic",
    };
    static const char* const touch[] = {
        "input/aim/pose", "input/trigger/value", "input/squeeze/value", "input/thumbstick",
        "input/a/click", "input/x/click", "input/menu/click", "output/haptic",
    };
    static const char* const simple[] = {
        "input/aim/pose", "input/select/click", "input/menu/click", "output/haptic",
    };
    probeProfile(&pa, "/interaction_profiles/valve/frame_controller", frame,
                 (int)(sizeof(frame) / sizeof(frame[0])), out);
    probeProfile(&pa, "/interaction_profiles/oculus/touch_controller", touch,
                 (int)(sizeof(touch) / sizeof(touch[0])), out);
    probeProfile(&pa, "/interaction_profiles/khr/simple_controller", simple,
                 (int)(sizeof(simple) / sizeof(simple[0])), out);
    if (hasExt("XR_EXT_eye_gaze_interaction")) {
        XrActionSuggestedBinding b = {pa.pose, toPath("/user/eyes_ext/input/gaze_ext/pose")};
        // gaze actions need no subaction path; reuse the pose action only to test the path
        XrInteractionProfileSuggestedBinding s = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        s.interactionProfile = toPath("/interaction_profiles/ext/eye_gaze_interaction");
        s.countSuggestedBindings = 1;
        s.suggestedBindings = &b;
        XrResult r = xrSuggestInteractionProfileBindings(g_instance, &s);
        fprintf(out, "profile /interaction_profiles/ext/eye_gaze_interaction\n  gaze_ext/pose %s\n",
                XR_SUCCEEDED(r) ? "ok" : xrStr(r));
        LOG("eye gaze profile binding: %s", XR_SUCCEEDED(r) ? "ok" : xrStr(r));
    }
}

// ---------------------------------------------------------------- Vulkan via XR_KHR_vulkan_enable2

static PFN_vkCmdBeginRenderingKHR pfnBeginRendering;
static PFN_vkCmdEndRenderingKHR pfnEndRendering;

typedef struct {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue;
} Vk;

static void createVulkan(XrSystemId system, Vk* vk) {
    PFN_xrGetVulkanGraphicsRequirements2KHR getReq;
    PFN_xrCreateVulkanInstanceKHR createInst;
    PFN_xrGetVulkanGraphicsDevice2KHR getDev;
    PFN_xrCreateVulkanDeviceKHR createDev;
    XR_CHECK(xrGetInstanceProcAddr(g_instance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&getReq));
    XR_CHECK(xrGetInstanceProcAddr(g_instance, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*)&createInst));
    XR_CHECK(xrGetInstanceProcAddr(g_instance, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction*)&getDev));
    XR_CHECK(xrGetInstanceProcAddr(g_instance, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*)&createDev));

    XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    XR_CHECK(getReq(g_instance, system, &req));
    LOG("vulkan api range accepted by the runtime: %u.%u .. %u.%u",
        XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
        XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported));

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Moonlight XR Native";
    app.applicationVersion = 1;
    app.pEngineName = "mlxr";
    // SteamVR 2.18 reports Vulkan 1.0 .. 1.2 as tested; stay inside it and take
    // dynamic rendering as VK_KHR_dynamic_rendering (Turnip has it).
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xci = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xci.systemId = system;
    xci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xci.vulkanCreateInfo = &ici;
    VkResult vr;
    XR_CHECK(createInst(g_instance, &xci, &vk->instance, &vr));
    VK_CHECK(vr);

    XrVulkanGraphicsDeviceGetInfoKHR gi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gi.systemId = system;
    gi.vulkanInstance = vk->instance;
    XR_CHECK(getDev(g_instance, &gi, &vk->phys));
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk->phys, &props);
    LOG("vulkan device: %s, api %u.%u.%u", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
        VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, NULL);
    VkQueueFamilyProperties qf[16];
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, qf);
    vk->queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk->queueFamily = i;
            break;
        }
    if (vk->queueFamily == UINT32_MAX) {
        LOG("FAIL no graphics queue family");
        exit(1);
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = vk->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dr.dynamicRendering = VK_TRUE;
    static const char* const devExts[] = {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &dr;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    XrVulkanDeviceCreateInfoKHR xdci = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xdci.systemId = system;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = vk->phys;
    xdci.vulkanCreateInfo = &dci;
    XR_CHECK(createDev(g_instance, &xdci, &vk->device, &vr));
    VK_CHECK(vr);
    vkGetDeviceQueue(vk->device, vk->queueFamily, 0, &vk->queue);
    pfnBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(vk->device, "vkCmdBeginRenderingKHR");
    pfnEndRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(vk->device, "vkCmdEndRenderingKHR");
    if (!pfnBeginRendering || !pfnEndRendering) {
        LOG("FAIL VK_KHR_dynamic_rendering entry points missing");
        exit(1);
    }
}

// ---------------------------------------------------------------- swapchains and pipeline

#define MAX_IMAGES 8

typedef struct {
    XrSwapchain handle;
    int32_t width, height;
    uint32_t count;
    XrSwapchainImageVulkan2KHR images[MAX_IMAGES];
    VkImageView views[MAX_IMAGES];
} Eye;

static VkShaderModule makeShader(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(dev, &ci, NULL, &m));
    return m;
}

static VkPipeline makePipeline(VkDevice dev, VkFormat format, VkPipelineLayout* layoutOut) {
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
    VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(dev, &lci, NULL, layoutOut));

    VkShaderModule vs = makeShader(dev, kVertSpv, sizeof(kVertSpv));
    VkShaderModule fs = makeShader(dev, kFragSpv, sizeof(kFragSpv));
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL},
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rci;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = *layoutOut;
    VkPipeline pipe;
    VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipe));
    vkDestroyShaderModule(dev, vs, NULL);
    vkDestroyShaderModule(dev, fs, NULL);
    return pipe;
}

static const char* vkFormatName(int64_t f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_D16_UNORM: return "D16_UNORM";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case VK_FORMAT_D32_SFLOAT: return "D32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_R8G8B8A8_SNORM: return "R8G8B8A8_SNORM";
    case VK_FORMAT_R5G6B5_UNORM_PACK16: return "R5G6B5_UNORM";
    case VK_FORMAT_R16G16B16A16_UNORM: return "R16G16B16A16_UNORM";
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "B10G11R11_UFLOAT";
    default: return "?";
    }
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    g_t0 = nowSec();
    int probe = 0;
    double seconds = 0;
    float wantRate = 0;
    const char* probeOut = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--probe")) probe = 1;
        else if (!strcmp(argv[i], "--probe-out") && i + 1 < argc) probeOut = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) wantRate = (float)atof(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--probe [--probe-out FILE]] [--seconds N] [--rate HZ]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    FILE* out = stdout;
    if (probeOut) {
        out = fopen(probeOut, "w");
        if (!out) {
            perror(probeOut);
            return 1;
        }
    }

    // ---- instance
    uint32_t n = 0;
    XrResult er = xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL);
    if (XR_FAILED(er)) {
        LOG("no OpenXR runtime reachable (%d). Is SteamVR running and ~/.config/openxr/1/active_runtime.json set?",
            (int)er);
        return 1;
    }
    XrExtensionProperties* props = calloc(n, sizeof(*props));
    for (uint32_t i = 0; i < n; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(NULL, n, &n, props));
    if (probe) fprintf(out, "# instance extensions (%u)\n", n);
    for (uint32_t i = 0; i < n; i++) {
        if (probe) fprintf(out, "%s v%u\n", props[i].extensionName, props[i].extensionVersion);
        for (size_t k = 0; k < N_EXTS; k++)
            if (!strcmp(g_exts[k].name, props[i].extensionName)) g_exts[k].present = 1;
    }
    free(props);

    uint32_t nl = 0;
    xrEnumerateApiLayerProperties(0, &nl, NULL);
    if (probe) fprintf(out, "# api layers: %u\n", nl);

    if (!hasExt("XR_KHR_vulkan_enable2")) {
        LOG("FAIL runtime lacks XR_KHR_vulkan_enable2");
        return 1;
    }

    const char* enable[N_EXTS];
    uint32_t nEnable = 0;
    for (size_t k = 0; k < N_EXTS; k++)
        if (g_exts[k].present && g_exts[k].enable) enable[nEnable++] = g_exts[k].name;

    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "Moonlight XR Native");
    ici.applicationInfo.applicationVersion = 1;
    snprintf(ici.applicationInfo.engineName, sizeof(ici.applicationInfo.engineName), "mlxr");
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = nEnable;
    ici.enabledExtensionNames = enable;
    XR_CHECK(xrCreateInstance(&ici, &g_instance));

    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    XR_CHECK(xrGetInstanceProperties(g_instance, &ip));
    LOG("runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
        XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    if (probe) {
        fprintf(out, "# runtime %s %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
        fprintf(out, "# wanted extensions\n");
        for (size_t k = 0; k < N_EXTS; k++)
            fprintf(out, "%-45s %s\n", g_exts[k].name, g_exts[k].present ? "PRESENT" : "absent");
    }

    // ---- system
    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    XrResult sr = xrGetSystem(g_instance, &sgi, &system);
    if (XR_FAILED(sr)) {
        LOG("FAIL xrGetSystem -> %s (headset not ready?)", xrStr(sr));
        return 1;
    }
    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    XrSystemHandTrackingPropertiesEXT hand = {XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemEyeGazeInteractionPropertiesEXT gaze = {XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemUserPresencePropertiesEXT presence = {XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT};
    const void** tail = (const void**)&sp.next;
    if (hasExt("XR_EXT_hand_tracking")) { *tail = &hand; tail = (const void**)&hand.next; }
    if (hasExt("XR_EXT_eye_gaze_interaction")) { *tail = &gaze; tail = (const void**)&gaze.next; }
    if (hasExt("XR_EXT_user_presence")) { *tail = &presence; tail = (const void**)&presence.next; }
    XR_CHECK(xrGetSystemProperties(g_instance, system, &sp));
    LOG("system: %s (vendor %u), max swapchain %ux%u, max layers %u", sp.systemName, sp.vendorId,
        sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight,
        sp.graphicsProperties.maxLayerCount);
    if (probe) {
        fprintf(out, "# system\nname %s\nvendor %u\nmaxSwapchain %ux%u\nmaxLayerCount %u\n"
                     "orientationTracking %u\npositionTracking %u\n",
                sp.systemName, sp.vendorId, sp.graphicsProperties.maxSwapchainImageWidth,
                sp.graphicsProperties.maxSwapchainImageHeight, sp.graphicsProperties.maxLayerCount,
                sp.trackingProperties.orientationTracking, sp.trackingProperties.positionTracking);
        if (hasExt("XR_EXT_hand_tracking")) fprintf(out, "supportsHandTracking %u\n", hand.supportsHandTracking);
        if (hasExt("XR_EXT_eye_gaze_interaction"))
            fprintf(out, "supportsEyeGazeInteraction %u\n", gaze.supportsEyeGazeInteraction);
        if (hasExt("XR_EXT_user_presence")) fprintf(out, "supportsUserPresence %u\n", presence.supportsUserPresence);
    }

    // ---- view configuration and blend modes
    XrViewConfigurationType vct[8];
    uint32_t nv = 0;
    XR_CHECK(xrEnumerateViewConfigurations(g_instance, system, 8, &nv, vct));
    XrEnvironmentBlendMode blends[8];
    uint32_t nb = 0;
    XR_CHECK(xrEnumerateEnvironmentBlendModes(g_instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &nb,
                                              blends));
    XrViewConfigurationView vcv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t nvv = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(g_instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nvv,
                                               vcv));
    LOG("stereo view: recommended %ux%u (max %ux%u), %u swapchain samples", vcv[0].recommendedImageRectWidth,
        vcv[0].recommendedImageRectHeight, vcv[0].maxImageRectWidth, vcv[0].maxImageRectHeight,
        vcv[0].recommendedSwapchainSampleCount);
    if (probe) {
        fprintf(out, "# view configurations:");
        for (uint32_t i = 0; i < nv; i++) fprintf(out, " %d", (int)vct[i]);
        fprintf(out, "\n# blend modes (1 opaque, 2 additive, 3 alpha_blend):");
        for (uint32_t i = 0; i < nb; i++) fprintf(out, " %d", (int)blends[i]);
        fprintf(out, "\n# stereo views\n");
        for (uint32_t i = 0; i < nvv; i++)
            fprintf(out, "eye %u recommended %ux%u max %ux%u samples %u\n", i, vcv[i].recommendedImageRectWidth,
                    vcv[i].recommendedImageRectHeight, vcv[i].maxImageRectWidth, vcv[i].maxImageRectHeight,
                    vcv[i].recommendedSwapchainSampleCount);
        fprintf(out, "# binding probe (each path suggested on its own)\n");
        probeBindings(out);
    }

    // ---- Vulkan + session
    Vk vk;
    createVulkan(system, &vk);
    XrGraphicsBindingVulkan2KHR gb = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    gb.instance = vk.instance;
    gb.physicalDevice = vk.phys;
    gb.device = vk.device;
    gb.queueFamilyIndex = vk.queueFamily;
    gb.queueIndex = 0;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = system;
    XrSession session;
    XR_CHECK(xrCreateSession(g_instance, &sci, &session));
    LOG("session created");

    XrReferenceSpaceType spaces[8];
    uint32_t ns = 0;
    XR_CHECK(xrEnumerateReferenceSpaces(session, 8, &ns, spaces));
    int64_t formats[64];
    uint32_t nf = 0;
    XR_CHECK(xrEnumerateSwapchainFormats(session, 64, &nf, formats));

    PFN_xrEnumerateDisplayRefreshRatesFB enumRates = NULL;
    PFN_xrGetDisplayRefreshRateFB getRate = NULL;
    PFN_xrRequestDisplayRefreshRateFB requestRate = NULL;
    float rates[16];
    uint32_t nr = 0;
    float rate = 0;
    if (hasExt("XR_FB_display_refresh_rate")) {
        xrGetInstanceProcAddr(g_instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction*)&enumRates);
        xrGetInstanceProcAddr(g_instance, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&getRate);
        xrGetInstanceProcAddr(g_instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction*)&requestRate);
        if (enumRates) enumRates(session, 16, &nr, rates);
        if (getRate) getRate(session, &rate);
    }

    if (probe) {
        fprintf(out, "# reference spaces (1 view, 2 local, 3 stage, 1000426000 local_floor):");
        for (uint32_t i = 0; i < ns; i++) fprintf(out, " %d", (int)spaces[i]);
        fprintf(out, "\n# swapchain formats (VkFormat)\n");
        for (uint32_t i = 0; i < nf; i++) fprintf(out, "%lld %s\n", (long long)formats[i], vkFormatName(formats[i]));
        fprintf(out, "# display refresh rates:");
        for (uint32_t i = 0; i < nr; i++) fprintf(out, " %.2f", rates[i]);
        fprintf(out, "\n# current refresh rate: %.2f\n", rate);
        if (out != stdout) fclose(out);
        LOG("probe done: %u extensions checked, %u formats, %u refresh rates (current %.2f)", (unsigned)N_EXTS, nf,
            nr, rate);
        xrDestroySession(session);
        xrDestroyInstance(g_instance);
        return 0;
    }

    LOG("refresh rates offered: %u, current %.2f Hz", nr, rate);
    if (wantRate > 0 && requestRate) {
        XrResult rr = requestRate(session, wantRate);
        LOG("requested %.2f Hz -> %s", wantRate, xrStr(rr));
    }

    // ---- space, swapchains, pipeline
    XrReferenceSpaceCreateInfo rsci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1;
    XrSpace local;
    XR_CHECK(xrCreateReferenceSpace(session, &rsci, &local));

    int64_t format = formats[0];
    for (uint32_t i = 0; i < nf; i++)
        if (formats[i] == VK_FORMAT_R8G8B8A8_SRGB || formats[i] == VK_FORMAT_B8G8R8A8_SRGB) {
            format = formats[i];
            break;
        }
    LOG("swapchain format %lld (%s)", (long long)format, vkFormatName(format));

    Eye eyes[2];
    for (int e = 0; e < 2; e++) {
        Eye* eye = &eyes[e];
        eye->width = (int32_t)vcv[e].recommendedImageRectWidth;
        eye->height = (int32_t)vcv[e].recommendedImageRectHeight;
        XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format = format;
        ci.sampleCount = 1;
        ci.width = (uint32_t)eye->width;
        ci.height = (uint32_t)eye->height;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(session, &ci, &eye->handle));
        XR_CHECK(xrEnumerateSwapchainImages(eye->handle, 0, &eye->count, NULL));
        if (eye->count > MAX_IMAGES) eye->count = MAX_IMAGES;
        for (uint32_t i = 0; i < eye->count; i++)
            eye->images[i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
        XR_CHECK(xrEnumerateSwapchainImages(eye->handle, eye->count, &eye->count,
                                            (XrSwapchainImageBaseHeader*)eye->images));
        for (uint32_t i = 0; i < eye->count; i++) {
            VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = eye->images[i].image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = (VkFormat)format;
            vci.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(vk.device, &vci, NULL, &eye->views[i]));
        }
        LOG("eye %d swapchain %dx%d, %u images", e, eye->width, eye->height, eye->count);
    }

    VkPipelineLayout layout;
    VkPipeline pipe = makePipeline(vk.device, (VkFormat)format, &layout);

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = vk.queueFamily;
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(vk.device, &pci, NULL, &pool));
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(vk.device, &cai, &cmd));
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    VK_CHECK(vkCreateFence(vk.device, &fci, NULL, &fence));

    // ---- frame loop
    int running = 1, sessionRunning = 0, exitRequested = 0;
    uint64_t frames = 0, rendered = 0, framesWindow = 0;
    double windowStart = nowSec(), started = nowSec();
    XrTime lastDisplayTime = 0;
    double periodSum = 0;
    uint64_t periodCount = 0;

    while (running) {
        if ((g_quit || (seconds > 0 && nowSec() - started > seconds)) && !exitRequested && sessionRunning) {
            LOG("requesting exit");
            xrRequestExitSession(session);
            exitRequested = 1;
        }
        if (g_quit && !sessionRunning) break;

        XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_instance, &ev) == XR_SUCCESS) {
            switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged* s = (XrEventDataSessionStateChanged*)&ev;
                LOG("session state %d", (int)s->state);
                if (s->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR_CHECK(xrBeginSession(session, &bi));
                    sessionRunning = 1;
                    if (getRate) {
                        getRate(session, &rate);
                        LOG("refresh rate at begin: %.2f Hz", rate);
                    }
                } else if (s->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    sessionRunning = 0;
                } else if (s->state == XR_SESSION_STATE_EXITING || s->state == XR_SESSION_STATE_LOSS_PENDING) {
                    running = 0;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
                XrEventDataDisplayRefreshRateChangedFB* r = (XrEventDataDisplayRefreshRateChangedFB*)&ev;
                LOG("display refresh rate now %.2f Hz (was %.2f)", r->toDisplayRefreshRate, r->fromDisplayRefreshRate);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LOG("instance loss pending");
                running = 0;
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                LOG("reference space change pending (recentre)");
                break;
            default:
                break;
            }
            ev = (XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!running) break;
        if (!sessionRunning) {
            struct timespec ts = {0, 10 * 1000 * 1000};
            nanosleep(&ts, NULL);
            continue;
        }

        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        XR_CHECK(xrWaitFrame(session, NULL, &fs));
        XR_CHECK(xrBeginFrame(session, NULL));
        frames++;
        framesWindow++;
        if (lastDisplayTime) {
            periodSum += (double)(fs.predictedDisplayTime - lastDisplayTime) * 1e-9;
            periodCount++;
        }
        lastDisplayTime = fs.predictedDisplayTime;

        XrCompositionLayerProjectionView pv[2];
        XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[1];
        uint32_t layerCount = 0;

        if (fs.shouldRender) {
            XrViewLocateInfo vli = {XR_TYPE_VIEW_LOCATE_INFO};
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = local;
            XrViewState vs = {XR_TYPE_VIEW_STATE};
            XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            uint32_t nViews = 0;
            XR_CHECK(xrLocateViews(session, &vli, &vs, 2, &nViews, views));
            int posesValid = (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
                             (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);

            uint32_t idx[2];
            for (int e = 0; e < 2; e++) {
                XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XR_CHECK(xrAcquireSwapchainImage(eyes[e].handle, &ai, &idx[e]));
                XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                XR_CHECK(xrWaitSwapchainImage(eyes[e].handle, &wi));
            }

            VK_CHECK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(vk.device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
            Mat4 model = translation(0.0f, 0.0f, -1.5f);
            for (int e = 0; e < 2; e++) {
                Eye* eye = &eyes[e];
                VkImage img = eye->images[idx[e]].image;
                VkImageMemoryBarrier toColor = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toColor.srcAccessMask = 0;
                toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toColor.image = img;
                toColor.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &toColor);

                VkRenderingAttachmentInfo att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                att.imageView = eye->views[idx[e]];
                att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                // left eye a touch bluer, right a touch greener: tells the eyes apart in a capture
                att.clearValue.color = (VkClearColorValue){{0.02f, e ? 0.05f : 0.03f, e ? 0.03f : 0.06f, 1.0f}};
                VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
                ri.renderArea.extent.width = (uint32_t)eye->width;
                ri.renderArea.extent.height = (uint32_t)eye->height;
                ri.layerCount = 1;
                ri.colorAttachmentCount = 1;
                ri.pColorAttachments = &att;
                pfnBeginRendering(cmd, &ri);
                if (posesValid) {
                    VkViewport vp = {0, 0, (float)eye->width, (float)eye->height, 0, 1};
                    VkRect2D sc = {{0, 0}, {(uint32_t)eye->width, (uint32_t)eye->height}};
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    vkCmdSetScissor(cmd, 0, 1, &sc);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    Mat4 p = projectionFov(views[e].fov, 0.05f, 100.0f);
                    Mat4 v = viewFromPose(views[e].pose);
                    Mat4 pv_ = matMul(&p, &v);
                    Mat4 mvp = matMul(&pv_, &model);
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &mvp);
                    vkCmdDraw(cmd, 6, 1, 0, 0);
                }
                pfnEndRendering(cmd);

                pv[e] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                pv[e].pose = views[e].pose;
                pv[e].fov = views[e].fov;
                pv[e].subImage.swapchain = eye->handle;
                pv[e].subImage.imageRect.extent.width = eye->width;
                pv[e].subImage.imageRect.extent.height = eye->height;
            }
            VK_CHECK(vkEndCommandBuffer(cmd));
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, fence));
            for (int e = 0; e < 2; e++) {
                XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                XR_CHECK(xrReleaseSwapchainImage(eyes[e].handle, &ri));
            }
            proj.space = local;
            proj.viewCount = 2;
            proj.views = pv;
            layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&proj;
            rendered++;
        }

        XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO};
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = layerCount;
        fei.layers = layers;
        XR_CHECK(xrEndFrame(session, &fei));

        double t = nowSec();
        if (t - windowStart >= 5.0) {
            if (getRate) getRate(session, &rate);
            double period = periodCount ? periodSum / periodCount : 0;
            LOG("frames %llu (rendered %llu): %.1f fps over %.1f s, runtime period %.3f ms (%.1f Hz), "
                "display %.2f Hz",
                (unsigned long long)frames, (unsigned long long)rendered, framesWindow / (t - windowStart),
                t - windowStart, period * 1e3, period > 0 ? 1.0 / period : 0.0, rate);
            windowStart = t;
            framesWindow = 0;
            periodSum = 0;
            periodCount = 0;
        }
    }

    LOG("shutting down after %llu frames", (unsigned long long)frames);
    vkDeviceWaitIdle(vk.device);
    vkDestroyFence(vk.device, fence, NULL);
    vkDestroyCommandPool(vk.device, pool, NULL);
    vkDestroyPipeline(vk.device, pipe, NULL);
    vkDestroyPipelineLayout(vk.device, layout, NULL);
    for (int e = 0; e < 2; e++) {
        for (uint32_t i = 0; i < eyes[e].count; i++) vkDestroyImageView(vk.device, eyes[e].views[i], NULL);
        xrDestroySwapchain(eyes[e].handle);
    }
    xrDestroySpace(local);
    xrDestroySession(session);
    xrDestroyInstance(g_instance);
    vkDestroyDevice(vk.device, NULL);
    vkDestroyInstance(vk.instance, NULL);
    return 0;
}
