#pragma once

#include "../Core/EngineAPI.h"
#include "Components.h"
#include <glm/glm.hpp>
#include <string>

namespace Engine {

class ENGINE_API Entity {
public:
  Entity() = default;
  Entity(int id, const std::string &name = "Entity") : m_ID(id), Name(name) {}

  std::string Name;

  TransformComponent Transform;
  SpriteRendererComponent SpriteRenderer;
  RigidBodyComponent RigidBody;
  BoxColliderComponent BoxCollider;
  SphereColliderComponent SphereCollider;
  CapsuleColliderComponent CapsuleCollider;
  MeshColliderComponent MeshCollider;
  WaterComponent Water;
  ParticleSystemComponent ParticleSystem;
  CameraComponent Camera;
  LightComponent Light;
  SkyboxComponent Skybox;
  MeshRendererComponent MeshRenderer;

  IDComponent UUID;
  RelationshipComponent Relationship;

  CanvasComponent Canvas;
  RectTransformComponent RectTransform;
  ImageComponent Image;
  ButtonComponent Button;
  TextComponent Text;
  AudioSourceComponent AudioSource;
  VRRigComponent VRRig;

  bool HasSkybox = false;
  bool HasMeshRenderer = false;
  bool HasRigidBody = false;
  bool HasBoxCollider = false;
  bool HasSphereCollider = false;
  bool HasCapsuleCollider = false;
  bool HasMeshCollider = false;
  bool HasWater = false;
  bool HasParticleSystem = false;
  bool HasCamera = false;
  bool HasSpriteRenderer = true;
  bool HasLight = false;
  bool HasAudioSource = false;
  bool HasVRRig = false;

  bool HasRelationship = false;
  bool HasCanvas = false;
  bool HasRectTransform = false;
  bool HasImage = false;
  bool HasButton = false;
  bool HasText = false;

  bool IsUIElement() const { return HasRectTransform; }

  NativeScriptComponent m_NativeScript;
  bool HasScript = false;

  template <typename T> void AddScript(const std::string &name) {
    m_NativeScript.Bind<T>(name);
    HasScript = true;
  }

  int GetID() const { return m_ID; }
  Engine::UUID GetUUID() const { return UUID.ID; }

private:
  int m_ID = -1;
};

}
