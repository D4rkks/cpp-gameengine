#include "VRRigSystem.h"
#include "VRSystem.h"
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"

#include <SDL.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Engine {

static glm::vec3 PoseTranslation(const glm::mat4 &m) {
    return glm::vec3(m[3]);
}

static glm::vec3 PoseEuler(const glm::mat4 &m) {
    glm::quat q = glm::quat_cast(glm::mat3(m));
    return glm::degrees(glm::eulerAngles(q));
}

void VRRigSystem::OnRuntimeUpdate(Scene &scene, float ts) {
    VRSystem *vr = VRSystem::Get();

    for (auto &entity : scene.GetEntities()) {
        if (!entity.HasVRRig) continue;
        auto &rig = entity.VRRig;

        if (vr && vr->IsActive()) {

            glm::vec3 hmdPos = vr->GetHMDPosition();
            entity.Transform.Translation = hmdPos;

            const auto &left  = vr->GetHandState(0);
            const auto &right = vr->GetHandState(1);

            if (rig.Locomotion == VRRigComponent::LocomotionMode::SmoothMove) {
                if (glm::length(left.Thumbstick) > 0.1f) {
                    glm::mat4 eyeView = vr->GetEyeViewMatrix(0);
                    glm::vec3 forward = -glm::normalize(glm::vec3(eyeView[0][2], 0.0f, eyeView[2][2]));
                    glm::vec3 right3  =  glm::normalize(glm::vec3(eyeView[0][0], 0.0f, eyeView[2][0]));
                    if (glm::length(forward) < 0.001f) forward = {0, 0, -1};
                    if (glm::length(right3)  < 0.001f) right3  = {1, 0,  0};

                    glm::vec3 move = (forward * left.Thumbstick.y +
                                      right3  * left.Thumbstick.x) * rig.MoveSpeed * ts;
                    entity.Transform.Translation += move;
                }
            }

            m_SnapCooldown -= ts;
            if (m_SnapCooldown <= 0.0f && std::abs(right.Thumbstick.x) > 0.7f) {
                float sign = right.Thumbstick.x > 0.0f ? 1.0f : -1.0f;
                entity.Transform.Rotation.y -= glm::radians(sign * rig.SnapAngleDeg);
                m_SnapCooldown = 0.3f;
            }

            if (rig.ShowControllerMeshes)
                UpdateHandEntities(scene, entity.GetID());

        } else if (rig.PreviewMode) {

            if (!m_PreviewInitialized) {
                m_PreviewPos         = entity.Transform.Translation;
                m_PreviewYaw         = glm::degrees(entity.Transform.Rotation.y);
                m_PreviewPitch       = glm::degrees(entity.Transform.Rotation.x);
                m_PreviewInitialized = true;
            }
            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
            int mx = 0, my = 0;
            Uint32 mouseBtn = SDL_GetRelativeMouseState(&mx, &my);

            if (mouseBtn & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
                m_PreviewYaw   -= mx * 0.15f;
                m_PreviewPitch -= my * 0.15f;
                m_PreviewPitch  = glm::clamp(m_PreviewPitch, -80.0f, 80.0f);
            }

            float rad = glm::radians(m_PreviewYaw);
            glm::vec3 fwd = {-std::sin(rad), 0.0f, -std::cos(rad)};
            glm::vec3 rt  = { std::cos(rad), 0.0f, -std::sin(rad)};

            float speed = rig.MoveSpeed;
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    m_PreviewPos += fwd * speed * ts;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])   m_PreviewPos -= fwd * speed * ts;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])   m_PreviewPos -= rt  * speed * ts;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])  m_PreviewPos += rt  * speed * ts;
            if (keys[SDL_SCANCODE_E]) m_PreviewPos.y += speed * ts;
            if (keys[SDL_SCANCODE_Q]) m_PreviewPos.y -= speed * ts;

            entity.Transform.Translation = m_PreviewPos;
            entity.Transform.Rotation.x  = glm::radians(m_PreviewPitch);
            entity.Transform.Rotation.y  = glm::radians(m_PreviewYaw);
        }

        scene.UpdateEntity(entity);
        break;
    }
}

void VRRigSystem::OnEditorUpdate(Scene &scene, float ts) {

}

void VRRigSystem::UpdateHandEntities(Scene &scene, int rigEntityID) {
    VRSystem *vr = VRSystem::Get();
    if (!vr || !vr->IsActive()) return;

    const char *handNames[2] = {"LeftHand", "RightHand"};
    for (int h = 0; h < 2; h++) {
        const auto &state = vr->GetHandState(h);
        if (!state.Active) continue;

        Entity *handEntity = nullptr;
        for (auto &e : scene.GetEntities()) {
            if (e.Name == handNames[h] &&
                e.HasRelationship &&
                (int)e.Relationship.Parent != -1) {

                const Entity *parent = scene.GetEntityByUUIDPtr(e.Relationship.Parent);
                if (parent && parent->GetID() == rigEntityID) {
                    handEntity = &e;
                    break;
                }
            }
        }

        if (!handEntity) continue;

        handEntity->Transform.Translation = PoseTranslation(state.GripPose);
        handEntity->Transform.Rotation    = PoseEuler(state.GripPose);
        scene.UpdateEntity(*handEntity);
    }
}

}
