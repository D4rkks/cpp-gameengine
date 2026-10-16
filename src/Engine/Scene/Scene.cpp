#include "Scene.h"
#include "../Audio/AudioEngine.h"
#include "../Renderer/Renderer.h"
#include <algorithm>
#include <btBulletDynamicsCommon.h>
#include <filesystem>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
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

namespace {
// A small body moving more than its own thickness in one step would pass clean
// through a wall or a table top. Past that speed Bullet sweeps a sphere along
// its path and stops it at the first thing in the way.
void UpdateContinuousCollision(btRigidBody *body) {
  btVector3 low, high;
  body->getCollisionShape()->getAabb(btTransform::getIdentity(), low, high);
  btVector3 half = (high - low) * btScalar(0.5);
  btScalar thinnest = std::min({half.x(), half.y(), half.z()});
  body->setCcdMotionThreshold(thinnest);
  body->setCcdSweptSphereRadius(thinnest * btScalar(0.8));
}
}

Scene::Scene() {
  m_DefaultFont = std::make_shared<Font>("assets/fonts/arial.ttf", 96.0f);
}

Scene::~Scene() {
  OnPhysicsStop();
}

Entity Scene::CreateEntity(const std::string &name) {
  static int s_EntityIDCount = 0;
  m_Entities.emplace_back(s_EntityIDCount++, name);
  return m_Entities.back();
}

void Scene::DestroyEntity(Entity entity) {
  Entity *storedEntity = GetEntityByID(entity.GetID());
  if (!storedEntity)
    return;

  std::vector<UUID> children;
  if (storedEntity->HasRelationship)
    children = storedEntity->Relationship.Children;

  for (UUID childUUID : children) {
    Entity *child = GetEntityByUUIDPtr(childUUID);
    if (child)
      DestroyEntity(*child);
  }

  UUID entityUUID = storedEntity->GetUUID();
  UUID parentUUID = storedEntity->HasRelationship
                        ? storedEntity->Relationship.Parent
                        : UUID(0);
  if (parentUUID != 0) {
    Entity *parent = GetEntityByUUIDPtr(parentUUID);
    if (parent && parent->HasRelationship) {
      auto &siblings = parent->Relationship.Children;
      siblings.erase(std::remove(siblings.begin(), siblings.end(), entityUUID),
                     siblings.end());
      parent->HasRelationship =
          parent->Relationship.Parent != 0 || !siblings.empty();
    }
  }

  auto it = std::remove_if(
      m_Entities.begin(), m_Entities.end(),
      [&](const Entity &e) { return e.GetID() == entity.GetID(); });
  if (it != m_Entities.end())
    m_Entities.erase(it, m_Entities.end());
}

void Scene::Clear() { m_Entities.clear(); }

