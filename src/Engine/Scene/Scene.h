#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include "../Core/EngineAPI.h"
#include "Entity.h"
#include "SceneSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine {

struct RaycastHit {
  glm::vec3 Point = {0.0f, 0.0f, 0.0f};
  glm::vec3 Normal = {0.0f, 1.0f, 0.0f};
  glm::vec3 Velocity = {0.0f, 0.0f, 0.0f};  // of the hit body at Point
  float Distance = 0.0f;
  int EntityID = -1;
};

class ENGINE_API Scene {
public:
  Scene();
  ~Scene();

  // includeGhosts = false skips bodies made non-solid with SetEntitySolid.
  bool Raycast(const glm::vec3 &from, const glm::vec3 &to, RaycastHit &outHit,
               int ignoreEntityID = -1, bool includeGhosts = true) const;

  // Move / push any entity from a script, keeping its rigid body (if it has
  // one) in step. Rotation is Euler radians, like TransformComponent.
  void SetEntityPose(Entity &entity, const glm::vec3 &position,
                     const glm::vec3 &rotation);
  void SetEntityVelocity(Entity &entity, const glm::vec3 &linear,
                         const glm::vec3 &angular = {0.0f, 0.0f, 0.0f});
  glm::vec3 GetEntityVelocity(const Entity &entity) const;
  // false turns the body into a ghost: still hit by raycasts, but it neither
  // pushes nor is pushed (for objects a script is carrying around).
  void SetEntitySolid(Entity &entity, bool solid);
  // Resizes the entity's collider relative to the size it was created with
  // (for an object whose shape changes while the game runs).
  void SetEntityColliderScale(Entity &entity, const glm::vec3 &scale);

  Entity CreateEntity(const std::string &name = "Entity");
  void DestroyEntity(Entity entity);
  void Clear();
  void OnUpdate(float ts);
  void OnUpdateEditor(float ts);
  void OnImGuiRender();
  void OnLateUpdate(float ts);
  void OnWorldUIRender();
  void OnUIRender(float width = 0.0f, float height = 0.0f,
                  const glm::vec2 &viewportOffset = {0.0f, 0.0f});

  Entity GetEntityByUUID(UUID uuid);
  Entity *GetEntityByUUIDPtr(UUID uuid);
  const Entity *GetEntityByUUIDPtr(UUID uuid) const;
  Entity *GetEntityByID(int id);
  const Entity *GetEntityByID(int id) const;

  bool SetParent(UUID childUUID, UUID parentUUID,
                 bool keepWorldTransform = true);
  bool SetParent(Entity child, Entity parent,
                 bool keepWorldTransform = true) {
    return SetParent(child.GetUUID(), parent.GetUUID(), keepWorldTransform);
  }
  bool ClearParent(UUID childUUID, bool keepWorldTransform = true) {
    return SetParent(childUUID, 0, keepWorldTransform);
  }
  bool WouldCreateCycle(UUID childUUID, UUID parentUUID) const;
  bool IsRootEntity(const Entity &entity) const;
  void NormalizeHierarchy();

  glm::mat4 GetLocalTransform(const Entity &entity) const;
  glm::mat4 GetWorldTransform(UUID uuid) const;
  glm::mat4 GetWorldTransform(const Entity &entity) const {
    glm::mat4 local = GetLocalTransform(entity);
    if (!entity.HasRelationship || entity.Relationship.Parent == 0) return local;
    return GetWorldTransform(entity.Relationship.Parent) * local;
  }
  void SetLocalTransformFromMatrix(Entity &entity, const glm::mat4 &matrix);

  template <typename T, typename... Args> T &RegisterSystem(Args &&...args) {
    static_assert(std::is_base_of_v<SceneSystem, T>,
                  "Scene systems must derive from SceneSystem");
    auto system = std::make_shared<T>(std::forward<Args>(args)...);
    T &ref = *system;
    m_Systems.push_back(std::move(system));
    return ref;
  }

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

  void *m_PhysicsWorld = nullptr;
  void *m_Broadphase = nullptr;
  void *m_CollisionConfiguration = nullptr;
  void *m_Dispatcher = nullptr;
  void *m_Solver = nullptr;

  std::vector<void *> m_CollisionShapes;
  std::vector<Entity> m_Entities;
  std::vector<std::shared_ptr<SceneSystem>> m_Systems;
  int m_EntityCount = 0;
  glm::vec3 m_Gravity = {0.0f, -9.81f, 0.0f};

  glm::vec3 m_SkyColor = {0.1f, 0.1f, 0.1f};
  glm::vec3 m_AmbientColor = {1.0f, 1.0f, 1.0f};
  float m_AmbientIntensity = 0.3f;

  std::string m_SkyTexturePath;
  std::shared_ptr<Texture2D> m_SkyTexture;
};

}
