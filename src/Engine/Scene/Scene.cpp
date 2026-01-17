#include "Scene.h"
#include "../Audio/AudioEngine.h"
#include "../Renderer/Renderer.h"
#include "../Scripting/ScriptRegistry.h"
#include <algorithm>
#include <btBulletDynamicsCommon.h>
#include <filesystem>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

#include "../Core/Application.h"
#include "../Core/Input.h"
#include "../Renderer/Font.h"
#include "../Renderer/Renderer2D.h"
#include "Components.h"
#include "Entity.h"

#include <glm/glm.hpp>

namespace Engine {

Scene::Scene() {
  m_DefaultFont = std::make_shared<Font>("assets/fonts/arial.ttf", 96.0f);
}

Scene::~Scene() {
  OnPhysicsStop();
  delete (btDiscreteDynamicsWorld *)m_PhysicsWorld;
  delete (btSequentialImpulseConstraintSolver *)m_Solver;
  delete (btDbvtBroadphase *)m_Broadphase;
  delete (btCollisionDispatcher *)m_Dispatcher;
  delete (btDefaultCollisionConfiguration *)m_CollisionConfiguration;
}

Entity Scene::CreateEntity(const std::string &name) {
  static int s_EntityIDCount = 0;
  m_Entities.emplace_back(s_EntityIDCount++, name);
  return m_Entities.back();
}

void Scene::DestroyEntity(Entity entity) {
  auto it = std::remove_if(
      m_Entities.begin(), m_Entities.end(),
      [&](const Entity &e) { return e.GetID() == entity.GetID(); });
  if (it != m_Entities.end())
    m_Entities.erase(it, m_Entities.end());
}

void Scene::Clear() { m_Entities.clear(); }

void Scene::OnUpdate(float ts) {
  // Update Scripts
  for (auto &entity : m_Entities) {
    if (entity.HasScript) {
      if (entity.m_NativeScript.Instance) {
        entity.m_NativeScript.Instance->m_Entity = &entity;
        entity.m_NativeScript.Instance->OnUpdate(ts);
      } else if (entity.m_NativeScript.InstantiateScript) {
        entity.m_NativeScript.Instance =
            entity.m_NativeScript.InstantiateScript();
        entity.m_NativeScript.Instance->m_Entity = &entity;
        entity.m_NativeScript.Instance->OnCreate();

        entity.m_NativeScript.Instance->OnUpdate(ts);
      }
    }
  }

  if (m_PhysicsWorld) {
    ((btDiscreteDynamicsWorld *)m_PhysicsWorld)->stepSimulation(ts, 10);

    {
      auto primaryCameraEntity = GetPrimaryCameraEntity();
      if (primaryCameraEntity) {
        // Extract vectors from Transform (Quaternions handling)
        const auto &trans = primaryCameraEntity->Transform;
        glm::mat4 transform =
            glm::translate(glm::mat4(1.0f), trans.Translation) *
            glm::toMat4(glm::quat(trans.Rotation)) *
            glm::scale(glm::mat4(1.0f), trans.Scale);

        glm::vec3 pos = trans.Translation;
        glm::vec3 up = glm::vec3(transform[1]);
        glm::vec3 forward = -glm::vec3(transform[2]);

        AudioEngine::SetListenerTransform(pos, forward, up);
      }
    }
    for (auto &entity : m_Entities) {
      if (entity.HasAudioSource) {
        auto &audio = entity.AudioSource;
        if (audio.PlayOnAwake && !audio.IsPlaying && !audio.FilePath.empty()) {
          std::cout << "[Scene] Found AudioSource on Entity. Path: "
                    << audio.FilePath << " PlayOnAwake: " << audio.PlayOnAwake
                    << std::endl;
          AudioEngine::PlaySound(audio.FilePath, audio.Volume, audio.Loop,
                                 audio.Spatial, audio.Range,
                                 entity.Transform.Translation);
          audio.IsPlaying = true;
        } else if (audio.IsPlaying) {
          AudioEngine::SetVoicePosition(audio.FilePath,
                                        entity.Transform.Translation);
        }
      }
    }
    AudioEngine::Update(ts);

    for (auto &entity : m_Entities) {
      if (entity.HasRigidBody && entity.RigidBody.RuntimeBody) {
        btRigidBody *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
        if (body && body->getMotionState()) {
          btTransform trans;
          body->getMotionState()->getWorldTransform(trans);

          btVector3 origin = trans.getOrigin();
          entity.Transform.Translation = {origin.getX(), origin.getY(),
                                          origin.getZ()};

          if (!entity.RigidBody.FixedRotation) {
            btQuaternion rot = trans.getRotation();
            glm::quat q(rot.w(), rot.x(), rot.y(), rot.z());
            entity.Transform.Rotation = glm::eulerAngles(q);
          }
        } else {
          btTransform trans = body->getWorldTransform();
          btVector3 origin = trans.getOrigin();
          entity.Transform.Translation = {origin.getX(), origin.getY(),
                                          origin.getZ()};
        }
      }
    }
  }

  if (!m_SkyTexturePath.empty()) {
    if (!m_SkyTexture || m_SkyTexture->GetPath() != m_SkyTexturePath) {
      std::string path = m_SkyTexturePath;
      if (!std::filesystem::exists(path)) {
        if (std::filesystem::exists("../" + path))
          path = "../" + path;
      }
      std::cout << "[Scene] [Runtime] Attempting to load sky texture: " << path
                << std::endl;
      m_SkyTexture = std::make_shared<Texture2D>(path);

      if (m_SkyTexture->GetID() == 0) {
        std::cout << "[Scene] [Runtime] FAILED to load sky texture: " << path
                  << std::endl;
        m_SkyTexture = nullptr;
      } else {
        std::cout << "[Scene] [Runtime] Successfully loaded sky texture: "
                  << path << " (ID: " << m_SkyTexture->GetID() << ")"
                  << std::endl;
      }
    }
  } else {
    m_SkyTexture = nullptr;
  }
}

void Scene::OnUpdateEditor(float ts) {
  if (!m_SkyTexturePath.empty()) {
    if (!m_SkyTexture || m_SkyTexture->GetPath() != m_SkyTexturePath) {
      std::string path = m_SkyTexturePath;
      if (!std::filesystem::exists(path)) {
        if (std::filesystem::exists("../" + path))
          path = "../" + path;
      }
      std::cout << "[Scene] [Editor] Attempting to load sky texture: " << path
                << std::endl;
      m_SkyTexture = std::make_shared<Texture2D>(path);

      if (m_SkyTexture->GetID() == 0) {
        std::cout << "[Scene] [Editor] FAILED to load sky texture: " << path
                  << std::endl;
        m_SkyTexture = nullptr;
      } else {
        std::cout << "[Scene] [Editor] Successfully loaded sky texture: "
                  << path << " (ID: " << m_SkyTexture->GetID() << ")"
                  << std::endl;
      }
    }
  } else {
    m_SkyTexture = nullptr;
  }
}

void Scene::OnImGuiRender() {
  for (auto &entity : m_Entities) {
    if (entity.HasScript && entity.m_NativeScript.Instance) {
      entity.m_NativeScript.Instance->OnImGuiRender();
    }
  }
}

Entity Scene::GetEntityByUUID(UUID uuid) {
  for (auto &entity : m_Entities) {
    if (entity.GetUUID() == uuid)
      return entity;
  }
  return Entity();
}

Entity *FindEntityByUUID(std::vector<Entity> &entities, UUID uuid) {
  for (auto &entity : entities) {
    if (entity.GetUUID() == uuid)
      return &entity;
  }
  return nullptr;
}

struct UIRect {
  glm::vec2 Position;
  glm::vec2 Size;
};

UIRect CalculateGlobalRect(const RectTransformComponent &rt,
                           const UIRect &parentRect) {
  UIRect rect;

  glm::vec2 parentMin = parentRect.Position;
  glm::vec2 parentMax = parentRect.Position + parentRect.Size;

  glm::vec2 anchorMinPoint = parentMin + rt.AnchorsMin * parentRect.Size;
  glm::vec2 anchorMaxPoint = parentMin + rt.AnchorsMax * parentRect.Size;

  glm::vec2 anchorCenter = (anchorMinPoint + anchorMaxPoint) * 0.5f;

  glm::vec2 baseSize = anchorMaxPoint - anchorMinPoint;
  rect.Size = baseSize + rt.SizeDelta;

  glm::vec2 pivotOffset = (rt.Pivot - glm::vec2(0.5f)) * rect.Size;
  glm::vec2 posWithoutPivot = anchorCenter + rt.Position;

  rect.Position = posWithoutPivot - rect.Size * 0.5f - pivotOffset;

  return rect;
}

void RenderUIHierarchy(Scene *scene, Entity *entity, const UIRect &parentRect,
                       const glm::vec2 &mousePos) {
  if (!entity || !entity->HasRectTransform)
    return;

  UIRect currentRect = CalculateGlobalRect(entity->RectTransform, parentRect);

  bool isHovered = (mousePos.x >= currentRect.Position.x &&
                    mousePos.x <= currentRect.Position.x + currentRect.Size.x &&
                    mousePos.y >= currentRect.Position.y &&
                    mousePos.y <= currentRect.Position.y + currentRect.Size.y);

  if (entity->HasImage && entity->Image.Visible) {
    auto &img = entity->Image;
    if (img.Texture) {
      Renderer2D::DrawQuad(currentRect.Position + currentRect.Size * 0.5f,
                           currentRect.Size, img.Texture,
                           img.Color *
                               (isHovered ? glm::vec4(1.2f) : glm::vec4(1.0f)));
    } else {
      Renderer2D::DrawQuad(
          currentRect.Position + currentRect.Size * 0.5f, currentRect.Size,
          img.Color * (isHovered ? glm::vec4(1.2f) : glm::vec4(1.0f)));
    }
  }

  if (entity->HasButton) {
    if (Input::IsMouseButtonPressed(0) && isHovered) {
    }
  }

  if (entity->HasText) {
    auto &txt = entity->Text;

    glm::vec3 textPos = {currentRect.Position.x + 5.0f,
                         currentRect.Position.y + currentRect.Size.y * 0.5f -
                             txt.FontSize * 0.3f,
                         0.1f};

    Renderer2D::DrawString(txt.TextString, scene->m_DefaultFont, textPos,
                           txt.Color, txt.FontSize);
  }

  if (entity->HasRelationship) {
    std::vector<UUID> children = entity->Relationship.Children;
    for (auto &childUUID : children) {
      Entity child = scene->GetEntityByUUID(childUUID);
      if (child.GetID() != -1) {
        RenderUIHierarchy(scene, &child, currentRect, mousePos);
      }
    }
  }
}

void Scene::OnUIRender(float width, float height,
                       const glm::vec2 &viewportOffset) {
  if (width == 0 || height == 0)
    return;

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glm::mat4 projection = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
  Renderer2D::BeginScene(projection);

  UIRect screenRect;
  screenRect.Position = {0.0f, 0.0f};
  screenRect.Size = {width, height};

  glm::vec2 mousePos = Input::GetMousePosition() - viewportOffset;

  for (auto &entity : m_Entities) {
    if (entity.IsUIElement()) {
      if (!entity.HasRelationship || entity.Relationship.Parent == 0) {
        if (entity.HasCanvas && !entity.Canvas.Enabled)
          continue;

        RenderUIHierarchy(this, &entity, screenRect, mousePos);
      }
    }
  }

  Renderer2D::EndScene();
  glEnable(GL_DEPTH_TEST);
}

void Scene::OnPhysicsStart() {
  m_CollisionConfiguration = new btDefaultCollisionConfiguration();
  m_Dispatcher = new btCollisionDispatcher(
      (btDefaultCollisionConfiguration *)m_CollisionConfiguration);
  m_Broadphase = new btDbvtBroadphase();
  m_Solver = new btSequentialImpulseConstraintSolver();
  m_PhysicsWorld = new btDiscreteDynamicsWorld(
      (btDispatcher *)m_Dispatcher, (btBroadphaseInterface *)m_Broadphase,
      (btConstraintSolver *)m_Solver,
      (btCollisionConfiguration *)m_CollisionConfiguration);
  ((btDiscreteDynamicsWorld *)m_PhysicsWorld)
      ->setGravity(btVector3(0, -25.0f, 0));

  for (auto &entity : m_Entities) {
    if (entity.HasRigidBody && entity.HasBoxCollider) {
      glm::vec3 size = entity.BoxCollider.Size * entity.Transform.Scale;
      btCollisionShape *shape = new btBoxShape(
          btVector3(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f));
      btTransform startTransform;
      startTransform.setIdentity();
      startTransform.setOrigin(btVector3(entity.Transform.Translation.x,
                                         entity.Transform.Translation.y,
                                         entity.Transform.Translation.z));

      glm::quat q(entity.Transform.Rotation);
      startTransform.setRotation(btQuaternion(q.x, q.y, q.z, q.w));

      btScalar mass =
          (entity.RigidBody.Type == RigidBodyComponent::BodyType::Dynamic)
              ? entity.RigidBody.Mass
              : 0.0f;
      btVector3 localInertia(0, 0, 0);
      if (mass != 0.0f)
        shape->calculateLocalInertia(mass, localInertia);

      btDefaultMotionState *myMotionState =
          new btDefaultMotionState(startTransform);
      btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState,
                                                      shape, localInertia);
      btRigidBody *body = new btRigidBody(rbInfo);
      if (entity.RigidBody.FixedRotation) {
        body->setAngularFactor(btVector3(0, 0, 0));
      }

      entity.RigidBody.RuntimeBody = body;

      ((btDiscreteDynamicsWorld *)m_PhysicsWorld)->addRigidBody(body);
    }
  }
}

