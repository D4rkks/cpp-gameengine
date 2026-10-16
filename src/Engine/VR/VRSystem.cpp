
#ifdef _WIN32
#  define XR_USE_PLATFORM_WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <unknwn.h>
#endif
#define XR_USE_GRAPHICS_API_OPENGL

#include "VRSystem.h"
#include "VRUIPlacement.h"
#include "../Renderer/Shader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl3.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <GL/glew.h>
#include <SDL.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>

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
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, frame = false, valid = false, render = false;
    bool cinema = false, interactive = false, uiReady = false;
    bool uiPoseValid = false;
    glm::mat4 uiPose{1.0f};
    float uiAspect = 16.0f / 9.0f;
    struct WorldPatch { unsigned first; glm::mat4 model; };
    std::unique_ptr<ImDrawList> worldUI;
    std::vector<WorldPatch> worldPatches;
    std::unique_ptr<Shader> worldShader;
    GLuint worldVAO = 0, worldVBO = 0, worldEBO = 0;
    bool worldUploaded = false;
    int quality = 1;
    uint32_t revision = 0;
    XrTime time = 0;
    XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    glm::mat4 origin{1}, head{1};
    VRSystem::HandState hands[2];
    XrActionSet actions = XR_NULL_HANDLE;
    XrPath paths[2]{};
    XrAction grip{}, aim{}, trigger{}, squeeze{}, stick{}, primary{}, secondary{}, haptic{};
    XrSpace grips[2]{}, aims[2]{};
    struct Image { GLuint fbo = 0, depth = 0; };
    struct Chain {
        XrSwapchain handle = XR_NULL_HANDLE;
        uint32_t width = 0, height = 0, index = 0;
        bool acquired = false, waited = false, submitted = false;
        std::vector<Image> images;
    } eyes[2], ui;

    void UpdateUIPose() {
        if (!valid) return;
        if (uiPoseValid) return; // A room anchor: looking at an edge never moves it away.
        glm::vec3 headPos(head[3]);
        glm::vec3 headForward = -glm::normalize(glm::vec3(head[2]));
        glm::vec3 flatForward(headForward.x, 0.0f, headForward.z);
        if (glm::length(flatForward) > 0.01f) flatForward = glm::normalize(flatForward);
        else flatForward = glm::vec3(0, 0, -1);

        float pitch = glm::clamp(std::asin(headForward.y), -0.45f, 0.45f);
        glm::vec3 forwardDir = glm::normalize(glm::vec3(flatForward.x * std::cos(pitch), std::sin(pitch), flatForward.z * std::cos(pitch)));
        glm::vec3 targetPos = headPos + forwardDir * 2.0f;

        glm::vec3 quadRight = glm::normalize(glm::cross(forwardDir, glm::vec3(0, 1, 0)));
        glm::vec3 quadUp = glm::normalize(glm::cross(quadRight, forwardDir));

        glm::mat4 targetMat(1.0f);
        targetMat[0] = glm::vec4(quadRight, 0.0f);
        targetMat[1] = glm::vec4(quadUp, 0.0f);
        targetMat[2] = glm::vec4(-forwardDir, 0.0f);
        targetMat[3] = glm::vec4(targetPos, 1.0f);

        uiPose = targetMat;
        uiPoseValid = true;
    }

    bool Check(XrResult result, const char* call) {
        if (XR_SUCCEEDED(result)) return true;
        char description[XR_MAX_RESULT_STRING_SIZE]{};
        if (instance) xrResultToString(instance, result, description);
        std::cerr << "[VR] " << call << ": " << description << " (" << result << ")\n";
        return false;
    }
    bool CreateChain(Chain& c, uint32_t width, uint32_t height, int64_t format) {
        c.width = width; c.height = height;
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
            XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = format; ci.sampleCount = 1; ci.width = width; ci.height = height;
        ci.faceCount = ci.arraySize = ci.mipCount = 1;
        if (!Check(xrCreateSwapchain(session, &ci, &c.handle), "create swapchain")) return false;
        uint32_t count = 0;
        if (!Check(xrEnumerateSwapchainImages(c.handle, 0, &count, nullptr), "count images") || !count) return false;
        std::vector<XrSwapchainImageOpenGLKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        if (!Check(xrEnumerateSwapchainImages(c.handle, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())), "enumerate images")) return false;
        c.images.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& image = c.images[i];
            glGenFramebuffers(1, &image.fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, image.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[i].image, 0);
            glGenRenderbuffers(1, &image.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, image.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, image.depth);
            bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (!complete) { std::cerr << "[VR] Incomplete framebuffer\n"; return false; }
        }
        return true;
    }
    bool Acquire(Chain& c) {
        if (!c.acquired) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (!Check(xrAcquireSwapchainImage(c.handle, &acquire, &c.index), "acquire image")) return false;
            c.acquired = true;
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        XrResult result = xrWaitSwapchainImage(c.handle, &wait);
        c.waited = result == XR_SUCCESS;
        return Check(result, "wait image") && c.waited && c.index < c.images.size();
    }
    void Release(Chain& c) {
        if (!c.acquired || !c.waited) return;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        c.submitted = Check(xrReleaseSwapchainImage(c.handle, &release), "release image");
        c.acquired = c.waited = false;
    }
    bool SetupActions() {
        for (int h = 0; h < 2; ++h)
            if (!Check(xrStringToPath(instance, h ? "/user/hand/right" : "/user/hand/left", &paths[h]), "hand path")) return false;
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(set.actionSetName, "gameplay"); strcpy_s(set.localizedActionSetName, "Gameplay");
        if (!Check(xrCreateActionSet(instance, &set, &actions), "create action set")) return false;
        auto action = [&](XrAction& out, const char* name, XrActionType type) {
            XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
            strcpy_s(ci.actionName, name); strcpy_s(ci.localizedActionName, name);
            ci.actionType = type; ci.countSubactionPaths = 2; ci.subactionPaths = paths;
            return Check(xrCreateAction(actions, &ci, &out), name);
        };
        if (!action(grip, "grip", XR_ACTION_TYPE_POSE_INPUT) || !action(aim, "aim", XR_ACTION_TYPE_POSE_INPUT) ||
            !action(trigger, "trigger", XR_ACTION_TYPE_FLOAT_INPUT) || !action(squeeze, "squeeze", XR_ACTION_TYPE_FLOAT_INPUT) ||
            !action(stick, "stick", XR_ACTION_TYPE_VECTOR2F_INPUT) || !action(primary, "primary", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
            !action(secondary, "secondary", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
            !action(haptic, "haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT)) return false;
        for (int profile = 0; profile < 2; ++profile) {
            std::vector<XrActionSuggestedBinding> bindings;
            auto bind = [&](XrAction act, int hand, const char* suffix) {
                std::string path = std::string(hand ? "/user/hand/right/" : "/user/hand/left/") +
                    (act == haptic ? "output/" : "input/") + suffix;
                XrPath p{};
                if (XR_SUCCEEDED(xrStringToPath(instance, path.c_str(), &p))) bindings.push_back({act, p});
            };
            for (int h = 0; h < 2; ++h) {
                bind(grip, h, "grip/pose"); bind(aim, h, "aim/pose");
                bind(trigger, h, "trigger/value"); bind(squeeze, h, profile ? "squeeze/force" : "squeeze/value");
                bind(stick, h, "thumbstick");
                bind(haptic, h, "haptic");
                bind(primary, h, profile || h ? "a/click" : "x/click");
                bind(secondary, h, profile || h ? "b/click" : "y/click");
            }
            XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            xrStringToPath(instance, profile ? "/interaction_profiles/valve/index_controller" :
                "/interaction_profiles/oculus/touch_controller", &suggested.interactionProfile);
            suggested.countSuggestedBindings = (uint32_t)bindings.size(); suggested.suggestedBindings = bindings.data();
            // A runtime may support only one of these profiles.
            Check(xrSuggestInteractionProfileBindings(instance, &suggested), "suggest controller profile");
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1; attach.actionSets = &actions;
        if (!Check(xrAttachSessionActionSets(session, &attach), "attach actions")) return false;
        for (int h = 0; h < 2; ++h) {
            XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            ci.subactionPath = paths[h]; ci.poseInActionSpace.orientation.w = 1;
            ci.action = grip;
            if (!Check(xrCreateActionSpace(session, &ci, &grips[h]), "grip space")) return false;
            ci.action = aim;
            if (!Check(xrCreateActionSpace(session, &ci, &aims[h]), "aim space")) return false;
        }
        return true;
    }
    void ReadHands() {
        for (int h = 0; h < 2; ++h) {
            auto& hand = hands[h]; hand = {};
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO}; gi.subactionPath = paths[h];
            auto pose = [&](XrAction action, XrSpace location, glm::mat4& out) {
                gi.action = action;
                XrActionStatePose value{XR_TYPE_ACTION_STATE_POSE};
                if (XR_FAILED(xrGetActionStatePose(session, &gi, &value)) || !value.isActive) return false;
                XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
                constexpr XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                if (XR_FAILED(xrLocateSpace(location, space, time, &loc)) || (loc.locationFlags & required) != required) return false;
                out = XrPoseToMat4(loc.pose); return true;
            };
            hand.Active = pose(grip, grips[h], hand.GripPose);
            if (!pose(aim, aims[h], hand.AimPose)) hand.AimPose = hand.GripPose;
            if (!hand.Active) continue;
            auto scalar = [&](XrAction action) {
                gi.action = action; XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
                return XR_SUCCEEDED(xrGetActionStateFloat(session, &gi, &value)) && value.isActive ? value.currentState : 0.0f;
            };
            hand.Trigger = scalar(trigger); hand.Squeeze = scalar(squeeze);
            gi.action = stick; XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &gi, &v)) && v.isActive) hand.Thumbstick = {v.currentState.x, v.currentState.y};
            auto button = [&](XrAction action) {
                gi.action = action; XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
                return XR_SUCCEEDED(xrGetActionStateBoolean(session, &gi, &value)) && value.isActive && value.currentState;
            };
            hand.PrimaryBtn = button(primary); hand.SecondaryBtn = button(secondary);
        }
    }
};

