// Moonlight XR Native: OpenXR session, frame loop, video quad, pointer. GPLv3.
//
// Notes from docs/probe/SUMMARY.md: SteamVR accepts a Vulkan 1.2 instance; one
// refresh rate is offered and SteamVR picks it; the Frame controller profile
// rejects some paths, so bindings are filtered per path before suggesting.
#include "xr_session.h"

#include <libavutil/frame.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../shaders/passes.h"
#include "../stream/frame_slot.h"
#include "../stream/hw_video.h"
#include "../video/vk_import.h"
#include "mlxr_log.h"
#include "xr_vk.h"

static const uint32_t kFullscreenVert[] =
#include "fullscreen.vert.inc"
    ;
static const uint32_t kYuvFrag[] =
#include "yuv420p.frag.inc"
    ;

static atomic_int g_quit;
void xr_app_request_quit(void) { atomic_store(&g_quit, 1); }

static XrInstance g_inst;

static const char* xrStr(XrResult r) {
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (g_inst && xrResultToString(g_inst, r, buf) == XR_SUCCESS) return buf;
    snprintf(buf, sizeof(buf), "XrResult %d", (int)r);
    return buf;
}

#define XR_TRY(call)                                                        \
    do {                                                                    \
        XrResult r_ = (call);                                               \
        if (XR_FAILED(r_)) {                                                \
            MLOG_E("%s -> %s", #call, xrStr(r_));                           \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define VK_TRY(call)                                                        \
    do {                                                                    \
        VkResult r_ = (call);                                               \
        if (r_ != VK_SUCCESS) {                                             \
            MLOG_E("%s -> VkResult %d", #call, (int)r_);                    \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

static double nowSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------ input

typedef struct {
    int primed;
    float x, dx;
} Euro;

enum { HAND_L, HAND_R, HANDS };

typedef struct Input {
    XrActionSet set;
    XrAction aim, trigger, alt, stick;
    XrPath hand[HANDS];
    XrSpace aimSpace[HANDS];
    int triggerDown[HANDS], altDown[HANDS];
    int active; // pointing hand
    int lastX, lastY;
    float trig[HANDS];
    // Click stabilisation: from the first touch of the trigger (or A) the host
    // cursor is held at its last position until the aim moves past a slop
    // radius (then it is a drag), and for a moment after release so a second
    // click lands on the same spot (double click). Trigger pulls jolt the aim
    // by a few pixels, which Windows otherwise reads as a tiny drag.
    int freeze, dragging, anchorX, anchorY;
    Euro fx, fy;
    XrTime lastT;
    int lastHand;
    double freezeUntil;
    int hit;          // the pointing ray meets the screen this frame
    float hitU, hitV; // where, 0..1 across and down
    float scrollAccum;
} Input;

static XrPath path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g_inst, s, &p);
    return p;
}

typedef struct {
    XrAction action;
    const char* path;
} Bind;

// Suggests the bindings a profile accepts. A whole-set failure is narrowed
// down path by path, so one path the profile lacks does not drop the rest.
static int suggest(const char* profile, const Bind* binds, int n) {
    XrActionSuggestedBinding sb[32];
    int m = 0;
    for (int i = 0; i < n && m < 32; i++) sb[m++] = (XrActionSuggestedBinding){binds[i].action, path(binds[i].path)};
    XrInteractionProfileSuggestedBinding s = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    s.interactionProfile = path(profile);
    s.suggestedBindings = sb;
    s.countSuggestedBindings = (uint32_t)m;
    if (XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_inst, &s))) {
        MLOG_I("bindings: %s accepted all %d", profile, m);
        return m;
    }
    XrActionSuggestedBinding ok[32];
    int k = 0;
    for (int i = 0; i < m; i++) {
        s.suggestedBindings = &sb[i];
        s.countSuggestedBindings = 1;
        if (XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_inst, &s)))
            ok[k++] = sb[i];
        else
            MLOG_W("bindings: %s rejects %s", profile, binds[i].path);
    }
    if (!k) return 0;
    s.suggestedBindings = ok;
    s.countSuggestedBindings = (uint32_t)k;
    XrResult r = xrSuggestInteractionProfileBindings(g_inst, &s);
    MLOG_I("bindings: %s accepted %d of %d (%s)", profile, k, m, xrStr(r));
    return XR_SUCCEEDED(r) ? k : 0;
}

static XrAction makeAction(XrActionSet set, const char* name, XrActionType type, XrPath* hands) {
    XrActionCreateInfo ci = {XR_TYPE_ACTION_CREATE_INFO};
    snprintf(ci.actionName, sizeof(ci.actionName), "%s", name);
    snprintf(ci.localizedActionName, sizeof(ci.localizedActionName), "%s", name);
    ci.actionType = type;
    ci.countSubactionPaths = HANDS;
    ci.subactionPaths = hands;
    XrAction a = XR_NULL_HANDLE;
    xrCreateAction(set, &ci, &a);
    return a;
}