void Scene::OnPhysicsStop() {
  if (m_PhysicsWorld) {
    delete (btDiscreteDynamicsWorld *)m_PhysicsWorld;
    delete (btSequentialImpulseConstraintSolver *)m_Solver;
    delete (btDbvtBroadphase *)m_Broadphase;
    delete (btCollisionDispatcher *)m_Dispatcher;
    delete (btDefaultCollisionConfiguration *)m_CollisionConfiguration;
    m_PhysicsWorld = nullptr;
  }
}

void ScriptableEntity::ApplyForce(const glm::vec3 &force) {
  if (m_Entity && m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    body->activate(true);
    body->applyCentralForce(btVector3(force.x, force.y, force.z));
  }
}

void ScriptableEntity::SetLinearVelocity(const glm::vec3 &velocity) {
  if (m_Entity && m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    body->activate(true);
    body->setLinearVelocity(btVector3(velocity.x, velocity.y, velocity.z));
  }
}

Entity *Scene::GetPrimaryCameraEntity() {
  for (auto &entity : m_Entities) {
    if (entity.HasCamera && entity.Camera.Primary)
      return &entity;
  }
  return nullptr;
}

glm::vec3 ScriptableEntity::GetLinearVelocity() {
  if (m_Entity && m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    const btVector3 &vel = body->getLinearVelocity();
    return {vel.x(), vel.y(), vel.z()};
  }
  return {0.0f, 0.0f, 0.0f};
}

} // namespace Engine