VRSystem* VRSystem::s_Instance = nullptr;
VRSystem::VRSystem() : m_Impl(std::make_unique<VRSystemImpl>()) {}
VRSystem::~VRSystem() { Shutdown(); }

void VRSystem::BeginWorldUIFrame() {
    auto& d = *m_Impl;
    d.worldPatches.clear(); d.worldUploaded = false;
    if (!d.worldUI) d.worldUI = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    d.worldUI->_ResetForNewFrame();
    d.worldUI->Flags |= ImDrawListFlags_AllowVtxOffset;
    d.worldUI->PushTexture(ImGui::GetIO().Fonts->TexRef);
    d.worldUI->PushClipRect(ImVec2(-1000000, -1000000), ImVec2(1000000, 1000000));
}

ImDrawList* VRSystem::GetWorldUIDrawList() { return m_Impl->worldUI.get(); }

void VRSystem::SetWorldUIAnchor(const glm::vec3& position, const glm::vec2& pixelOrigin, float metersPerPixel) {
    auto& d = *m_Impl;
    if (!d.worldUI) return;
    // Upright billboards turn toward the viewer's position, never their head rotation.
    glm::mat4 model = WorldUITransform(position, glm::vec3(GetWorldHeadPose()[3]), pixelOrigin, metersPerPixel);
    SetWorldUITransform(model);
}

