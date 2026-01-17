#pragma once

#include "../Core/UUID.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Texture.h"
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

struct IDComponent {
  UUID ID;

  IDComponent() = default;
  IDComponent(const IDComponent &) = default;
  IDComponent(const UUID &uuid) : ID(uuid) {}
};

struct RelationshipComponent {
  UUID Parent = 0;
  std::vector<UUID> Children;

  RelationshipComponent() = default;
  RelationshipComponent(const RelationshipComponent &) = default;
};

struct RectTransformComponent {
  glm::vec2 AnchorsMin = {0.5f, 0.5f};
  glm::vec2 AnchorsMax = {0.5f, 0.5f};
  glm::vec2 Pivot = {0.5f, 0.5f};
  glm::vec2 Position = {0.0f, 0.0f};
  glm::vec2 SizeDelta = {100.0f, 100.0f};
  glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
  glm::vec3 Scale = {1.0f, 1.0f, 1.0f};

  RectTransformComponent() = default;
  RectTransformComponent(const RectTransformComponent &) = default;
};

struct CanvasComponent {
  bool IsScreenSpace = true;
  bool IsPixelPerfect = false;
  int SortingOrder = 0;
  bool Enabled = true;

  CanvasComponent() = default;
  CanvasComponent(const CanvasComponent &) = default;
};

struct ImageComponent {
  glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
  std::shared_ptr<Texture2D> Texture;
  bool Visible = true;

  ImageComponent() = default;
  ImageComponent(const ImageComponent &) = default;
};

struct ButtonComponent {
  glm::vec4 NormalColor = {0.8f, 0.8f, 0.8f, 1.0f};
  glm::vec4 HoverColor = {1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 ClickedColor = {0.6f, 0.6f, 0.6f, 1.0f};

  // Runtime State
  bool IsHovered = false;
  bool IsClicked = false;

  ButtonComponent() = default;
  ButtonComponent(const ButtonComponent &) = default;
};

struct TextComponent {
  std::string TextString = "New Text";
  glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
  float FontSize = 12.0f;

  TextComponent() = default;
  TextComponent(const TextComponent &) = default;
};

struct TransformComponent {
  glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
  glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
  glm::vec3 Scale = {1.0f, 1.0f, 1.0f};

  TransformComponent() = default;
  TransformComponent(const TransformComponent &) = default;
  TransformComponent(const glm::vec3 &translation) : Translation(translation) {}
};

struct SpriteRendererComponent {
  glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
  std::shared_ptr<Texture2D> Texture;
  float TilingFactor = 1.0f;
  std::string TexturePath;

  SpriteRendererComponent() = default;
  SpriteRendererComponent(const SpriteRendererComponent &) = default;
  SpriteRendererComponent(const glm::vec4 &color) : Color(color) {}
};

struct MeshLOD {
  std::shared_ptr<class Mesh> Mesh;
  float Distance;
};

struct MeshRendererComponent {
  std::shared_ptr<class Mesh> Mesh;
  std::string FilePath;
  glm::vec4 Color = {1.0f, 1.0f, 1.0f, 1.0f};

  std::vector<MeshLOD> LODs;

  std::shared_ptr<Texture2D> DiffuseMap;
  std::string DiffusePath;
  std::shared_ptr<Texture2D> NormalMap;
  std::string NormalPath;

  MeshRendererComponent() = default;
  MeshRendererComponent(const MeshRendererComponent &) = default;
};

class Entity;

class ScriptableEntity {
public:
  virtual ~ScriptableEntity() {}

  template <typename T> T &GetComponent();

public:
  virtual void OnCreate() {}
  virtual void OnDestroy() {}
  virtual void OnUpdate(float ts) {}
  virtual void OnImGuiRender() {}

  void ApplyForce(const glm::vec3 &force);
  void SetLinearVelocity(const glm::vec3 &velocity);
  glm::vec3 GetLinearVelocity();
  void LoadScene(const std::string &path);

