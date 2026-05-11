#pragma once

#include "../Scene/ScriptableEntity.h"
#include "../Core/Application.h"
#include "../VR/VRSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

class VRPlayerController : public ScriptableEntity {
public:
    float MoveSpeed   = 3.0f;
    float SnapAngle   = 45.0f;

    void OnUpdate(float ts) override {
        VRSystem* vr = VRSystem::Get();
        if (!vr || !vr->IsActive()) return;

        const auto& left  = vr->GetHandState(0);
        const auto& right = vr->GetHandState(1);

        if (glm::length(left.Thumbstick) > 0.1f) {

            glm::vec3 hmdPos   = vr->GetHMDPosition();

            glm::mat4 eyeView  = vr->GetEyeViewMatrix(0);
            glm::vec3 forward  = -glm::normalize(glm::vec3(eyeView[0][2],
                                                            0.0f,
                                                            eyeView[2][2]));
            glm::vec3 right3   =  glm::normalize(glm::vec3(eyeView[0][0],
                                                            0.0f,
                                                            eyeView[2][0]));
            if (glm::length(forward) < 0.001f) forward = {0, 0, -1};
            if (glm::length(right3)  < 0.001f) right3  = {1, 0,  0};

            glm::vec3 move = (forward * left.Thumbstick.y +
                              right3  * left.Thumbstick.x) * MoveSpeed * ts;

            auto& t = GetComponent<TransformComponent>();
            t.Translation += move;
        }

        {
            static float s_SnapCooldown = 0.0f;
            s_SnapCooldown -= ts;
            if (s_SnapCooldown <= 0.0f && std::abs(right.Thumbstick.x) > 0.7f) {
                float sign = right.Thumbstick.x > 0.0f ? 1.0f : -1.0f;
                auto& t = GetComponent<TransformComponent>();
                t.Rotation.y -= sign * SnapAngle;
                s_SnapCooldown = 0.3f;
            }
        }

        if (left.Trigger > 0.9f)  OnInteract();
        if (right.Trigger > 0.9f) OnFire();
    }

    virtual void OnInteract() {}
    virtual void OnFire()     {}
};

}