void VRSystem::SetWorldUITransform(const glm::mat4& pixelToWorld) {
    auto& d = *m_Impl;
    if (d.worldUI) d.worldPatches.push_back({(unsigned)d.worldUI->IdxBuffer.Size, pixelToWorld});
}

void VRSystem::RenderWorldUI(const glm::mat4& viewProjection) {
    auto& d = *m_Impl;
    if (!d.worldUI || d.worldPatches.empty() || !d.worldUI->IdxBuffer.Size) return;
    // World text is rendered before the flat ImGui pass. Upload newly requested
    // glyphs now, including on the first stereo frame.
    for (auto* texture : ImGui::GetPlatformIO().Textures)
        if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
            ImGui_ImplOpenGL3_UpdateTexture(texture);
    if (!d.worldShader) {
        d.worldShader = std::make_unique<Shader>(R"(#version 330 core
            layout(location=0) in vec2 Position;
            layout(location=1) in vec2 UV;
            layout(location=2) in vec4 Color;
            uniform mat4 u_Matrix;
            out vec2 uv; out vec4 color;
            void main() { uv=UV; color=Color; gl_Position=u_Matrix*vec4(Position,0,1); }
        )", R"(#version 330 core
            in vec2 uv; in vec4 color;
            uniform sampler2D u_Texture;
            out vec4 frag;
            void main() { frag=color*texture(u_Texture,uv); if(frag.a<.01) discard; }
        )");
        glGenVertexArrays(1,&d.worldVAO); glGenBuffers(1,&d.worldVBO); glGenBuffers(1,&d.worldEBO);
    }
    glBindVertexArray(d.worldVAO);
    glBindBuffer(GL_ARRAY_BUFFER,d.worldVBO); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,d.worldEBO);
    if (!d.worldUploaded) {
        glBufferData(GL_ARRAY_BUFFER,d.worldUI->VtxBuffer.Size*sizeof(ImDrawVert),d.worldUI->VtxBuffer.Data,GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,d.worldUI->IdxBuffer.Size*sizeof(ImDrawIdx),d.worldUI->IdxBuffer.Data,GL_STREAM_DRAW);
        d.worldUploaded = true;
    }
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,pos));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,uv));
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(ImDrawVert),(void*)offsetof(ImDrawVert,col));
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    d.worldShader->Bind(); d.worldShader->UploadUniformInt("u_Texture",0);
    glActiveTexture(GL_TEXTURE0);
    for (size_t p=0; p<d.worldPatches.size(); ++p) {
        const auto& patch=d.worldPatches[p];
        unsigned end=p+1<d.worldPatches.size()?d.worldPatches[p+1].first:(unsigned)d.worldUI->IdxBuffer.Size;
        if (end==patch.first) continue;
        d.worldShader->UploadUniformMat4("u_Matrix",viewProjection*patch.model);
        for (const auto& cmd : d.worldUI->CmdBuffer) {
            const unsigned first=std::max(patch.first,cmd.IdxOffset), last=std::min(end,cmd.IdxOffset+cmd.ElemCount);
            if (last<=first || cmd.UserCallback) continue;
            glBindTexture(GL_TEXTURE_2D,(GLuint)cmd.GetTexID());
            glDrawElementsBaseVertex(GL_TRIANGLES,last-first,sizeof(ImDrawIdx)==2?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT,
                (void*)(uintptr_t)(first*sizeof(ImDrawIdx)),cmd.VtxOffset);
        }
    }
    glDepthMask(GL_TRUE); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

