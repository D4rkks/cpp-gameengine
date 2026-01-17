#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include "Entity.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class Scene {
public:
  Scene();
  ~Scene();

  Entity CreateEntity(const std::string &name = "Entity");
  void DestroyEntity(Entity entity);
  void Clear();
  void OnUpdate(float ts);
  void OnUpdateEditor(float ts);
  void OnImGuiRender();
  void OnUIRender(float width = 0.0f, float height = 0.0f,
                  const glm::vec2 &viewportOffset = {0.0f, 0.0f});

  Entity GetEntityByUUID(UUID uuid);

  const std::vector<Entity> &GetEntities() const { return m_Entities; }
  std::vector<Entity> &GetEntities() { return m_Entities; } 

  void UpdateEntity(const Entity &entity) {
    for (auto &e : m_Entities) {
      if (e.GetID() == entity.GetID()) {
        e = entity;
        return;
      }
    }
  }

  void OnPhysicsStart();
  void OnPhysicsStop();

  Entity *GetPrimaryCameraEntity();

  glm::vec3 GetGravity() const { return m_Gravity; }
  void SetGravity(const glm::vec3 &gravity) { m_Gravity = gravity; }

  glm::vec3 GetSkyColor() const { return m_SkyColor; }
  void SetSkyColor(const glm::vec3 &color) { m_SkyColor = color; }

  glm::vec3 GetAmbientColor() const { return m_AmbientColor; }
  void SetAmbientColor(const glm::vec3 &color) { m_AmbientColor = color; }

  float GetAmbientIntensity() const { return m_AmbientIntensity; }
  void SetAmbientIntensity(float intensity) { m_AmbientIntensity = intensity; }

  std::shared_ptr<class Font> m_DefaultFont;

  const std::string &GetSkyTexturePath() const { return m_SkyTexturePath; }
  void SetSkyTexturePath(const std::string &path) { m_SkyTexturePath = path; }

  std::shared_ptr<Texture2D> GetSkyTexture() const { return m_SkyTexture; }

private:
  // Physics
  void *m_PhysicsWorld = nullptr;
  void *m_Broadphase = nullptr;
  void *m_CollisionConfiguration = nullptr;
  void *m_Dispatcher = nullptr;
  void *m_Solver = nullptr;

  std::vector<void *> m_CollisionShapes; 
  std::vector<Entity> m_Entities;
  int m_EntityCount = 0;
  glm::vec3 m_Gravity = {0.0f, -9.81f, 0.0f};

  glm::vec3 m_SkyColor = {0.1f, 0.1f, 0.1f};
  glm::vec3 m_AmbientColor = {1.0f, 1.0f, 1.0f};
  float m_AmbientIntensity = 0.3f;

  std::string m_SkyTexturePath;
  std::shared_ptr<Texture2D> m_SkyTexture;
};

}
