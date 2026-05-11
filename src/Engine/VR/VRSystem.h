#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

struct SDL_Window;
typedef void* SDL_GLContext;

namespace Engine {

struct VRSystemImpl;

class VRSystem {
public:
    VRSystem();
    ~VRSystem();

    bool Init(SDL_Window* window, SDL_GLContext ctx);
    void Shutdown();

    bool IsActive() const;

    bool BeginFrame();

    void EndFrame();

    int      GetEyeCount()  const { return IsActive() ? 2 : 1; }
    uint32_t GetEyeWidth()  const;
    uint32_t GetEyeHeight() const;

    void BindEyeFramebuffer(int eye);
    void UnbindEyeFramebuffer(int eye);

    glm::mat4 GetEyeViewMatrix(int eye)                          const;
    glm::mat4 GetEyeProjectionMatrix(int eye, float near, float far) const;

    glm::vec3 GetHMDPosition() const;

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