bool VRSystem::Init(SDL_Window*, SDL_GLContext) {
    Shutdown(); s_Instance = this;
    auto& d = *m_Impl;
    auto init = [&]() {
        uint32_t count = 0;
        if (!d.Check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "find OpenXR runtime")) return false;
        std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (!d.Check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()), "extensions")) return false;
        if (std::none_of(extensions.begin(), extensions.end(), [](auto& e) { return std::strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0; })) {
            std::cerr << "[VR] Runtime does not support OpenGL.\n"; return false;
        }
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName, "MyEngine"); strcpy_s(ci.applicationInfo.engineName, "MyEngine");
        ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        const char* extension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
        ci.enabledExtensionCount = 1; ci.enabledExtensionNames = &extension;
        if (!d.Check(xrCreateInstance(&ci, &d.instance), "create instance")) return false;
        XrSystemGetInfo system{XR_TYPE_SYSTEM_GET_INFO}; system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (!d.Check(xrGetSystem(d.instance, &system, &d.system), "find headset")) return false;
        PFN_xrGetOpenGLGraphicsRequirementsKHR requirements = nullptr;
        if (!d.Check(xrGetInstanceProcAddr(d.instance, "xrGetOpenGLGraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&requirements)), "OpenGL requirements") || !requirements) return false;
        XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        if (!d.Check(requirements(d.instance, d.system, &req), "OpenGL requirements")) return false;
        GLint major = 0, minor = 0; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
        XrVersion version = XR_MAKE_VERSION(major, minor, 0);
        if (version < req.minApiVersionSupported || version > req.maxApiVersionSupported) {
            std::cerr << "[VR] OpenGL context does not meet runtime requirements.\n"; return false;
        }
        XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        binding.hDC = wglGetCurrentDC(); binding.hGLRC = wglGetCurrentContext();
        XrSessionCreateInfo session{XR_TYPE_SESSION_CREATE_INFO}; session.systemId = d.system; session.next = &binding;
        if (!d.Check(xrCreateSession(d.instance, &session, &d.session), "create session")) return false;
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; space.poseInReferenceSpace.orientation.w = 1;
        if (!d.Check(xrCreateReferenceSpace(d.session, &space, &d.space), "reference space")) return false;
        count = 0;
        if (!d.Check(xrEnumerateViewConfigurationViews(d.instance, d.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "view count") || count != 2) return false;
        XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        if (!d.Check(xrEnumerateViewConfigurationViews(d.instance, d.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views), "views")) return false;
        if (!d.Check(xrEnumerateSwapchainFormats(d.session, 0, &count, nullptr), "format count")) return false;
        std::vector<int64_t> formats(count);
        if (!d.Check(xrEnumerateSwapchainFormats(d.session, count, &count, formats.data()), "formats")) return false;
        // The engine's final colors are already encoded for a display.
        int64_t format = GL_SRGB8_ALPHA8;
        if (std::find(formats.begin(), formats.end(), format) == formats.end()) format = GL_RGBA8;
        if (std::find(formats.begin(), formats.end(), format) == formats.end()) return false;
        for (int eye = 0; eye < 2; ++eye) {
            uint32_t ew = views[eye].recommendedImageRectWidth;
            uint32_t eh = views[eye].recommendedImageRectHeight;
            // Keep the runtime's optical resolution. Quality scales the scene
            // target; an arbitrary swapchain cap also blurred the high preset.
            if (!d.CreateChain(d.eyes[eye], ew, eh, format)) return false;
        }
        if (!d.CreateChain(d.ui, kUIWidth, kUIHeight, format) || !d.SetupActions()) return false;
        std::cout << "[VR] OpenXR ready; waiting for the headset session.\n";
        return true;
    };
    if (init()) return true;
    Shutdown(); return false;
}

bool VRSystem::BeginFrame() {
    auto& d = *m_Impl;
    if (!d.session) return false;
    d.valid = d.render = d.uiReady = false;
    for (auto& c : d.eyes) c.submitted = false;
    d.ui.submitted = false;
    d.hands[0] = {}; d.hands[1] = {};
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(d.instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            d.state = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            if (d.state == XR_SESSION_STATE_READY && !d.running) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                d.running = d.Check(xrBeginSession(d.session, &begin), "begin session");
            } else if (d.state == XR_SESSION_STATE_STOPPING && d.running) {
                d.Check(xrEndSession(d.session), "end session"); d.running = false;
            } else if (d.state == XR_SESSION_STATE_EXITING || d.state == XR_SESSION_STATE_LOSS_PENDING) d.running = false;
        } else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) { ++d.revision; d.uiPoseValid = false; }
        else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) d.running = false;
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (!d.running) return false;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState frame{XR_TYPE_FRAME_STATE};
    if (!d.Check(xrWaitFrame(d.session, &wait, &frame), "wait frame")) return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (!d.Check(xrBeginFrame(d.session, &begin), "begin frame")) return false;
    d.frame = true; d.time = frame.predictedDisplayTime;
    RefreshTracking();
    if (d.state == XR_SESSION_STATE_FOCUSED && d.valid) {
        XrActiveActionSet active{d.actions, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO}; sync.countActiveActionSets = 1; sync.activeActionSets = &active;
        if (xrSyncActions(d.session, &sync) == XR_SUCCESS) d.ReadHands();
    }
    d.UpdateUIPose();
    d.render = d.valid && frame.shouldRender;
    // Finish even an invisible/invalid frame: every xrBeginFrame needs xrEndFrame.
    if (!d.render) EndFrame();
    return d.render;
}

