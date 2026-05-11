
#ifdef _WIN32
#  define XR_USE_PLATFORM_WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <unknwn.h>
#endif
#define XR_USE_GRAPHICS_API_OPENGL

#include "VRSystem.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <GL/glew.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>

namespace Engine {

static glm::mat4 XrPoseToMat4(const XrPosef& pose) {
    glm::quat q(pose.orientation.w,
                pose.orientation.x,
                pose.orientation.y,
                pose.orientation.z);
    glm::mat4 rot   = glm::mat4_cast(q);
    glm::mat4 trans = glm::translate(glm::mat4(1.0f),
        glm::vec3(pose.position.x, pose.position.y, pose.position.z));
    return trans * rot;
}

static glm::mat4 XrFovToProjection(const XrFovf& fov, float nearZ, float farZ) {
    float l = tanf(fov.angleLeft);
    float r = tanf(fov.angleRight);
    float d = tanf(fov.angleDown);
    float u = tanf(fov.angleUp);

    float w = r - l;
    float h = u - d;

    glm::mat4 m(0.0f);
    m[0][0] =  2.0f / w;
    m[1][1] =  2.0f / h;
    m[2][0] = (r + l) / w;
    m[2][1] = (u + d) / h;
    m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m[2][3] = -1.0f;
    m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

struct VRSystemImpl {

    XrInstance instance  = XR_NULL_HANDLE;
    XrSession  session   = XR_NULL_HANDLE;
    XrSpace    appSpace  = XR_NULL_HANDLE;
    XrSystemId systemId  = XR_NULL_SYSTEM_ID;

    XrSwapchain swapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

    XrActionSet actionSet      = XR_NULL_HANDLE;
    XrAction    poseAction     = XR_NULL_HANDLE;
    XrAction    aimAction      = XR_NULL_HANDLE;
    XrAction    triggerAction  = XR_NULL_HANDLE;
    XrAction    squeezeAction  = XR_NULL_HANDLE;
    XrAction    thumbAction    = XR_NULL_HANDLE;
    XrAction    btnAAction     = XR_NULL_HANDLE;
    XrAction    btnBAction     = XR_NULL_HANDLE;
    XrSpace     handSpace[2]   = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrSpace     aimSpace[2]    = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrPath      handPath[2]    = {XR_NULL_PATH, XR_NULL_PATH};

    static constexpr int kMaxImages = 8;
    struct SwapImage { uint32_t texture = 0; uint32_t fbo = 0; uint32_t depthRB = 0; };
    SwapImage images[2][kMaxImages];
    int imageCount[2] = {0, 0};
    int acquiredIdx[2] = {0, 0};

    uint32_t eyeW = 1440;
    uint32_t eyeH = 1600;

    bool    frameBegan          = false;
    int64_t predictedDisplayTime = 0;

    glm::mat4 eyeView[2]  = {glm::mat4(1.0f), glm::mat4(1.0f)};
    XrFovf    eyeFov[2]   = {};
    XrPosef   eyePose[2]  = {};
    bool      viewsValid  = false;
    glm::vec3 hmdPos      = {};

    bool sessionRunning = false;

    VRSystem::HandState hands[2];

    bool SetupActions();
    void UpdateHands(int64_t predictedTime);
    void AcquireSwapchainImage(int eye);
};

VRSystem* VRSystem::s_Instance = nullptr;

VRSystem::VRSystem()  : m_Impl(std::make_unique<VRSystemImpl>()) {}
VRSystem::~VRSystem() { Shutdown(); }

bool VRSystem::Init(SDL_Window* , SDL_GLContext ) {
    s_Instance = this;
    auto& d = *m_Impl;

    {
        uint32_t extCount = 0;
        XrResult probe = xrEnumerateInstanceExtensionProperties(
            nullptr, 0, &extCount, nullptr);
        if (XR_FAILED(probe) || extCount == 0) {
            std::cout << "[VR] No OpenXR runtime found — VR disabled.\n";
            return false;
        }
    }

    {
        XrApplicationInfo appInfo{};
        strncpy(appInfo.applicationName, "MyEngine",    XR_MAX_APPLICATION_NAME_SIZE - 1);
        strncpy(appInfo.engineName,      "MyEngine",    XR_MAX_ENGINE_NAME_SIZE - 1);
        appInfo.applicationVersion = 1;
        appInfo.engineVersion      = 1;
        appInfo.apiVersion         = XR_CURRENT_API_VERSION;

        const char* exts[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        ci.applicationInfo            = appInfo;
        ci.enabledExtensionCount      = 1;
        ci.enabledExtensionNames      = exts;

        if (XR_FAILED(xrCreateInstance(&ci, &d.instance))) {
            std::cerr << "[VR] xrCreateInstance failed.\n";
            return false;
        }
    }

    {
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
        si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (XR_FAILED(xrGetSystem(d.instance, &si, &d.systemId))) {
            std::cout << "[VR] No HMD found — VR disabled.\n";
            xrDestroyInstance(d.instance);
            d.instance = XR_NULL_HANDLE;
            return false;
        }
    }

    {
        PFN_xrGetOpenGLGraphicsRequirementsKHR fn = nullptr;
        xrGetInstanceProcAddr(d.instance,
            "xrGetOpenGLGraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&fn));
        if (!fn) {
            std::cerr << "[VR] xrGetOpenGLGraphicsRequirementsKHR not available.\n";
            xrDestroyInstance(d.instance);
            d.instance = XR_NULL_HANDLE;
            return false;
        }
        XrGraphicsRequirementsOpenGLKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        fn(d.instance, d.systemId, &reqs);
    }

    {
#ifdef _WIN32
        XrGraphicsBindingOpenGLWin32KHR glBind{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        glBind.hDC   = wglGetCurrentDC();
        glBind.hGLRC = wglGetCurrentContext();
#endif
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.systemId = d.systemId;
        sci.next     = &glBind;

        if (XR_FAILED(xrCreateSession(d.instance, &sci, &d.session))) {
            std::cerr << "[VR] xrCreateSession failed.\n";
            xrDestroyInstance(d.instance);
            d.instance = XR_NULL_HANDLE;
            return false;
        }
    }

    {
        XrReferenceSpaceCreateInfo rci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        rci.poseInReferenceSpace.orientation.w = 1.0f;
        xrCreateReferenceSpace(d.session, &rci, &d.appSpace);
    }

    {
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(d.instance, d.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);

        std::vector<XrViewConfigurationView> vcv(viewCount,
            {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(d.instance, d.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewCount, &viewCount, vcv.data());

        if (viewCount >= 2) {
            d.eyeW = vcv[0].recommendedImageRectWidth;
            d.eyeH = vcv[0].recommendedImageRectHeight;
        }
    }

    {

        uint32_t fmtCount = 0;
        xrEnumerateSwapchainFormats(d.session, 0, &fmtCount, nullptr);
        std::vector<int64_t> fmts(fmtCount);
        xrEnumerateSwapchainFormats(d.session, fmtCount, &fmtCount, fmts.data());

        int64_t chosen = fmts.empty() ? GL_RGBA8 : fmts[0];
        for (auto f : fmts) {
            if (f == GL_SRGB8_ALPHA8) { chosen = f; break; }
            if (f == GL_RGBA8)          chosen = f;
        }

        for (int eye = 0; eye < 2; eye++) {
            XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            sci.arraySize   = 1;
            sci.format      = chosen;
            sci.width       = d.eyeW;
            sci.height      = d.eyeH;
            sci.mipCount    = 1;
            sci.faceCount   = 1;
            sci.sampleCount = 1;
            sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                              XR_SWAPCHAIN_USAGE_SAMPLED_BIT;

            xrCreateSwapchain(d.session, &sci, &d.swapchain[eye]);

            uint32_t imgCount = 0;
            xrEnumerateSwapchainImages(d.swapchain[eye], 0, &imgCount, nullptr);
            imgCount = std::min(imgCount, (uint32_t)VRSystemImpl::kMaxImages);

            std::vector<XrSwapchainImageOpenGLKHR> imgs(imgCount,
                {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
            xrEnumerateSwapchainImages(d.swapchain[eye], imgCount, &imgCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));

            d.imageCount[eye] = (int)imgCount;

            for (int i = 0; i < d.imageCount[eye]; i++) {
                auto& si = d.images[eye][i];
                si.texture = imgs[i].image;

                glGenFramebuffers(1, &si.fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, si.fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, si.texture, 0);

                glGenRenderbuffers(1, &si.depthRB);
                glBindRenderbuffer(GL_RENDERBUFFER, si.depthRB);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                      d.eyeW, d.eyeH);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, si.depthRB);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
    }

    if (!d.SetupActions()) {
        std::cerr << "[VR] Controller action setup failed (input won't work).\n";
    }

    {
        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        xrBeginSession(d.session, &bi);
        d.sessionRunning = true;
    }

    std::cout << "[VR] OpenXR session started. Eye resolution: "
              << d.eyeW << "x" << d.eyeH << "\n";
    return true;
}

bool VRSystemImpl::SetupActions() {
    xrStringToPath(instance, "/user/hand/left",  &handPath[0]);
    xrStringToPath(instance, "/user/hand/right", &handPath[1]);

    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(asci.actionSetName,           "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(asci.localizedActionSetName,  "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_FAILED(xrCreateActionSet(instance, &asci, &actionSet))) return false;

    auto makeAction = [&](const char* name, const char* loc,
                          XrActionType type) -> XrAction {
        XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
        strncpy(ci.actionName,           name, XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(ci.localizedActionName,  loc,  XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        ci.actionType            = type;
        ci.countSubactionPaths   = 2;
        ci.subactionPaths        = handPath;
        XrAction a = XR_NULL_HANDLE;
        xrCreateAction(actionSet, &ci, &a);
        return a;
    };

    poseAction    = makeAction("hand_pose",  "Hand Grip Pose", XR_ACTION_TYPE_POSE_INPUT);
    aimAction     = makeAction("aim_pose",   "Hand Aim Pose",  XR_ACTION_TYPE_POSE_INPUT);
    triggerAction = makeAction("trigger",    "Trigger",        XR_ACTION_TYPE_FLOAT_INPUT);
    squeezeAction = makeAction("squeeze",    "Squeeze",        XR_ACTION_TYPE_FLOAT_INPUT);
    thumbAction   = makeAction("thumbstick", "Thumbstick",     XR_ACTION_TYPE_VECTOR2F_INPUT);
    btnAAction    = makeAction("btn_primary","Button A/X",     XR_ACTION_TYPE_BOOLEAN_INPUT);
    btnBAction    = makeAction("btn_secondary","Button B/Y",   XR_ACTION_TYPE_BOOLEAN_INPUT);

    auto bindFor = [&](const char* path) -> XrPath {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(instance, path, &p);
        return p;
    };

    {
        XrPath touchProfile = XR_NULL_PATH;
        xrStringToPath(instance,
            "/interaction_profiles/oculus/touch_controller", &touchProfile);

        XrActionSuggestedBinding b[] = {
            {poseAction,    bindFor("/user/hand/left/input/grip/pose")},
            {poseAction,    bindFor("/user/hand/right/input/grip/pose")},
            {aimAction,     bindFor("/user/hand/left/input/aim/pose")},
            {aimAction,     bindFor("/user/hand/right/input/aim/pose")},
            {triggerAction, bindFor("/user/hand/left/input/trigger/value")},
            {triggerAction, bindFor("/user/hand/right/input/trigger/value")},
            {squeezeAction, bindFor("/user/hand/left/input/squeeze/value")},
            {squeezeAction, bindFor("/user/hand/right/input/squeeze/value")},
            {thumbAction,   bindFor("/user/hand/left/input/thumbstick")},
            {thumbAction,   bindFor("/user/hand/right/input/thumbstick")},
            {btnAAction,    bindFor("/user/hand/left/input/x/click")},
            {btnAAction,    bindFor("/user/hand/right/input/a/click")},
            {btnBAction,    bindFor("/user/hand/left/input/y/click")},
            {btnBAction,    bindFor("/user/hand/right/input/b/click")},
        };

        XrInteractionProfileSuggestedBinding sp{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sp.interactionProfile     = touchProfile;
        sp.suggestedBindings      = b;
        sp.countSuggestedBindings = (uint32_t)(sizeof(b) / sizeof(b[0]));
        xrSuggestInteractionProfileBindings(instance, &sp);
    }

    {
        XrPath indexProfile = XR_NULL_PATH;
        xrStringToPath(instance,
            "/interaction_profiles/valve/index_controller", &indexProfile);

        XrActionSuggestedBinding b[] = {
            {poseAction,    bindFor("/user/hand/left/input/grip/pose")},
            {poseAction,    bindFor("/user/hand/right/input/grip/pose")},
            {aimAction,     bindFor("/user/hand/left/input/aim/pose")},
            {aimAction,     bindFor("/user/hand/right/input/aim/pose")},
            {triggerAction, bindFor("/user/hand/left/input/trigger/value")},
            {triggerAction, bindFor("/user/hand/right/input/trigger/value")},
            {squeezeAction, bindFor("/user/hand/left/input/squeeze/force")},
            {squeezeAction, bindFor("/user/hand/right/input/squeeze/force")},
            {thumbAction,   bindFor("/user/hand/left/input/thumbstick")},
            {thumbAction,   bindFor("/user/hand/right/input/thumbstick")},
            {btnAAction,    bindFor("/user/hand/left/input/a/click")},
            {btnAAction,    bindFor("/user/hand/right/input/a/click")},
            {btnBAction,    bindFor("/user/hand/left/input/b/click")},
            {btnBAction,    bindFor("/user/hand/right/input/b/click")},
        };

        XrInteractionProfileSuggestedBinding sp{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sp.interactionProfile     = indexProfile;
        sp.suggestedBindings      = b;
        sp.countSuggestedBindings = (uint32_t)(sizeof(b) / sizeof(b[0]));
        xrSuggestInteractionProfileBindings(instance, &sp);
    }

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.actionSets      = &actionSet;
    attachInfo.countActionSets = 1;
    xrAttachSessionActionSets(session, &attachInfo);

    for (int hand = 0; hand < 2; hand++) {
        auto makeSpace = [&](XrAction action, XrSpace& out) {
            XrActionSpaceCreateInfo asci2{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            asci2.action                     = action;
            asci2.subactionPath              = handPath[hand];
            asci2.poseInActionSpace.orientation.w = 1.0f;
            xrCreateActionSpace(session, &asci2, &out);
        };
        makeSpace(poseAction, handSpace[hand]);
        makeSpace(aimAction,  aimSpace[hand]);
    }

    return true;
}

bool VRSystem::BeginFrame() {
    auto& d = *m_Impl;
    if (!d.sessionRunning) return false;

    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(d.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* se = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            if (se->state == XR_SESSION_STATE_READY) {

                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(d.session, &bi);
            } else if (se->state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(d.session);
                d.sessionRunning = false;
                return false;
            } else if (se->state == XR_SESSION_STATE_LOSS_PENDING ||
                       se->state == XR_SESSION_STATE_EXITING) {
                d.sessionRunning = false;
                return false;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState    frameState{XR_TYPE_FRAME_STATE};
    xrWaitFrame(d.session, &waitInfo, &frameState);
    d.predictedDisplayTime = frameState.predictedDisplayTime;

    XrFrameBeginInfo fbi{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(d.session, &fbi);
    d.frameBegan = true;

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime           = frameState.predictedDisplayTime;
    locateInfo.space                 = d.appSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t    viewCount = 2;
    XrView      views[2]  = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    xrLocateViews(d.session, &locateInfo, &viewState, 2, &viewCount, views);

    d.viewsValid =
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

    if (d.viewsValid) {
        for (int eye = 0; eye < 2; eye++) {
            d.eyePose[eye] = views[eye].pose;
            d.eyeFov[eye]  = views[eye].fov;

            d.eyeView[eye] = glm::inverse(XrPoseToMat4(views[eye].pose));
        }
        d.hmdPos = glm::vec3(
            (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
            (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
            (views[0].pose.position.z + views[1].pose.position.z) * 0.5f);
    }

    if (d.actionSet != XR_NULL_HANDLE) {
        XrActiveActionSet active{d.actionSet, XR_NULL_PATH};
        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.activeActionSets      = &active;
        syncInfo.countActiveActionSets = 1;
        xrSyncActions(d.session, &syncInfo);

        d.UpdateHands(frameState.predictedDisplayTime);
    }

    for (int eye = 0; eye < 2; eye++) {
        d.AcquireSwapchainImage(eye);
    }

    return d.viewsValid;
}

void VRSystemImpl::AcquireSwapchainImage(int eye) {
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t idx = 0;
    xrAcquireSwapchainImage(swapchain[eye], &acquireInfo, &idx);

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(swapchain[eye], &waitInfo);

    acquiredIdx[eye] = (int)idx;
}

void VRSystemImpl::UpdateHands(int64_t predictedTime) {
    for (int hand = 0; hand < 2; hand++) {
        auto& hs = hands[hand];

        if (handSpace[hand] != XR_NULL_HANDLE) {
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            xrLocateSpace(handSpace[hand], appSpace, predictedTime, &loc);
            bool posValid  = loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
            bool oriValid  = loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            hs.Active = posValid && oriValid;
            if (hs.Active) {
                hs.GripPose = XrPoseToMat4(loc.pose);
            }
        }

        if (aimSpace[hand] != XR_NULL_HANDLE) {
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            xrLocateSpace(aimSpace[hand], appSpace, predictedTime, &loc);
            if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                hs.AimPose = XrPoseToMat4(loc.pose);
            }
        }

        if (triggerAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action        = triggerAction;
            gi.subactionPath = handPath[hand];
            XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
            xrGetActionStateFloat(session, &gi, &fs);
            hs.Trigger = fs.isActive ? fs.currentState : 0.0f;
        }

        if (squeezeAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action        = squeezeAction;
            gi.subactionPath = handPath[hand];
            XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
            xrGetActionStateFloat(session, &gi, &fs);
            hs.Squeeze = fs.isActive ? fs.currentState : 0.0f;
        }

        if (thumbAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action        = thumbAction;
            gi.subactionPath = handPath[hand];
            XrActionStateVector2f vs{XR_TYPE_ACTION_STATE_VECTOR2F};
            xrGetActionStateVector2f(session, &gi, &vs);
            hs.Thumbstick = vs.isActive
                ? glm::vec2(vs.currentState.x, vs.currentState.y)
                : glm::vec2(0.0f);
        }

        auto readBtn = [&](XrAction action, bool& out) {
            if (action == XR_NULL_HANDLE) return;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action        = action;
            gi.subactionPath = handPath[hand];
            XrActionStateBoolean bs{XR_TYPE_ACTION_STATE_BOOLEAN};
            xrGetActionStateBoolean(session, &gi, &bs);
            out = bs.isActive && bs.currentState;
        };
        readBtn(btnAAction, hs.PrimaryBtn);
        readBtn(btnBAction, hs.SecondaryBtn);
    }
}

void VRSystem::BindEyeFramebuffer(int eye) {
    auto& d = *m_Impl;
    int idx = d.acquiredIdx[eye];
    glBindFramebuffer(GL_FRAMEBUFFER, d.images[eye][idx].fbo);
    glViewport(0, 0, d.eyeW, d.eyeH);
}

void VRSystem::UnbindEyeFramebuffer(int eye) {

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(m_Impl->swapchain[eye], &ri);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VRSystem::EndFrame() {
    auto& d = *m_Impl;
    if (!d.frameBegan) return;
    d.frameBegan = false;

    XrCompositionLayerProjectionView projViews[2]{};
    for (int eye = 0; eye < 2; eye++) {
        projViews[eye].type    = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projViews[eye].pose    = d.eyePose[eye];
        projViews[eye].fov     = d.eyeFov[eye];
        projViews[eye].subImage.swapchain              = d.swapchain[eye];
        projViews[eye].subImage.imageRect.offset        = {0, 0};
        projViews[eye].subImage.imageRect.extent.width  = d.eyeW;
        projViews[eye].subImage.imageRect.extent.height = d.eyeH;
        projViews[eye].subImage.imageArrayIndex         = 0;
    }

    XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projLayer.space     = d.appSpace;
    projLayer.viewCount = 2;
    projLayer.views     = projViews;

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer)
    };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime           = d.predictedDisplayTime;
    endInfo.environmentBlendMode  = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount            = 1;
    endInfo.layers                = layers;

    xrEndFrame(d.session, &endInfo);
}

bool VRSystem::IsActive() const {
    return m_Impl && m_Impl->sessionRunning;
}

uint32_t VRSystem::GetEyeWidth()  const { return m_Impl->eyeW; }
uint32_t VRSystem::GetEyeHeight() const { return m_Impl->eyeH; }

glm::mat4 VRSystem::GetEyeViewMatrix(int eye) const {
    return m_Impl->eyeView[eye];
}

glm::mat4 VRSystem::GetEyeProjectionMatrix(int eye, float nearZ, float farZ) const {
    return XrFovToProjection(m_Impl->eyeFov[eye], nearZ, farZ);
}

glm::vec3 VRSystem::GetHMDPosition() const {
    return m_Impl->hmdPos;
}

const VRSystem::HandState& VRSystem::GetHandState(int hand) const {
    return m_Impl->hands[hand];
}

void VRSystem::Shutdown() {
    if (!m_Impl) return;
    auto& d = *m_Impl;

    if (d.sessionRunning) {
        xrEndSession(d.session);
        d.sessionRunning = false;
    }

    for (int eye = 0; eye < 2; eye++) {
        for (int i = 0; i < d.imageCount[eye]; i++) {
            if (d.images[eye][i].fbo)     glDeleteFramebuffers(1, &d.images[eye][i].fbo);
            if (d.images[eye][i].depthRB) glDeleteRenderbuffers(1, &d.images[eye][i].depthRB);
        }
        if (d.swapchain[eye] != XR_NULL_HANDLE)
            xrDestroySwapchain(d.swapchain[eye]);
        if (d.handSpace[eye] != XR_NULL_HANDLE)
            xrDestroySpace(d.handSpace[eye]);
        if (d.aimSpace[eye] != XR_NULL_HANDLE)
            xrDestroySpace(d.aimSpace[eye]);
    }

    if (d.actionSet != XR_NULL_HANDLE) xrDestroyActionSet(d.actionSet);
    if (d.appSpace  != XR_NULL_HANDLE) xrDestroySpace(d.appSpace);
    if (d.session   != XR_NULL_HANDLE) xrDestroySession(d.session);
    if (d.instance  != XR_NULL_HANDLE) xrDestroyInstance(d.instance);

    d = {};
    if (s_Instance == this) s_Instance = nullptr;
}

}
