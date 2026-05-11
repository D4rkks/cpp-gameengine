#pragma once

#include "../Scene/SceneSystem.h"
#include <glm/glm.hpp>

namespace Engine {

class VRRigSystem : public SceneSystem {
public:
    void OnRuntimeUpdate(Scene &scene, float ts) override;
    void OnEditorUpdate(Scene &scene, float ts)  override;

private:
    float     m_SnapCooldown       = 0.0f;
    glm::vec3 m_PreviewPos         = {0.0f, 0.0f,  0.0f};
    float     m_PreviewYaw         = 0.0f;
    float     m_PreviewPitch       = 0.0f;
    bool      m_PreviewInitialized = false;

    void UpdateHandEntities(Scene &scene, int rigEntityID);
};

}