void VRSystem::RefreshTracking() {
    auto& d = *m_Impl;
    if (!d.frame) return;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; locate.displayTime = d.time; locate.space = d.space;
    XrViewState state{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
    constexpr XrViewStateFlags required = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    d.valid = d.Check(xrLocateViews(d.session, &locate, &state, 2, &count, d.views), "locate views") &&
        count == 2 && (state.viewStateFlags & required) == required;
    if (d.valid) {
        d.head = XrPoseToMat4(d.views[0].pose);
        d.head[3] = (XrPoseToMat4(d.views[0].pose)[3] + XrPoseToMat4(d.views[1].pose)[3]) * 0.5f;
    }
    if (!d.valid) return;
    if (d.state != XR_SESSION_STATE_FOCUSED) return;
    // Actions were synced at frame start; only locate the same poses again.
    for (int h = 0; h < 2; ++h) {
        auto& hand = d.hands[h];
        if (!hand.Active) continue;
        auto locateHand = [&](XrSpace space, glm::mat4& pose) {
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            constexpr auto required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (XR_FAILED(xrLocateSpace(space, d.space, d.time, &loc)) || (loc.locationFlags & required) != required) return false;
            pose = XrPoseToMat4(loc.pose); return true;
        };
        hand.Active = locateHand(d.grips[h], hand.GripPose);
        if (!locateHand(d.aims[h], hand.AimPose)) hand.AimPose = hand.GripPose;
    }
}

bool VRSystem::BindEyeFramebuffer(int eye) {
    auto& d = *m_Impl; auto& c = d.eyes[eye];
    if (d.frame && d.render && d.Acquire(c)) {
        glBindFramebuffer(GL_FRAMEBUFFER, c.images[c.index].fbo);
        glViewport(0, 0, c.width, c.height);
        glEnable(GL_FRAMEBUFFER_SRGB);
        return true;
    }
    return false;
}
void VRSystem::UnbindEyeFramebuffer(int eye) {
    auto& d = *m_Impl; auto& c = d.eyes[eye];
    // Mirror the left eye into the desktop while the image is still owned.
    if (eye == 0 && c.waited) {
        int width = 1280, height = 720;
        SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &width, &height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, c.images[c.index].fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        // The application's desktop viewport is restored before ImGui.
        glBlitFramebuffer(0, 0, c.width, c.height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    d.Release(c); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_FRAMEBUFFER_SRGB);
}
bool VRSystem::BindUIFramebuffer() {
    auto& d = *m_Impl;
    if (!d.frame || !d.render || !d.Acquire(d.ui)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, d.ui.images[d.ui.index].fbo);
    glViewport(0, 0, kUIWidth, kUIHeight);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    return true;
}
void VRSystem::UnbindUIFramebuffer() {
    if (m_Impl->cinema && m_Impl->ui.waited) {
        int width=0,height=0;SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(),&width,&height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,m_Impl->ui.images[m_Impl->ui.index].fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        glBlitFramebuffer(0,0,kUIWidth,kUIHeight,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    }
    m_Impl->Release(m_Impl->ui); m_Impl->uiReady = m_Impl->ui.submitted;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void VRSystem::EndFrame() {
    auto& d = *m_Impl; if (!d.frame) return;
    for (auto& c : d.eyes) d.Release(c);
    d.Release(d.ui);
    XrCompositionLayerProjectionView views[2]{};
    for (int eye = 0; eye < 2; ++eye) {
        views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        views[eye].pose = d.views[eye].pose; views[eye].fov = d.views[eye].fov;
        views[eye].subImage.swapchain = d.eyes[eye].handle;
        views[eye].subImage.imageRect.extent = {(int32_t)d.eyes[eye].width, (int32_t)d.eyes[eye].height};
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space = d.space; projection.viewCount = 2; projection.views = views;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.space = d.space; quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    d.UpdateUIPose();
    glm::quat rotation = glm::quat_cast(glm::mat3(d.uiPose));
    quad.pose.orientation = {rotation.x, rotation.y, rotation.z, rotation.w};
    quad.pose.position = {d.uiPose[3].x, d.uiPose[3].y, d.uiPose[3].z};
    quad.size = {3.5f, 3.5f / d.uiAspect};
    quad.subImage.swapchain = d.ui.handle; quad.subImage.imageRect.extent = {(int32_t)kUIWidth, (int32_t)kUIHeight};
    const XrCompositionLayerBaseHeader* layers[2]; uint32_t count = 0;
    if (d.render && !d.cinema && d.eyes[0].submitted && d.eyes[1].submitted)
        layers[count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    if (d.render && d.uiReady) layers[count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = d.time; end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = count; end.layers = layers;
    d.Check(xrEndFrame(d.session, &end), "end frame"); d.frame = false;
}

bool VRSystem::IsActive() const { return m_Impl->running; }
bool VRSystem::IsAvailable() const { return m_Impl->session != XR_NULL_HANDLE; }
bool VRSystem::IsFocused() const { return m_Impl->running && m_Impl->state == XR_SESSION_STATE_FOCUSED; }
bool VRSystem::HasTracking() const { return IsActive() && m_Impl->valid; }
uint32_t VRSystem::GetEyeWidth() const { return m_Impl->eyes[0].width; }
uint32_t VRSystem::GetEyeHeight() const { return m_Impl->eyes[0].height; }
glm::mat4 VRSystem::GetEyeViewMatrix(int eye) const { return glm::inverse(m_Impl->origin * XrPoseToMat4(m_Impl->views[eye].pose)); }
glm::mat4 VRSystem::GetEyeProjectionMatrix(int eye, float nearZ, float farZ) const { return XrFovToProjection(m_Impl->views[eye].fov, nearZ, farZ); }
glm::vec3 VRSystem::GetHMDPosition() const { return glm::vec3(m_Impl->head[3]); }
glm::mat4 VRSystem::GetHeadPose() const { return m_Impl->head; }
glm::mat4 VRSystem::GetWorldHeadPose() const { return m_Impl->origin * m_Impl->head; }
void VRSystem::SetTrackingOrigin(const glm::mat4& origin) { m_Impl->origin = origin; }
glm::mat4 VRSystem::GetTrackingOrigin() const { return m_Impl->origin; }
uint32_t VRSystem::GetReferenceSpaceRevision() const { return m_Impl->revision; }
void VRSystem::SetCinemaMode(bool enabled) {
    if (!m_Impl->cinema && enabled) m_Impl->uiPoseValid = false;
    m_Impl->cinema = enabled;
}
bool VRSystem::IsCinemaMode() const { return m_Impl->cinema; }
void VRSystem::SetUIInteractive(bool enabled) {
    if (!m_Impl->interactive && enabled) m_Impl->uiPoseValid = false;
    m_Impl->interactive = enabled;
}
bool VRSystem::IsUIInteractive() const { return m_Impl->interactive; }
void VRSystem::RecenterUI() { if (m_Impl) m_Impl->uiPoseValid = false; }
void VRSystem::SetQuality(int quality) { m_Impl->quality = glm::clamp(quality,0,2); }
int VRSystem::GetQuality() const { return m_Impl->quality; }
float VRSystem::GetRenderScale() const { const float scales[]={.8f,1.0f,1.15f};return scales[m_Impl->quality]; }
void VRSystem::Pulse(int hand, float amplitude, float seconds) {
    auto& d = *m_Impl;
    if (!IsFocused() || !d.haptic || hand < 0 || hand > 1 || !d.hands[hand].Active) return;
    XrHapticActionInfo action{XR_TYPE_HAPTIC_ACTION_INFO};
    action.action = d.haptic; action.subactionPath = d.paths[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = glm::clamp(amplitude, 0.0f, 1.0f);
    vibration.duration = (XrDuration)(glm::clamp(seconds, 0.005f, 0.15f) * 1e9);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(d.session, &action, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}
void VRSystem::SetUIAspect(float aspect) { m_Impl->uiAspect = glm::clamp(aspect, 0.5f, 4.0f); }
const VRSystem::HandState& VRSystem::GetHandState(int hand) const { return m_Impl->hands[hand == 1 ? 1 : 0]; }
bool VRSystem::GetUIPointer(glm::vec2& uv) const {
    m_Impl->UpdateUIPose();
    const auto& d = *m_Impl;
    if (!HasTracking() || !IsFocused()) return false;
    const auto& hand = d.hands[1].Active ? d.hands[1] : d.hands[0];
    if (!hand.Active) return false;
    glm::mat4 toQuad = glm::inverse(d.uiPose) * hand.AimPose;
    glm::vec3 origin(toQuad[3]), direction = -glm::vec3(toQuad[2]);
    if (direction.z >= -0.001f) return false;
    float t = -origin.z / direction.z;
    if (t < 0.0f) return false;
    glm::vec3 point = origin + direction * t;
    uv = {0.5f + point.x / 3.5f, 0.5f - point.y / (3.5f / d.uiAspect)};
    return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
}
void VRSystem::Shutdown() {
    if (m_Impl) {
        m_Impl->worldUI.reset(); m_Impl->worldShader.reset();
        if (m_Impl->worldVAO) glDeleteVertexArrays(1, &m_Impl->worldVAO);
        if (m_Impl->worldVBO) glDeleteBuffers(1, &m_Impl->worldVBO);
        if (m_Impl->worldEBO) glDeleteBuffers(1, &m_Impl->worldEBO);
        m_Impl->worldVAO = m_Impl->worldVBO = m_Impl->worldEBO = 0;
    }
    if (!m_Impl) return;
    auto& d = *m_Impl;
    EndFrame();
    auto destroy = [&](VRSystemImpl::Chain& c) {
        for (auto& image : c.images) { if (image.fbo) glDeleteFramebuffers(1, &image.fbo); if (image.depth) glDeleteRenderbuffers(1, &image.depth); }
        if (c.handle) xrDestroySwapchain(c.handle);
    };
    for (auto& c : d.eyes) destroy(c);
    destroy(d.ui);
    for (int h = 0; h < 2; ++h) { if (d.grips[h]) xrDestroySpace(d.grips[h]); if (d.aims[h]) xrDestroySpace(d.aims[h]); }
    if (d.actions) xrDestroyActionSet(d.actions);
    if (d.space) xrDestroySpace(d.space);
    if (d.session) xrDestroySession(d.session);
    if (d.instance) xrDestroyInstance(d.instance);
    d = {};
    if (s_Instance == this) s_Instance = nullptr;
}
}