static int inputCreate(Input* in, XrSession session, int frameExt) {
    memset(in, 0, sizeof(*in));
    in->active = HAND_R;
    in->lastX = in->lastY = -1;
    in->hand[HAND_L] = path("/user/hand/left");
    in->hand[HAND_R] = path("/user/hand/right");
    XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(si.actionSetName, "screen");
    strcpy(si.localizedActionSetName, "Screen");
    if (XR_FAILED(xrCreateActionSet(g_inst, &si, &in->set))) return -1;
    in->aim = makeAction(in->set, "aim", XR_ACTION_TYPE_POSE_INPUT, in->hand);
    in->trigger = makeAction(in->set, "click", XR_ACTION_TYPE_FLOAT_INPUT, in->hand);
    in->alt = makeAction(in->set, "right_click", XR_ACTION_TYPE_BOOLEAN_INPUT, in->hand);
    in->stick = makeAction(in->set, "scroll", XR_ACTION_TYPE_VECTOR2F_INPUT, in->hand);

    if (frameExt) {
        const Bind frame[] = {
            {in->aim, "/user/hand/left/input/aim/pose"},          {in->aim, "/user/hand/right/input/aim/pose"},
            {in->trigger, "/user/hand/left/input/trigger/value"}, {in->trigger, "/user/hand/right/input/trigger/value"},
            {in->alt, "/user/hand/right/input/a/click"},          {in->alt, "/user/hand/left/input/dpad_down/click"},
            {in->stick, "/user/hand/left/input/thumbstick"},      {in->stick, "/user/hand/right/input/thumbstick"},
        };
        suggest("/interaction_profiles/valve/frame_controller", frame, (int)(sizeof(frame) / sizeof(frame[0])));
    }
    const Bind touch[] = {
        {in->aim, "/user/hand/left/input/aim/pose"},          {in->aim, "/user/hand/right/input/aim/pose"},
        {in->trigger, "/user/hand/left/input/trigger/value"}, {in->trigger, "/user/hand/right/input/trigger/value"},
        {in->alt, "/user/hand/right/input/a/click"},          {in->alt, "/user/hand/left/input/x/click"},
        {in->stick, "/user/hand/left/input/thumbstick"},      {in->stick, "/user/hand/right/input/thumbstick"},
    };
    suggest("/interaction_profiles/oculus/touch_controller", touch, (int)(sizeof(touch) / sizeof(touch[0])));
    const Bind simple[] = {
        {in->aim, "/user/hand/left/input/aim/pose"},          {in->aim, "/user/hand/right/input/aim/pose"},
        {in->trigger, "/user/hand/left/input/select/click"},  {in->trigger, "/user/hand/right/input/select/click"},
    };
    suggest("/interaction_profiles/khr/simple_controller", simple, (int)(sizeof(simple) / sizeof(simple[0])));

    XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &in->set;
    if (XR_FAILED(xrAttachSessionActionSets(session, &ai))) return -1;
    for (int h = 0; h < HANDS; h++) {
        XrActionSpaceCreateInfo sci = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = in->aim;
        sci.subactionPath = in->hand[h];
        sci.poseInActionSpace.orientation.w = 1;
        xrCreateActionSpace(session, &sci, &in->aimSpace[h]);
    }
    return 0;
}

static float getFloat(XrSession s, XrAction a, XrPath sub) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = sub;
    XrActionStateFloat st = {XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(s, &gi, &st)) || !st.isActive) return 0;
    return st.currentState;
}

static int getBool(XrSession s, XrAction a, XrPath sub) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = sub;
    XrActionStateBoolean st = {XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(s, &gi, &st)) || !st.isActive) return 0;
    return st.currentState;
}

static XrVector2f getVec2(XrSession s, XrAction a, XrPath sub) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = sub;
    XrActionStateVector2f st = {XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(s, &gi, &st)) || !st.isActive) return (XrVector2f){0, 0};
    return st.currentState;
}

static XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    // v' = v + 2w(q x v) + 2 q x (q x v)
    XrVector3f u = {q.x, q.y, q.z};
    XrVector3f t = {2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x)};
    return (XrVector3f){v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
                        v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

// One Euro filter (Casiez et al. 2012): heavy smoothing when the pointer is
// nearly still (hand tremor), little lag when it moves fast. Pixel units.
static float euroAlpha(float cutoffHz, float dt) {
    float tau = 1.0f / (2.0f * 3.14159265f * cutoffHz);
    return 1.0f / (1.0f + tau / dt);
}

static float euro(Euro* f, float x, float dt, float minCutoff, float beta) {
    if (!f->primed || dt <= 0) {
        f->primed = 1;
        f->x = x;
        f->dx = 0;
        return x;
    }
    float dx = (x - f->x) / dt;
    f->dx += euroAlpha(1.0f, dt) * (dx - f->dx);
    float cutoff = minCutoff + beta * fabsf(f->dx);
    f->x += euroAlpha(cutoff, dt) * (x - f->x);
    return f->x;
}

// Screen: centred at (0, 0, -dist) in LOCAL, facing +z, width w, height hgt.
static int hitScreen(XrPosef aim, float dist, float w, float hgt, float* u, float* v) {
    XrVector3f dir = rotate(aim.orientation, (XrVector3f){0, 0, -1});
    if (dir.z > -1e-4f) return 0;
    float t = (-dist - aim.position.z) / dir.z;
    if (t <= 0) return 0;
    float x = aim.position.x + dir.x * t, y = aim.position.y + dir.y * t;
    *u = x / w + 0.5f;
    *v = 0.5f - y / hgt;
    return *u >= 0 && *u <= 1 && *v >= 0 && *v <= 1;
}

static void inputUpdate(Input* in, XrSession session, XrSpace local, XrTime t, const XrAppConfig* cfg, int videoW,
                        int videoH, float screenH) {
    XrActiveActionSet as = {in->set, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &as;
    if (XR_FAILED(xrSyncActions(session, &sync))) return;
    const XrAppHooks* hk = &cfg->hooks;

    for (int h = 0; h < HANDS; h++) {
        float tv = getFloat(session, in->trigger, in->hand[h]);
        in->trig[h] = tv;
        int down = in->triggerDown[h] ? tv > 0.4f : tv > 0.6f;
        if (down && !in->triggerDown[h]) in->active = h; // the hand that clicks points
        in->triggerDown[h] = down;
    }
    int h = in->active;
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    int hit = 0;
    float u = 0, v = 0;
    if (in->aimSpace[h] && XR_SUCCEEDED(xrLocateSpace(in->aimSpace[h], local, t, &loc)) &&
        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
        (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        hit = hitScreen(loc.pose, cfg->screenDistance, cfg->screenWidth, screenH, &u, &v);
    // Smooth the aim point on the screen (pixels) before anything uses it.
    float dt = in->lastT ? (float)((t - in->lastT) * 1e-9) : 0;
    in->lastT = t;
    if (!hit || h != in->lastHand || videoW <= 0) {
        in->fx.primed = in->fy.primed = 0;
    } else {
        float px = euro(&in->fx, u * videoW, dt, 1.0f, 0.01f);
        float py = euro(&in->fy, v * videoH, dt, 1.0f, 0.01f);
        u = px / videoW;
        v = py / videoH;
    }
    in->lastHand = h;
    in->hit = hit;
    in->hitU = u;
    in->hitV = v;
    int altAny = 0;
    for (int k = 0; k < HANDS; k++) altAny |= getBool(session, in->alt, in->hand[k]);
    int touching = in->trig[h] > 0.12f || in->triggerDown[h] || altAny;
    if (hit && videoW > 0 && hk->pointer) {
        int x = (int)(u * (videoW - 1) + 0.5f), y = (int)(v * (videoH - 1) + 0.5f);
        int slop = (videoW + 95) / 192; // 10 px at 1080p (owner found 38 px too sticky)
        double now = nowSec();
        if (touching && !in->freeze && in->lastX >= 0) {
            in->freeze = 1;
            in->dragging = 0;
            in->anchorX = in->lastX;
            in->anchorY = in->lastY;
        }
        int send = 1;
        if (in->freeze) {
            int dx = x - in->anchorX, dy = y - in->anchorY;
            if (dx * dx + dy * dy > slop * slop) in->dragging = 1;
            send = in->dragging;
            if (touching) {
                in->freezeUntil = 0;
            } else if (in->dragging || (in->freezeUntil > 0 && now > in->freezeUntil)) {
                in->freeze = 0;
                in->freezeUntil = 0;
                send = 1;
            } else if (in->freezeUntil == 0) {
                in->freezeUntil = now + 0.35; // double-click window
            }
        }
        if (in->freeze && !in->dragging) { // keep the dot on the held cursor
            in->hitU = (in->anchorX + 0.5f) / (float)videoW;
            in->hitV = (in->anchorY + 0.5f) / (float)videoH;
        }
        if (send && (x != in->lastX || y != in->lastY)) {
            hk->pointer(hk->user, x, y, videoW, videoH);
            in->lastX = x;
            in->lastY = y;
        }
    }
    // Buttons follow the pointing hand; a press only starts on the screen.
    static int leftHeld;
    int td = in->triggerDown[h];
    if (td && !leftHeld && hit) {
        leftHeld = 1;
        if (hk->button) hk->button(hk->user, 1, 1);
    } else if (!td && leftHeld) {
        leftHeld = 0;
        if (hk->button) hk->button(hk->user, 1, 0);
    }
    for (int k = 0; k < HANDS; k++) {
        int a = getBool(session, in->alt, in->hand[k]);
        if (a != in->altDown[k] && hk->button && (hit || !a)) hk->button(hk->user, 3, a);
        in->altDown[k] = a;
    }
    XrVector2f st = getVec2(session, in->stick, in->hand[h]);
    if (fabsf(st.y) > 0.3f && hk->scroll) {
        in->scrollAccum += st.y * 10.0f; // ~7 notches a second at full tilt
        int amt = (int)in->scrollAccum;
        if (amt) {
            hk->scroll(hk->user, amt);
            in->scrollAccum -= (float)amt;
        }
    } else {
        in->scrollAccum = 0;
    }
}

// ------------------------------------------------------------------ video quad

typedef struct Plane {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
} Plane;

typedef struct Video {
    int width, height;
    XrSwapchain swapchain;
    uint32_t count;
    XrSwapchainImageVulkan2KHR images[8];
    VkImageView views[8];
    Plane planes[3];
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* stagingPtr;
    VkDeviceSize stagingSize;
    VkSampler sampler;
    VkDescriptorSetLayout dsl;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipe;
    int shown; // a frame has been released at least once
    uint64_t uploads;
} Video;

static int makePlane(const XrVk* vk, Plane* p, int w, int h) {
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8_UNORM;
    ci.extent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(vk->device, &ci, NULL, &p->image) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, p->image, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vk->device, &ai, NULL, &p->mem) != VK_SUCCESS) return -1;
    vkBindImageMemory(vk->device, p->image, p->mem, 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = p->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(vk->device, &vi, NULL, &p->view) == VK_SUCCESS ? 0 : -1;
}

static void freePlane(const XrVk* vk, Plane* p) {
    if (p->view) vkDestroyImageView(vk->device, p->view, NULL);
    if (p->image) vkDestroyImage(vk->device, p->image, NULL);
    if (p->mem) vkFreeMemory(vk->device, p->mem, NULL);
    memset(p, 0, sizeof(*p));
}

static int videoPipeline(const XrVk* vk, Video* vd, VkFormat format) {
    VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk->device, &sci, NULL, &vd->sampler) != VK_SUCCESS) return -1;
    VkDescriptorSetLayoutBinding b[3];
    for (int i = 0; i < 3; i++)
        b[i] = (VkDescriptorSetLayoutBinding){(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                              VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 3;
    dli.pBindings = b;
    if (vkCreateDescriptorSetLayout(vk->device, &dli, NULL, &vd->dsl) != VK_SUCCESS) return -1;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(vk->device, &dpi, NULL, &vd->pool) != VK_SUCCESS) return -1;
    VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = vd->pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &vd->dsl;
    if (vkAllocateDescriptorSets(vk->device, &dai, &vd->set) != VK_SUCCESS) return -1;
    VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &vd->dsl;
    if (vkCreatePipelineLayout(vk->device, &lci, NULL, &vd->layout) != VK_SUCCESS) return -1;

    VkShaderModule vs = xr_vk_shader(vk, kFullscreenVert, sizeof(kFullscreenVert));
    VkShaderModule fs = xr_vk_shader(vk, kYuvFrag, sizeof(kYuvFrag));
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL},
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
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
    VkPipelineRenderingCreateInfoKHR rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rci;
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = vd->layout;
    VkResult r = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &gp, NULL, &vd->pipe);
    vkDestroyShaderModule(vk->device, vs, NULL);
    vkDestroyShaderModule(vk->device, fs, NULL);
    return r == VK_SUCCESS ? 0 : -1;
}