  Entity *m_Entity = nullptr;

private:
  friend class Scene;
  friend class SceneSerializer;
  friend class SceneHierarchyPanel;
};

struct NativeScriptComponent {
  ScriptableEntity *Instance = nullptr;

  ScriptableEntity *(*InstantiateScript)();
  void (*DestroyScript)(NativeScriptComponent *);

  std::string ScriptName;

  template <typename T> void Bind(const std::string &name) {
    ScriptName = name;
    InstantiateScript = []() {
      return static_cast<ScriptableEntity *>(new T());
    };
    DestroyScript = [](NativeScriptComponent *nsc) {
      delete nsc->Instance;
      nsc->Instance = nullptr;
    };
  }
};

struct RigidBodyComponent {
  enum class BodyType { Static = 0, Dynamic, Kinematic };
  BodyType Type = BodyType::Static;
  bool FixedRotation = false;

  void *RuntimeBody = nullptr;

  RigidBodyComponent() = default;
  RigidBodyComponent(const RigidBodyComponent &) = default;

  float Mass = 1.0f;
  float Friction = 0.5f;
  float RollingFriction = 0.1f;
  float SpinningFriction = 0.1f;
  float Restitution = 0.0f;
};

struct CameraComponent {
  enum class ProjectionType { Perspective = 0, Orthographic = 1 };

  ProjectionType Type = ProjectionType::Perspective;
  bool Primary = true;
  bool FixedAspectRatio = false;

  bool AntiAliasing = true;
  bool FrustumCulling = true;

  float PerspectiveFOV = 45.0f;
  float PerspectiveNear = 0.01f;
  float PerspectiveFar = 1000.0f;
  float OrthographicSize = 10.0f;
  float OrthographicNear = -1.0f;
  float OrthographicFar = 1.0f;

  CameraComponent() = default;
  CameraComponent(const CameraComponent &) = default;

  glm::mat4 GetProjectionMatrix(float aspectRatio) const {
    if (Type == ProjectionType::Perspective) {
      return glm::perspective(glm::radians(PerspectiveFOV), aspectRatio,
                              PerspectiveNear, PerspectiveFar);
    } else {
      float orthoLeft = -OrthographicSize * aspectRatio * 0.5f;
      float orthoRight = OrthographicSize * aspectRatio * 0.5f;
      float orthoBottom = -OrthographicSize * 0.5f;
      float orthoTop = OrthographicSize * 0.5f;
      return glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop,
                        OrthographicNear, OrthographicFar);
    }
  }
};

struct BoxColliderComponent {
  glm::vec3 Offset = {0.0f, 0.0f, 0.0f};
  glm::vec3 Size = {0.5f, 0.5f, 0.5f};

  float Density = 1.0f;
  float Friction = 0.5f;
  float Restitution = 0.0f;
  float RestitutionThreshold = 0.5f;

  void *RuntimeShape = nullptr;

  BoxColliderComponent() = default;
  BoxColliderComponent(const BoxColliderComponent &) = default;
};

struct LightComponent {
  enum class LightType { Directional = 0, Point = 1 };
  LightType Type = LightType::Directional;
  glm::vec3 Color = {1.0f, 1.0f, 1.0f};
  float Intensity = 1.0f;

  float Radius = 10.0f;

  LightComponent() = default;
  LightComponent(const LightComponent &) = default;
};

struct SkyboxComponent {
  std::string FacePaths[6];
  bool IsLoaded = false;
  uint32_t RendererID = 0;

  SkyboxComponent() = default;
  SkyboxComponent(const SkyboxComponent &) = default;
};

struct AudioSourceComponent {
  std::string FilePath;
  float Volume = 1.0f;
  float Pitch = 1.0f;
  float Range = 100.0f;
  bool Loop = false;
  bool PlayOnAwake = true;
  bool Spatial = true;

  bool IsPlaying = false;

  AudioSourceComponent() = default;
  AudioSourceComponent(const AudioSourceComponent &) = default;
};

}