void Scene::OnUpdate(float ts) {
  for (auto &system : m_Systems)
    system->OnRuntimeUpdate(*this, ts);

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

    float waterY = -1e9f;
    bool hasWater = false;
    for (const auto &e : m_Entities) {
      if (e.HasWater) {
        waterY = e.Transform.Translation.y;
        hasWater = true;
        break;
      }
    }
    if (hasWater) {
      auto *world = (btDiscreteDynamicsWorld *)m_PhysicsWorld;
      for (auto &entity : m_Entities) {
        if (!entity.HasRigidBody || entity.RigidBody.Type !=
            RigidBodyComponent::BodyType::Dynamic) continue;
        auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
        if (!body) continue;
        btVector3 p = body->getWorldTransform().getOrigin();
        float depth = waterY - p.y();
        if (depth > 0.0f) {
          float massBuoy = entity.RigidBody.Mass;

          float submerged = std::min(depth, 1.0f);
          float upForce = 35.0f * massBuoy * submerged;
          body->applyCentralForce(btVector3(0, upForce, 0));

          btVector3 v = body->getLinearVelocity();
          v *= 0.92f;
          body->setLinearVelocity(v);
          body->activate(true);
        }
      }
    }

    // The whole frame is simulated now, in equal slices of at most 1/120 s, so
    // bodies stay exactly in step with whatever scripts move every frame. (A
    // fixed step with interpolation leaves them a fraction of a step behind,
    // which shows as stutter next to anything script-driven.)
    auto *world = (btDiscreteDynamicsWorld *)m_PhysicsWorld;
    float frame = std::clamp((float)ts, 0.0f, 0.1f);
    int slices = std::clamp((int)std::ceil(frame * 120.0f), 1, 12);

    // Kinematic bodies are driven by their entity transform, not the solver.
    // They cross the frame's movement one slice at a time, which is what gives
    // them the right velocity when they push something.
    struct KinematicMove {
      btRigidBody *Body;
      btTransform From, To;
    };
    std::vector<KinematicMove> moves;
    for (auto &entity : m_Entities) {
      if (!entity.HasRigidBody || !entity.RigidBody.RuntimeBody ||
          entity.RigidBody.Type != RigidBodyComponent::BodyType::Kinematic)
        continue;
      auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
      if (!body->getMotionState())
        continue;
      btTransform trans;
      trans.setIdentity();
      trans.setOrigin(btVector3(entity.Transform.Translation.x,
                                entity.Transform.Translation.y,
                                entity.Transform.Translation.z));
      glm::quat q(entity.Transform.Rotation);
      trans.setRotation(btQuaternion(q.x, q.y, q.z, q.w));
      moves.push_back({body, body->getWorldTransform(), trans});
    }

    for (int slice = 1; slice <= slices; ++slice) {
      btScalar t = (btScalar)slice / (btScalar)slices;
      for (auto &move : moves) {
        btTransform now(
            move.From.getRotation().slerp(move.To.getRotation(), t),
            move.From.getOrigin().lerp(move.To.getOrigin(), t));
        move.Body->getMotionState()->setWorldTransform(now);
      }
      if (frame > 0.0f)
        world->stepSimulation(frame / (float)slices, 0);
    }

    {
      auto primaryCameraEntity = GetPrimaryCameraEntity();
      if (primaryCameraEntity) {
        glm::mat4 transform = GetWorldTransform(*primaryCameraEntity);

        glm::vec3 pos = glm::vec3(transform[3]);
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
                                 glm::vec3(GetWorldTransform(entity)[3]));
          audio.IsPlaying = true;
        } else if (audio.IsPlaying) {
          AudioEngine::SetVoicePosition(audio.FilePath,
                                        glm::vec3(GetWorldTransform(entity)[3]));
        }
      }
    }
    AudioEngine::Update(ts);

    for (auto &entity : m_Entities) {
      if (entity.HasRigidBody && entity.RigidBody.RuntimeBody) {
        if (entity.RigidBody.Type == RigidBodyComponent::BodyType::Kinematic)
          continue;
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
  for (auto &system : m_Systems)
    system->OnEditorUpdate(*this, ts);

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

void Scene::OnLateUpdate(float ts) {
  for (auto &entity : m_Entities)
    if (entity.HasScript && entity.m_NativeScript.Instance)
      entity.m_NativeScript.Instance->OnLateUpdate(ts);
}

void Scene::OnWorldUIRender() {
  for (auto &entity : m_Entities)
    if (entity.HasScript && entity.m_NativeScript.Instance)
      entity.m_NativeScript.Instance->OnWorldUIRender();
}

Entity Scene::GetEntityByUUID(UUID uuid) {
  for (auto &entity : m_Entities) {
    if (entity.GetUUID() == uuid)
      return entity;
  }
  return Entity();
}

Entity *Scene::GetEntityByUUIDPtr(UUID uuid) {
  for (auto &entity : m_Entities) {
    if (entity.GetUUID() == uuid)
      return &entity;
  }
  return nullptr;
}

const Entity *Scene::GetEntityByUUIDPtr(UUID uuid) const {
  for (const auto &entity : m_Entities) {
    if (entity.GetUUID() == uuid)
      return &entity;
  }
  return nullptr;
}

Entity *Scene::GetEntityByID(int id) {
  for (auto &entity : m_Entities) {
    if (entity.GetID() == id)
      return &entity;
  }
  return nullptr;
}

const Entity *Scene::GetEntityByID(int id) const {
  for (const auto &entity : m_Entities) {
    if (entity.GetID() == id)
      return &entity;
  }
  return nullptr;
}

glm::mat4 Scene::GetLocalTransform(const Entity &entity) const {
  return glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
         glm::toMat4(glm::quat(entity.Transform.Rotation)) *
         glm::scale(glm::mat4(1.0f), entity.Transform.Scale);
}

glm::mat4 Scene::GetWorldTransform(UUID uuid) const {
  const Entity *entity = GetEntityByUUIDPtr(uuid);
  if (!entity)
    return glm::mat4(1.0f);

  glm::mat4 local = GetLocalTransform(*entity);
  if (!entity->HasRelationship || entity->Relationship.Parent == 0)
    return local;

  const Entity *parent = GetEntityByUUIDPtr(entity->Relationship.Parent);
  if (!parent)
    return local;

  return GetWorldTransform(parent->GetUUID()) * local;
}

void Scene::SetLocalTransformFromMatrix(Entity &entity,
                                        const glm::mat4 &matrix) {
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::quat orientation;
  glm::decompose(matrix, entity.Transform.Scale, orientation,
                 entity.Transform.Translation, skew, perspective);
  entity.Transform.Rotation = glm::eulerAngles(glm::normalize(orientation));
}

bool Scene::WouldCreateCycle(UUID childUUID, UUID parentUUID) const {
  if (childUUID == 0 || parentUUID == 0)
    return false;
  if (childUUID == parentUUID)
    return true;

  UUID cursor = parentUUID;
  std::vector<UUID> visited;
  while (cursor != 0) {
    if (cursor == childUUID)
      return true;
    if (std::find(visited.begin(), visited.end(), cursor) != visited.end())
      return true;
    visited.push_back(cursor);

    const Entity *entity = GetEntityByUUIDPtr(cursor);
    if (!entity || !entity->HasRelationship)
      return false;
    cursor = entity->Relationship.Parent;
  }

  return false;
}

bool Scene::SetParent(UUID childUUID, UUID parentUUID,
                      bool keepWorldTransform) {
  Entity *child = GetEntityByUUIDPtr(childUUID);
  if (!child)
    return false;

  if (parentUUID != 0 && !GetEntityByUUIDPtr(parentUUID))
    return false;

  if (WouldCreateCycle(childUUID, parentUUID))
    return false;

  glm::mat4 worldTransform = GetWorldTransform(childUUID);

  UUID oldParentUUID =
      child->HasRelationship ? child->Relationship.Parent : UUID(0);
  if (oldParentUUID != 0) {
    Entity *oldParent = GetEntityByUUIDPtr(oldParentUUID);
    if (oldParent && oldParent->HasRelationship) {
      auto &children = oldParent->Relationship.Children;
      children.erase(std::remove(children.begin(), children.end(), childUUID),
                     children.end());
      oldParent->HasRelationship =
          oldParent->Relationship.Parent != 0 || !children.empty();
    }
  }

  child = GetEntityByUUIDPtr(childUUID);
  child->HasRelationship = true;
  child->Relationship.Parent = parentUUID;

  if (parentUUID != 0) {
    Entity *parent = GetEntityByUUIDPtr(parentUUID);
    parent->HasRelationship = true;
    auto &children = parent->Relationship.Children;
    if (std::find(children.begin(), children.end(), childUUID) ==
        children.end()) {
      children.push_back(childUUID);
    }
  }

  if (keepWorldTransform) {
    glm::mat4 localTransform = worldTransform;
    if (parentUUID != 0)
      localTransform = glm::inverse(GetWorldTransform(parentUUID)) *
                       worldTransform;

    child = GetEntityByUUIDPtr(childUUID);
    SetLocalTransformFromMatrix(*child, localTransform);
  }

  child = GetEntityByUUIDPtr(childUUID);
  child->HasRelationship =
      child->Relationship.Parent != 0 || !child->Relationship.Children.empty();

  return true;
}

bool Scene::IsRootEntity(const Entity &entity) const {
  if (!entity.HasRelationship || entity.Relationship.Parent == 0)
    return true;

  return GetEntityByUUIDPtr(entity.Relationship.Parent) == nullptr;
}

void Scene::NormalizeHierarchy() {
  for (auto &entity : m_Entities) {
    if (!entity.HasRelationship)
      entity.Relationship.Parent = 0;
    entity.Relationship.Children.clear();
  }

  for (auto &entity : m_Entities) {
    UUID parentUUID =
        entity.HasRelationship ? entity.Relationship.Parent : UUID(0);
    if (parentUUID == 0)
      continue;

    if (!GetEntityByUUIDPtr(parentUUID) ||
        WouldCreateCycle(entity.GetUUID(), parentUUID)) {
      entity.Relationship.Parent = 0;
      continue;
    }

    Entity *parent = GetEntityByUUIDPtr(parentUUID);
    parent->HasRelationship = true;
    auto &children = parent->Relationship.Children;
    if (std::find(children.begin(), children.end(), entity.GetUUID()) ==
        children.end()) {
      children.push_back(entity.GetUUID());
    }
  }

  for (auto &entity : m_Entities) {
    entity.HasRelationship =
        entity.Relationship.Parent != 0 || !entity.Relationship.Children.empty();
  }
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
    btCollisionShape *shape = nullptr;
    if (entity.HasRigidBody && entity.HasMeshCollider &&
        entity.HasMeshRenderer && entity.MeshRenderer.Mesh) {
      auto &mesh = entity.MeshRenderer.Mesh;
      const auto &verts = mesh->GetVertices();
      const auto &inds  = mesh->GetIndices();
      glm::vec3 sc = entity.Transform.Scale;
      if (entity.MeshCollider.Convex) {

        auto *hull = new btConvexHullShape();
        for (const auto &v : verts) {
          hull->addPoint(btVector3(v.Position.x * sc.x,
                                   v.Position.y * sc.y,
                                   v.Position.z * sc.z),
                         false);
        }
        hull->recalcLocalAabb();
        shape = hull;
        entity.MeshCollider.RuntimeShape = hull;
      } else {

        auto *tm = new btTriangleMesh();
        for (size_t i = 0; i + 2 < inds.size(); i += 3) {
          const auto &p0 = verts[inds[i+0]].Position;
          const auto &p1 = verts[inds[i+1]].Position;
          const auto &p2 = verts[inds[i+2]].Position;
          tm->addTriangle(btVector3(p0.x*sc.x, p0.y*sc.y, p0.z*sc.z),
                          btVector3(p1.x*sc.x, p1.y*sc.y, p1.z*sc.z),
                          btVector3(p2.x*sc.x, p2.y*sc.y, p2.z*sc.z));
        }
        auto *bvh = new btBvhTriangleMeshShape(tm, true);
        shape = bvh;
        entity.MeshCollider.RuntimeShape = bvh;
        entity.MeshCollider.RuntimeMesh  = tm;
      }
    }
    if (entity.HasRigidBody && entity.HasBoxCollider && !shape) {
      glm::vec3 size = entity.BoxCollider.Size * entity.Transform.Scale;
      shape = new btBoxShape(
          btVector3(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f));
    }
    if (entity.HasRigidBody && entity.HasSphereCollider && !shape) {
      float r = entity.SphereCollider.Radius *
                std::max({entity.Transform.Scale.x, entity.Transform.Scale.y,
                          entity.Transform.Scale.z});
      shape = new btSphereShape(r);
    }
    if (entity.HasRigidBody && entity.HasCapsuleCollider && !shape) {
      float r = entity.CapsuleCollider.Radius *
                std::max(entity.Transform.Scale.x, entity.Transform.Scale.z);
      float h = entity.CapsuleCollider.Height * entity.Transform.Scale.y;
      shape = new btCapsuleShape(r, h);
    }

    if (entity.HasRigidBody && shape) {
      btTransform startTransform;
      startTransform.setIdentity();
      startTransform.setOrigin(btVector3(entity.Transform.Translation.x,
                                         entity.Transform.Translation.y,
                                         entity.Transform.Translation.z));

      glm::quat q(entity.Transform.Rotation);
      startTransform.setRotation(btQuaternion(q.x, q.y, q.z, q.w));

      bool isTriangleMesh = entity.HasMeshCollider && !entity.MeshCollider.Convex;
      btScalar mass = (entity.RigidBody.Type == RigidBodyComponent::BodyType::Dynamic
                       && !isTriangleMesh)
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
      body->setFriction(entity.RigidBody.Friction);
      body->setRestitution(entity.RigidBody.Restitution);
      body->setUserIndex(entity.GetID());
      if (mass != 0.0f)
        UpdateContinuousCollision(body);
      if (entity.RigidBody.Type == RigidBodyComponent::BodyType::Kinematic) {
        body->setCollisionFlags(body->getCollisionFlags() |
                                btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
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
    m_Solver = nullptr;
    m_Broadphase = nullptr;
    m_Dispatcher = nullptr;
    m_CollisionConfiguration = nullptr;
  }
}

void ScriptableEntity::ApplyForce(const glm::vec3 &force) {
  if (m_Entity && m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    body->activate(true);
    body->applyCentralForce(btVector3(force.x, force.y, force.z));
  }
}

void ScriptableEntity::ApplyImpulse(const glm::vec3 &impulse) {
  if (m_Entity && m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    body->activate(true);
    body->applyCentralImpulse(btVector3(impulse.x, impulse.y, impulse.z));
  }
}

void ScriptableEntity::Teleport(const glm::vec3 &position) {
  if (!m_Entity)
    return;
  m_Entity->Transform.Translation = position;
  if (m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody) {
    btRigidBody *body = (btRigidBody *)m_Entity->RigidBody.RuntimeBody;
    btTransform trans = body->getWorldTransform();
    trans.setOrigin(btVector3(position.x, position.y, position.z));
    body->setWorldTransform(trans);
    body->setInterpolationWorldTransform(trans);
    if (body->getMotionState())
      body->getMotionState()->setWorldTransform(trans);
    body->activate(true);
  }
}

namespace {
struct IgnoreEntityRayCallback : btCollisionWorld::ClosestRayResultCallback {
  int IgnoreID;
  bool IncludeGhosts;
  IgnoreEntityRayCallback(const btVector3 &from, const btVector3 &to, int ignoreID,
                          bool includeGhosts)
      : ClosestRayResultCallback(from, to), IgnoreID(ignoreID),
        IncludeGhosts(includeGhosts) {}

  bool needsCollision(btBroadphaseProxy *proxy) const override {
    auto *object = (const btCollisionObject *)proxy->m_clientObject;
    if (object && object->getUserIndex() == IgnoreID)
      return false;
    if (object && !IncludeGhosts &&
        (object->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
      return false;
    return ClosestRayResultCallback::needsCollision(proxy);
  }
};
}

bool Scene::Raycast(const glm::vec3 &from, const glm::vec3 &to,
                    RaycastHit &outHit, int ignoreEntityID,
                    bool includeGhosts) const {
  if (!m_PhysicsWorld)
    return false;

  btVector3 btFrom(from.x, from.y, from.z);
  btVector3 btTo(to.x, to.y, to.z);
  IgnoreEntityRayCallback callback(btFrom, btTo, ignoreEntityID, includeGhosts);
  ((btDiscreteDynamicsWorld *)m_PhysicsWorld)->rayTest(btFrom, btTo, callback);
  if (!callback.hasHit())
    return false;

  const btVector3 &p = callback.m_hitPointWorld;
  const btVector3 &n = callback.m_hitNormalWorld;
  outHit.Point = {p.x(), p.y(), p.z()};
  outHit.Normal = {n.x(), n.y(), n.z()};
  outHit.Distance = glm::distance(from, outHit.Point);
  outHit.EntityID = callback.m_collisionObject->getUserIndex();
  outHit.Velocity = {0.0f, 0.0f, 0.0f};
  if (const btRigidBody *body = btRigidBody::upcast(callback.m_collisionObject)) {
    btVector3 v = body->getVelocityInLocalPoint(p - body->getCenterOfMassPosition());
    outHit.Velocity = {v.x(), v.y(), v.z()};
  }
  return true;
}

void Scene::SetEntityPose(Entity &entity, const glm::vec3 &position,
                          const glm::vec3 &rotation) {
  entity.Transform.Translation = position;
  entity.Transform.Rotation = rotation;
  if (!m_PhysicsWorld || !entity.HasRigidBody || !entity.RigidBody.RuntimeBody)
    return;
  auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
  glm::quat q(rotation);
  btTransform trans(btQuaternion(q.x, q.y, q.z, q.w),
                    btVector3(position.x, position.y, position.z));
  body->setWorldTransform(trans);
  body->setInterpolationWorldTransform(trans);
  if (body->getMotionState())
    body->getMotionState()->setWorldTransform(trans);
  body->activate(true);
}

void Scene::SetEntityVelocity(Entity &entity, const glm::vec3 &linear,
                              const glm::vec3 &angular) {
  if (!m_PhysicsWorld || !entity.HasRigidBody || !entity.RigidBody.RuntimeBody)
    return;
  auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
  body->activate(true);
  body->setLinearVelocity(btVector3(linear.x, linear.y, linear.z));
  body->setAngularVelocity(btVector3(angular.x, angular.y, angular.z));
}

void Scene::SetEntitySolid(Entity &entity, bool solid) {
  if (!m_PhysicsWorld || !entity.HasRigidBody || !entity.RigidBody.RuntimeBody)
    return;
  auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
  int flags = body->getCollisionFlags();
  bool isSolid = (flags & btCollisionObject::CF_NO_CONTACT_RESPONSE) == 0;
  if (isSolid == solid)
    return;
  body->setCollisionFlags(solid ? (flags & ~btCollisionObject::CF_NO_CONTACT_RESPONSE)
                                : (flags | btCollisionObject::CF_NO_CONTACT_RESPONSE));
  body->activate(true);
}

void Scene::SetEntityColliderScale(Entity &entity, const glm::vec3 &scale) {
  if (!m_PhysicsWorld || !entity.HasRigidBody || !entity.RigidBody.RuntimeBody)
    return;
  auto *body = (btRigidBody *)entity.RigidBody.RuntimeBody;
  auto *world = (btDiscreteDynamicsWorld *)m_PhysicsWorld;
  btCollisionShape *shape = body->getCollisionShape();
  btVector3 wanted(scale.x, scale.y, scale.z);
  if (!shape || shape->getLocalScaling().distance2(wanted) < 1e-8f)
    return;

  shape->setLocalScaling(wanted);
  if (body->getInvMass() > 0.0f) {
    btScalar mass = 1.0f / body->getInvMass();
    btVector3 inertia(0, 0, 0);
    shape->calculateLocalInertia(mass, inertia);
    body->setMassProps(mass, inertia);
    body->updateInertiaTensor();
  }
  if (body->getInvMass() > 0.0f)
    UpdateContinuousCollision(body);
  // Contacts cached for the old size are no longer valid.
  world->getBroadphase()->getOverlappingPairCache()->cleanProxyFromPairs(
      body->getBroadphaseHandle(), world->getDispatcher());
  world->updateSingleAabb(body);
  body->activate(true);
}

glm::vec3 Scene::GetEntityVelocity(const Entity &entity) const {
  if (!m_PhysicsWorld || !entity.HasRigidBody || !entity.RigidBody.RuntimeBody)
    return {0.0f, 0.0f, 0.0f};
  const btVector3 &v = ((btRigidBody *)entity.RigidBody.RuntimeBody)->getLinearVelocity();
  return {v.x(), v.y(), v.z()};
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

}