static void videoFreeSized(const XrVk* vk, Video* vd) {
    for (uint32_t i = 0; i < vd->count; i++) vkDestroyImageView(vk->device, vd->views[i], NULL);
    vd->count = 0;
    if (vd->swapchain) xrDestroySwapchain(vd->swapchain);
    vd->swapchain = XR_NULL_HANDLE;
    for (int i = 0; i < 3; i++) freePlane(vk, &vd->planes[i]);
    if (vd->staging) {
        vkUnmapMemory(vk->device, vd->stagingMem);
        vkDestroyBuffer(vk->device, vd->staging, NULL);
        vkFreeMemory(vk->device, vd->stagingMem, NULL);
    }
    vd->staging = VK_NULL_HANDLE;
    vd->stagingMem = VK_NULL_HANDLE;
    vd->stagingSize = 0;
    vd->shown = 0;
}

static int videoResize(const XrVk* vk, XrSession session, Video* vd, int64_t format, int w, int h, const AVFrame* f) {
    vkDeviceWaitIdle(vk->device);
    videoFreeSized(vk, vd);
    vd->width = w;
    vd->height = h;
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    if (XR_FAILED(xrCreateSwapchain(session, &ci, &vd->swapchain))) return -1;
    xrEnumerateSwapchainImages(vd->swapchain, 0, &vd->count, NULL);
    if (vd->count > 8) vd->count = 8;
    for (uint32_t i = 0; i < vd->count; i++) vd->images[i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
    xrEnumerateSwapchainImages(vd->swapchain, vd->count, &vd->count, (XrSwapchainImageBaseHeader*)vd->images);
    for (uint32_t i = 0; i < vd->count; i++) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = vd->images[i].image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)format;
        vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(vk->device, &vi, NULL, &vd->views[i]) != VK_SUCCESS) return -1;
    }
    if (!f) { // hardware frames are sampled straight from their imports
        MLOG_I("video quad %dx%d, %u swapchain images (hardware decode)", w, h, vd->count);
        return 0;
    }
    int cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
    if (makePlane(vk, &vd->planes[0], f->width, f->height) || makePlane(vk, &vd->planes[1], cw, ch) ||
        makePlane(vk, &vd->planes[2], cw, ch))
        return -1;
    vd->stagingSize = (VkDeviceSize)f->linesize[0] * f->height + (VkDeviceSize)f->linesize[1] * ch +
                      (VkDeviceSize)f->linesize[2] * ch + 256;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = vd->stagingSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(vk->device, &bi, NULL, &vd->staging) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk->device, vd->staging, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk->device, &ai, NULL, &vd->stagingMem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(vk->device, vd->staging, vd->stagingMem, 0);
    vkMapMemory(vk->device, vd->stagingMem, 0, VK_WHOLE_SIZE, 0, &vd->stagingPtr);

    VkDescriptorImageInfo ii[3];
    VkWriteDescriptorSet wr[3];
    for (int i = 0; i < 3; i++) {
        ii[i] = (VkDescriptorImageInfo){vd->sampler, vd->planes[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr[i].dstSet = vd->set;
        wr[i].dstBinding = (uint32_t)i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(vk->device, 3, wr, 0, NULL);
    MLOG_I("video quad %dx%d, %u swapchain images", f->width, f->height, vd->count);
    return 0;
}

// Records upload + conversion of `f` into swapchain image `idx`.
static void videoRecord(const XrVk* vk, Video* vd, VkCommandBuffer cmd, const AVFrame* f, uint32_t idx) {
    int ch = (f->height + 1) / 2, cw = (f->width + 1) / 2;
    int hs[3] = {f->height, ch, ch}, ws[3] = {f->width, cw, cw};
    VkDeviceSize off = 0;
    VkBufferImageCopy regions[3];
    for (int p = 0; p < 3; p++) {
        size_t bytes = (size_t)f->linesize[p] * hs[p];
        memcpy((uint8_t*)vd->stagingPtr + off, f->data[p], bytes);
        regions[p] = (VkBufferImageCopy){off, (uint32_t)f->linesize[p], 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                         {0, 0, 0}, {(uint32_t)ws[p], (uint32_t)hs[p], 1}};
        off += (bytes + 255) & ~(VkDeviceSize)255;
    }
    for (int p = 0; p < 3; p++) {
        xr_vk_image_barrier(cmd, vd->planes[p].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBufferToImage(cmd, vd->staging, vd->planes[p].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &regions[p]);
        xr_vk_image_barrier(cmd, vd->planes[p].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    VkImage img = vd->images[idx].image;
    xr_vk_image_barrier(cmd, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkRenderingAttachmentInfoKHR att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    att.imageView = vd->views[idx];
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea.extent = (VkExtent2D){(uint32_t)vd->width, (uint32_t)vd->height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vk->cmdBeginRendering(cmd, &ri);
    VkViewport vp = {0, 0, (float)vd->width, (float)vd->height, 0, 1};
    VkRect2D sc = {{0, 0}, {(uint32_t)vd->width, (uint32_t)vd->height}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vd->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vd->layout, 0, 1, &vd->set, 0, NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vk->cmdEndRendering(cmd);
    vd->uploads++;
}

// ------------------------------------------------------------------ hardware video (M2)

#define HW_VIEWS 32

typedef struct HwRender {
    HwVideo* src;           // the decoder the imports belong to
    mlxr_vkimp* imp;
    MlxrPasses* passes;
    VkSemaphore timeline;
    uint64_t value;         // last signalled timeline value
    struct { uint32_t gen, index; VkImageView view; } views[HW_VIEWS];
    uint64_t rendered;
} HwRender;

static void hwReleaseCb(void* user, const mlxr_vframe* f) { hw_video_release((HwVideo*)user, f); }

static void hwDropViews(const XrVk* vk, HwRender* hr) {
    for (int i = 0; i < HW_VIEWS; i++)
        if (hr->views[i].view) vkDestroyImageView(vk->device, hr->views[i].view, NULL);
    memset(hr->views, 0, sizeof(hr->views));
}

// Drops every import (GPU idle first). The frames go back to the decoder.
static void hwDropImports(const XrVk* vk, HwRender* hr) {
    if (!hr->imp) return;
    vkDeviceWaitIdle(vk->device);
    mlxr_vkimp_drain(hr->imp, 1000000000ull);
    hwDropViews(vk, hr);
    mlxr_vkimp_reset(hr->imp);
}

static void hwDestroy(const XrVk* vk, HwRender* hr) {
    if (hr->imp) {
        hwDropImports(vk, hr);
        mlxr_vkimp_destroy(hr->imp);
        hr->imp = NULL;
    }
    hr->src = NULL;
}

static VkImageView hwView(const XrVk* vk, HwRender* hr, const mlxr_vkimp_image* img) {
    int slot = -1;
    for (int i = 0; i < HW_VIEWS; i++) {
        if (hr->views[i].view && hr->views[i].gen == img->generation && hr->views[i].index == img->index)
            return hr->views[i].view;
        if (!hr->views[i].view && slot < 0) slot = i;
    }
    if (slot < 0) {
        hwDropViews(vk, hr); // more buffers than slots: start over (views only, not imports)
        slot = 0;
    }
    VkImageView v = VK_NULL_HANDLE;
    if (mlxr_passes_nv12_view(hr->passes, img->image, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    hr->views[slot].gen = img->generation;
    hr->views[slot].index = img->index;
    hr->views[slot].view = v;
    return v;
}

// ------------------------------------------------------------------ pointer dot

#define DOT_PX 64

// A small static swapchain holding a white dot with a dark rim, filled once
// by a buffer copy; shown as a quad on the screen where the ray lands.
static XrSwapchain makeDot(const XrVk* vk, XrSession session, int64_t format, VkCommandBuffer cmd, VkFence fence) {
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = ci.height = DOT_PX;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    XrSwapchain sc = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateSwapchain(session, &ci, &sc))) {
        // Some runtimes refuse static images; fall back to an ordinary swapchain.
        ci.createFlags = 0;
        if (XR_FAILED(xrCreateSwapchain(session, &ci, &sc))) return XR_NULL_HANDLE;
    }
    uint32_t count = 0;
    XrSwapchainImageVulkan2KHR imgs[8];
    xrEnumerateSwapchainImages(sc, 0, &count, NULL);
    if (count > 8) count = 8;
    for (uint32_t i = 0; i < count; i++) imgs[i] = (XrSwapchainImageVulkan2KHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
    xrEnumerateSwapchainImages(sc, count, &count, (XrSwapchainImageBaseHeader*)imgs);

    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = DOT_PX * DOT_PX * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(vk->device, &bi, NULL, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk->device, buf, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(vk->device, &ai, NULL, &mem);
    vkBindBufferMemory(vk->device, buf, mem, 0);
    uint8_t* px = NULL;
    vkMapMemory(vk->device, mem, 0, VK_WHOLE_SIZE, 0, (void**)&px);
    int bgra = format == VK_FORMAT_B8G8R8A8_SRGB;
    for (int y = 0; y < DOT_PX; y++)
        for (int x = 0; x < DOT_PX; x++) {
            float dx = (x + 0.5f) / DOT_PX * 2 - 1, dy = (y + 0.5f) / DOT_PX * 2 - 1;
            float r = sqrtf(dx * dx + dy * dy);
            float a = fminf(fmaxf((1.0f - r) * DOT_PX * 0.5f, 0.0f), 1.0f);    // anti-aliased edge
            float core = fminf(fmaxf((0.62f - r) * DOT_PX * 0.5f, 0.0f), 1.0f); // white inside, dark rim
            float c = core * 1.0f + (1 - core) * 0.08f;
            uint8_t v = (uint8_t)(c * a * 255.0f + 0.5f); // premultiplied
            uint8_t* o = px + (y * DOT_PX + x) * 4;
            o[0] = o[1] = o[2] = v;
            o[3] = (uint8_t)(a * 255.0f + 0.5f);
            (void)bgra;
        }

    for (uint32_t i = 0; i < (ci.createFlags ? 1u : count); i++) {
        uint32_t idx;
        XrSwapchainImageAcquireInfo aq = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        xrAcquireSwapchainImage(sc, &aq, &idx);
        XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(sc, &wi);
        vkWaitForFences(vk->device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk->device, 1, &fence);
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &cbi);
        VkImage img = imgs[idx].image;
        xr_vk_image_barrier(cmd, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy rg = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {DOT_PX, DOT_PX, 1}};
        vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        xr_vk_image_barrier(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(vk->queue, 1, &si, fence);
        vkWaitForFences(vk->device, 1, &fence, VK_TRUE, UINT64_MAX);
        XrSwapchainImageReleaseInfo rl = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc, &rl);
    }
    vkUnmapMemory(vk->device, mem);
    vkDestroyBuffer(vk->device, buf, NULL);
    vkFreeMemory(vk->device, mem, NULL);
    MLOG_I("pointer dot ready (%s swapchain)", ci.createFlags ? "static" : "ordinary");
    return sc;
}

// ------------------------------------------------------------------ run

static void onSignal(int s) {
    (void)s;
    atomic_store(&g_quit, 1);
}

int xr_app_run(const XrAppConfig* cfg) {
    int rc = 1;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    // ---- instance with the extensions this milestone uses
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL))) {
        MLOG_E("no OpenXR runtime reachable: is SteamVR running?");
        return 1;
    }
    XrExtensionProperties* props = calloc(n, sizeof(*props));
    for (uint32_t i = 0; i < n; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    xrEnumerateInstanceExtensionProperties(NULL, n, &n, props);
    int haveVk = 0, haveFrame = 0, haveRate = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!strcmp(props[i].extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) haveVk = 1;
        if (!strcmp(props[i].extensionName, "XR_VALVE_frame_controller_interaction")) haveFrame = 1;
        if (!strcmp(props[i].extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) haveRate = 1;
    }
    free(props);
    if (!haveVk) {
        MLOG_E("runtime lacks XR_KHR_vulkan_enable2");
        return 1;
    }
    const char* exts[4];
    uint32_t ne = 0;
    exts[ne++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
    if (haveFrame) exts[ne++] = "XR_VALVE_frame_controller_interaction";
    if (haveRate) exts[ne++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(ici.applicationInfo.applicationName, "Moonlight XR Native");
    strcpy(ici.applicationInfo.engineName, "mlxr");
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = ne;
    ici.enabledExtensionNames = exts;
    if (XR_FAILED(xrCreateInstance(&ici, &g_inst))) {
        MLOG_E("xrCreateInstance failed");
        return 1;
    }
    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(g_inst, &ip);
    MLOG_I("runtime %s %u.%u.%u; frame controller ext %d, refresh ext %d", ip.runtimeName,
           XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
           XR_VERSION_PATCH(ip.runtimeVersion), haveFrame, haveRate);

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrResult sr = xrGetSystem(g_inst, &sgi, &system);
    if (XR_FAILED(sr)) {
        MLOG_E("no headset: %s", xrStr(sr));
        xrDestroyInstance(g_inst);
        return 1;
    }
    // View configuration must be enumerated before a session on some runtimes.
    XrViewConfigurationType vct[4];
    uint32_t nv = 0;
    xrEnumerateViewConfigurations(g_inst, system, 4, &nv, vct);

    XrVk vk;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    Input in;
    Video vd;
    memset(&vd, 0, sizeof(vd));
    memset(&in, 0, sizeof(in));
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    XrSwapchain dot = XR_NULL_HANDLE;
    HwRender hr;
    memset(&hr, 0, sizeof(hr));
    uint32_t nImpExt = 0;
    const char* const* impExt = mlxr_vkimp_device_extensions(&nImpExt);
    if (xr_vk_create(g_inst, system, impExt, nImpExt, &vk)) goto fail;

    XrGraphicsBindingVulkan2KHR gb = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    gb.instance = vk.instance;
    gb.physicalDevice = vk.phys;
    gb.device = vk.device;
    gb.queueFamilyIndex = vk.queueFamily;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = system;
    XR_TRY(xrCreateSession(g_inst, &sci, &session));
    XrReferenceSpaceCreateInfo rsci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1;
    XR_TRY(xrCreateReferenceSpace(session, &rsci, &local));
    if (inputCreate(&in, session, haveFrame)) MLOG_W("input setup failed; pointer disabled");

    int64_t formats[32];
    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(session, 32, &nf, formats);
    int64_t format = 0;
    for (uint32_t i = 0; i < nf && !format; i++)
        if (formats[i] == VK_FORMAT_R8G8B8A8_SRGB || formats[i] == VK_FORMAT_B8G8R8A8_SRGB) format = formats[i];
    if (!format) {
        MLOG_E("no sRGB swapchain format");
        goto fail;
    }
    if (videoPipeline(&vk, &vd, (VkFormat)format)) goto fail;

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = vk.queueFamily;
    VK_TRY(vkCreateCommandPool(vk.device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK_TRY(vkAllocateCommandBuffers(vk.device, &cai, &cmd));
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_TRY(vkCreateFence(vk.device, &fci, NULL, &fence));

    dot = makeDot(&vk, session, format, cmd, fence);

    // Shader passes (NV12 through a YCbCr conversion for hardware frames) and
    // the timeline semaphore the importer waits on before requeueing buffers.
    {
        MlxrPassesInfo pi = {0};
        pi.phys = vk.phys;
        pi.device = vk.device;
        pi.nv12 = 1;
        pi.nv12Chroma = MLXR_CHROMA_LEFT; // H.264/HEVC default siting
        if (mlxr_passes_create(&pi, &hr.passes) != 0) {
            MLOG_E("shader passes unavailable; hardware decode frames cannot be shown");
            hr.passes = NULL;
        } else {
            VK_TRY(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
            VK_TRY(vkResetFences(vk.device, 1, &fence));
            VK_TRY(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_TRY(vkBeginCommandBuffer(cmd, &cbi));
            mlxr_passes_prepare(hr.passes, cmd);
            VK_TRY(vkEndCommandBuffer(cmd));
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            VK_TRY(vkQueueSubmit(vk.queue, 1, &si, fence));
        }
        VkSemaphoreTypeCreateInfo st = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        st.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo sci2 = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        sci2.pNext = &st;
        VK_TRY(vkCreateSemaphore(vk.device, &sci2, NULL, &hr.timeline));
    }

    PFN_xrGetDisplayRefreshRateFB getRate = NULL;
    if (haveRate) xrGetInstanceProcAddr(g_inst, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&getRate);

    // ---- loop
    int running = 1, sessionRunning = 0, exitRequested = 0;
    double start = nowSec(), lastStats = start;
    uint64_t frames = 0, framesWindow = 0, uploadsWindow = 0;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    while (running) {
        if ((atomic_load(&g_quit) || (cfg->seconds > 0 && nowSec() - start > cfg->seconds)) && !exitRequested) {
            exitRequested = 1;
            if (sessionRunning) {
                MLOG_I("requesting session exit");
                xrRequestExitSession(session);
            } else {
                break;
            }
        }
        XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                state = ((XrEventDataSessionStateChanged*)&ev)->state;
                MLOG_I("session state %d", (int)state);
                if (cfg->hooks.focus) cfg->hooks.focus(cfg->hooks.user, state == XR_SESSION_STATE_FOCUSED);
                if (state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR_TRY(xrBeginSession(session, &bi));
                    sessionRunning = 1;
                } else if (state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    sessionRunning = 0;
                } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                    running = 0;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                running = 0;
            } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                MLOG_I("recentre (reference space change)");
            } else if (ev.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {
                XrEventDataDisplayRefreshRateChangedFB* r = (XrEventDataDisplayRefreshRateChangedFB*)&ev;
                MLOG_I("display refresh rate %.2f -> %.2f Hz", r->fromDisplayRefreshRate, r->toDisplayRefreshRate);
            }
            ev = (XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!running) break;
        if (!sessionRunning) {
            struct timespec ts = {0, 10000000};
            nanosleep(&ts, NULL);
            continue;
        }

        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        XR_TRY(xrWaitFrame(session, NULL, &fs));
        XR_TRY(xrBeginFrame(session, NULL));
        frames++;
        framesWindow++;

        float screenH = vd.width ? cfg->screenWidth * (float)vd.height / (float)vd.width : cfg->screenWidth * 9.0f / 16.0f;
        if (in.set && state == XR_SESSION_STATE_FOCUSED)
            inputUpdate(&in, session, local, fs.predictedDisplayTime, cfg, vd.width, vd.height, screenH);

        // ---- hardware frames (M2): sampled straight from the decoder's dma-bufs
        HwVideo* hv = hw_video_current();
        if (hv != hr.src) { // a new decoder (or none): imports of the old one go
            hwDestroy(&vk, &hr);
            if (hv && hr.passes) {
                mlxr_vkimp_config ic = {0};
                ic.phys = vk.phys;
                ic.device = vk.device;
                ic.queue_family = vk.queueFamily;
                ic.release = hwReleaseCb;
                ic.release_user = hv;
                ic.linear_filter = true;
                if (mlxr_vkimp_create(&ic, &hr.imp) == 0) {
                    hr.src = hv;
                    MLOG_I("hardware video: importer ready");
                } else {
                    MLOG_E("hardware video: importer failed");
                    hr.imp = NULL;
                }
            }
        }
        if (hr.imp) {
            mlxr_vkimp_poll(hr.imp);
            if (hw_video_format_change_pending(hv)) {
                MLOG_I("hardware video: resolution change, dropping imports");
                hwDropImports(&vk, &hr);
                hw_video_format_change_done(hv);
            }
            mlxr_vframe hf;
            if (hw_video_take(hv, &hf)) {
                const mlxr_vkimp_image* img = fs.shouldRender ? mlxr_vkimp_get(hr.imp, &hf) : NULL;
                VkImageView nv12 = img ? hwView(&vk, &hr, img) : VK_NULL_HANDLE;
                if (!nv12) {
                    hw_video_release(hv, &hf);
                } else {
                    if ((int)hf.width != vd.width || (int)hf.height != vd.height) {
                        if (videoResize(&vk, session, &vd, format, (int)hf.width, (int)hf.height, NULL)) {
                            MLOG_E("video quad %ux%u failed", hf.width, hf.height);
                            hw_video_release(hv, &hf);
                            goto fail;
                        }
                        screenH = cfg->screenWidth * (float)vd.height / (float)vd.width;
                    }
                    uint32_t idx;
                    XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    XR_TRY(xrAcquireSwapchainImage(vd.swapchain, &ai, &idx));
                    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    wi.timeout = XR_INFINITE_DURATION;
                    XR_TRY(xrWaitSwapchainImage(vd.swapchain, &wi));
                    VK_TRY(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
                    VK_TRY(vkResetFences(vk.device, 1, &fence));
                    VK_TRY(vkResetCommandBuffer(cmd, 0));
                    VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    VK_TRY(vkBeginCommandBuffer(cmd, &cbi));
                    mlxr_vkimp_cmd_acquire(hr.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    xr_vk_image_barrier(cmd, vd.images[idx].image, VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                    MlxrTarget tg = {0};
                    tg.view = vd.views[idx];
                    tg.format = (VkFormat)format;
                    tg.rect.extent = (VkExtent2D){(uint32_t)vd.width, (uint32_t)vd.height};
                    MlxrWarpParams w;
                    memset(&w, 0, sizeof(w));
                    w.video.kind = MLXR_VIDEO_NV12;
                    w.video.planes[0] = nv12;
                    w.video.width = hf.width;
                    w.video.height = hf.height;
                    w.video.texWidth = hf.coded_width;
                    w.video.texHeight = hf.coded_height;
                    w.convergence = 0.5f;
                    w.lowResWidth = (float)(hf.width / 4);
                    w.srcInset = 0.5f / (float)hf.width;
                    w.edgeFade = 16.0f / (float)hf.width;
                    w.grade[1] = w.grade[2] = w.grade[3] = 1.0f;
                    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
                    mlxr_pass_warp(hr.passes, cmd, &tg, &w);
                    mlxr_vkimp_cmd_release(hr.imp, cmd, img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    VK_TRY(vkEndCommandBuffer(cmd));
                    uint64_t signal = ++hr.value;
                    VkTimelineSemaphoreSubmitInfo tsi = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
                    tsi.signalSemaphoreValueCount = 1;
                    tsi.pSignalSemaphoreValues = &signal;
                    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
                    si.pNext = &tsi;
                    si.commandBufferCount = 1;
                    si.pCommandBuffers = &cmd;
                    si.signalSemaphoreCount = 1;
                    si.pSignalSemaphores = &hr.timeline;
                    VK_TRY(vkQueueSubmit(vk.queue, 1, &si, fence));
                    // The quad keeps its own copy, so this buffer is sampled once.
                    mlxr_vkimp_frame_done_timeline(hr.imp, &hf, hr.timeline, signal);
                    XrSwapchainImageReleaseInfo rli = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    XR_TRY(xrReleaseSwapchainImage(vd.swapchain, &rli));
                    vd.shown = 1;
                    uploadsWindow++;
                    hr.rendered++;
                }
            }
        }

        AVFrame* f = cfg->video ? frame_slot_take(cfg->video) : NULL;
        if (f && (f->width != vd.width || f->height != vd.height)) {
            if (videoResize(&vk, session, &vd, format, f->width, f->height, f)) {
                MLOG_E("video resize to %dx%d failed", f->width, f->height);
                av_frame_free(&f);
                goto fail;
            }
            screenH = cfg->screenWidth * (float)vd.height / (float)vd.width;
        }
        if (f && fs.shouldRender) {
            uint32_t idx;
            XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XR_TRY(xrAcquireSwapchainImage(vd.swapchain, &ai, &idx));
            XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi.timeout = XR_INFINITE_DURATION;
            XR_TRY(xrWaitSwapchainImage(vd.swapchain, &wi));
            VK_TRY(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX)); // staging is free again
            VK_TRY(vkResetFences(vk.device, 1, &fence));
            VK_TRY(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_TRY(vkBeginCommandBuffer(cmd, &cbi));
            videoRecord(&vk, &vd, cmd, f, idx);
            VK_TRY(vkEndCommandBuffer(cmd));
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            VK_TRY(vkQueueSubmit(vk.queue, 1, &si, fence));
            XrSwapchainImageReleaseInfo rli = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            XR_TRY(xrReleaseSwapchainImage(vd.swapchain, &rli));
            vd.shown = 1;
            uploadsWindow++;
        }
        av_frame_free(&f);

        XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad dotq = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader* layers[2];
        uint32_t nl = 0;
        if (fs.shouldRender && vd.shown) {
            quad.space = local;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = vd.swapchain;
            quad.subImage.imageRect.extent = (XrExtent2Di){vd.width, vd.height};
            quad.pose.orientation.w = 1;
            quad.pose.position.z = -cfg->screenDistance;
            quad.size = (XrExtent2Df){cfg->screenWidth, screenH};
            layers[nl++] = (const XrCompositionLayerBaseHeader*)&quad;
            if (dot && in.hit && state == XR_SESSION_STATE_FOCUSED) {
                dotq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                dotq.space = local;
                dotq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                dotq.subImage.swapchain = dot;
                dotq.subImage.imageRect.extent = (XrExtent2Di){DOT_PX, DOT_PX};
                dotq.pose.orientation.w = 1;
                dotq.pose.position.x = (in.hitU - 0.5f) * cfg->screenWidth;
                dotq.pose.position.y = (0.5f - in.hitV) * screenH;
                dotq.pose.position.z = -cfg->screenDistance + 0.005f;
                dotq.size = (XrExtent2Df){0.03f, 0.03f};
                layers[nl++] = (const XrCompositionLayerBaseHeader*)&dotq;
            }
        }
        XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO};
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = nl;
        fei.layers = layers;
        XR_TRY(xrEndFrame(session, &fei));

        double t = nowSec();
        if (t - lastStats >= 5.0) {
            float rate = 0;
            if (getRate) getRate(session, &rate);
            char extra[256] = "";
            if (cfg->hooks.stats) cfg->hooks.stats(cfg->hooks.user, extra, sizeof(extra));
            MLOG_I("xr: %.1f fps (display %.0f Hz), video %.1f fps shown; %s", framesWindow / (t - lastStats), rate,
                   uploadsWindow / (t - lastStats), extra);
            framesWindow = uploadsWindow = 0;
            lastStats = t;
        }
    }
    rc = 0;
    MLOG_I("session ended after %llu frames", (unsigned long long)frames);

fail:
    if (vk.device) vkDeviceWaitIdle(vk.device);
    if (vk.device) {
        hwDestroy(&vk, &hr);
        if (hr.passes) mlxr_passes_destroy(hr.passes);
        if (hr.timeline) vkDestroySemaphore(vk.device, hr.timeline, NULL);
        videoFreeSized(&vk, &vd);
        if (dot) xrDestroySwapchain(dot);
        if (vd.pipe) vkDestroyPipeline(vk.device, vd.pipe, NULL);
        if (vd.layout) vkDestroyPipelineLayout(vk.device, vd.layout, NULL);
        if (vd.pool) vkDestroyDescriptorPool(vk.device, vd.pool, NULL);
        if (vd.dsl) vkDestroyDescriptorSetLayout(vk.device, vd.dsl, NULL);
        if (vd.sampler) vkDestroySampler(vk.device, vd.sampler, NULL);
        if (fence) vkDestroyFence(vk.device, fence, NULL);
        if (pool) vkDestroyCommandPool(vk.device, pool, NULL);
    }
    for (int h = 0; h < HANDS; h++)
        if (in.aimSpace[h]) xrDestroySpace(in.aimSpace[h]);
    if (in.set) xrDestroyActionSet(in.set);
    if (local) xrDestroySpace(local);
    if (session) xrDestroySession(session);
    xrDestroyInstance(g_inst);
    g_inst = XR_NULL_HANDLE;
    xr_vk_destroy(&vk);
    return rc;
}
