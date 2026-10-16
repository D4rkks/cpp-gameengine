#pragma once

#include "../Core/EngineAPI.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

struct SDL_Window;
typedef void* SDL_GLContext;
struct ImDrawList;

namespace Engine {

struct VRSystemImpl;

class ENGINE_API VRSystem {
public:
    VRSystem();
    ~VRSystem();

    bool Init(SDL_Window* window, SDL_GLContext ctx);
    void Shutdown();

    bool IsActive() const;
    bool IsAvailable() const;
    bool IsFocused() const;
    bool HasTracking() const;

    bool BeginFrame();
    // Refresh predicted poses after simulation, without resampling buttons.
    void RefreshTracking();

    void EndFrame();

    int      GetEyeCount()  const { return IsActive() ? 2 : 1; }
    uint32_t GetEyeWidth()  const;
    uint32_t GetEyeHeight() const;

    bool BindEyeFramebuffer(int eye);
    void UnbindEyeFramebuffer(int eye);

    glm::mat4 GetEyeViewMatrix(int eye)                          const;
    glm::mat4 GetEyeProjectionMatrix(int eye, float near, float far) const;

    glm::vec3 GetHMDPosition() const;
    glm::mat4 GetHeadPose() const; // Raw OpenXR tracking space, in metres.
    void SetTrackingOrigin(const glm::mat4& origin);
    glm::mat4 GetTrackingOrigin() const;
    glm::mat4 GetWorldHeadPose() const;
    uint32_t GetReferenceSpaceRevision() const;

    // Flat menus/build screens share the desktop image; gameplay is stereo.
    void SetCinemaMode(bool enabled);
    bool IsCinemaMode() const;
    void SetUIInteractive(bool enabled);
    bool IsUIInteractive() const;
    bool GetUIPointer(glm::vec2& uv) const;
    void RecenterUI();
    void Pulse(int hand, float amplitude = 0.3f, float seconds = 0.035f);
    void SetQuality(int quality);
    int GetQuality() const;
    float GetRenderScale() const;
    void SetUIAspect(float aspect);
    bool BindUIFramebuffer();
    void UnbindUIFramebuffer();
    enum : uint32_t { kUIWidth = 2048, kUIHeight = 1152 };

    // Pixel artwork rendered as depth-tested billboards in the stereo scene.
    void BeginWorldUIFrame();
    ImDrawList* GetWorldUIDrawList();
    void SetWorldUIAnchor(const glm::vec3& position, const glm::vec2& pixelOrigin, float metersPerPixel);
    void SetWorldUITransform(const glm::mat4& pixelToWorld);
    void RenderWorldUI(const glm::mat4& viewProjection);

    struct HandState {
        bool      Active      = false;
        glm::mat4 GripPose    = glm::mat4(1.0f);
        glm::mat4 AimPose     = glm::mat4(1.0f);
        float     Trigger     = 0.0f;
        float     Squeeze     = 0.0f;
        glm::vec2 Thumbstick  = {0.0f, 0.0f};
        bool      PrimaryBtn  = false;
        bool      SecondaryBtn = false;
    };

    const HandState& GetHandState(int hand) const;

    static VRSystem* Get() { return s_Instance; }

private:
    std::unique_ptr<VRSystemImpl> m_Impl;
    static VRSystem* s_Instance;
};

}
