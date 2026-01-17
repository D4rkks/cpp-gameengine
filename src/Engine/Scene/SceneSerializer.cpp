#include "SceneSerializer.h"
#include "../Renderer/Texture.h"
#include "../Scripting/ScriptRegistry.h"
#include "../Scripts/PlayerController.h"
#include "Entity.h"
#include <fstream>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace Engine {

SceneSerializer::SceneSerializer(const std::shared_ptr<Scene> &scene)
    : m_Scene(scene) {}

#define WRITE_VEC2(vec)                                                        \
  out << YAML::Flow << YAML::BeginSeq << vec.x << vec.y << YAML::EndSeq

#define WRITE_VEC3(vec)                                                        \
  out << YAML::Flow << YAML::BeginSeq << vec.x << vec.y << vec.z << YAML::EndSeq

#define WRITE_VEC4(vec)                                                        \
  out << YAML::Flow << YAML::BeginSeq << vec.r << vec.g << vec.b << vec.a      \
      << YAML::EndSeq

#define READ_VEC2(node, vec)                                                   \
  if (node)                                                                    \
  vec = {node[0].as<float>(), node[1].as<float>()}

#define READ_VEC3(node, vec)                                                   \
  if (node)                                                                    \
  vec = {node[0].as<float>(), node[1].as<float>(), node[2].as<float>()}

#define READ_VEC4(node, vec)                                                   \
  if (node)                                                                    \
  vec = {node[0].as<float>(), node[1].as<float>(), node[2].as<float>(),        \
         node[3].as<float>()}

template <typename T>
static void
SerializeComponent(YAML::Emitter &out, const Entity &entity,
                   const std::string &key, bool hasComponent,
                   const T &component,
                   std::function<void(YAML::Emitter &, const T &)> func) {
  if (hasComponent) {
    out << YAML::Key << key;
    out << YAML::BeginMap;
    func(out, component);
    out << YAML::EndMap;
  }
}

template <typename T>
static void DeserializeComponent(YAML::Node &entityNode, const std::string &key,
                                 Entity &entity, bool &hasComponent,
                                 T &component,
                                 std::function<void(YAML::Node &, T &)> func) {
  auto node = entityNode[key];
  hasComponent = (bool)node;
  if (node) {
    func(node, component);
  }
}

static void SerializeEntity(YAML::Emitter &out, const Entity &entity) {
  out << YAML::BeginMap;
  out << YAML::Key << "Entity" << YAML::Value << (uint64_t)entity.GetUUID();
  out << YAML::Key << "Name" << YAML::Value << entity.Name;

  out << YAML::Key << "TransformComponent";
  out << YAML::BeginMap;
  out << YAML::Key << "Translation";
  WRITE_VEC3(entity.Transform.Translation);
  out << YAML::Key << "Rotation";
  WRITE_VEC3(entity.Transform.Rotation);
  out << YAML::Key << "Scale";
  WRITE_VEC3(entity.Transform.Scale);
  out << YAML::EndMap;

  SerializeComponent<SpriteRendererComponent>(
      out, entity, "SpriteRendererComponent", entity.HasSpriteRenderer,
      entity.SpriteRenderer, [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "Color";
        WRITE_VEC4(c.Color);
        if (!c.TexturePath.empty())
          out << YAML::Key << "TexturePath" << YAML::Value << c.TexturePath;
        out << YAML::Key << "TilingFactor" << YAML::Value << c.TilingFactor;
      });

  SerializeComponent<RigidBodyComponent>(
      out, entity, "RigidBodyComponent", entity.HasRigidBody, entity.RigidBody,
      [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "Type" << YAML::Value << (int)c.Type;
        out << YAML::Key << "FixedRotation" << YAML::Value << c.FixedRotation;
        out << YAML::Key << "Mass" << YAML::Value << c.Mass;
        out << YAML::Key << "Friction" << YAML::Value << c.Friction;
        out << YAML::Key << "RollingFriction" << YAML::Value
            << c.RollingFriction;
        out << YAML::Key << "SpinningFriction" << YAML::Value
            << c.SpinningFriction;
        out << YAML::Key << "Restitution" << YAML::Value << c.Restitution;
      });

  SerializeComponent<BoxColliderComponent>(
      out, entity, "BoxColliderComponent", entity.HasBoxCollider,
      entity.BoxCollider, [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "Offset";
        WRITE_VEC3(c.Offset);
        out << YAML::Key << "Size";
        WRITE_VEC3(c.Size);
        out << YAML::Key << "Density" << YAML::Value << c.Density;
        out << YAML::Key << "Friction" << YAML::Value << c.Friction;
        out << YAML::Key << "Restitution" << YAML::Value << c.Restitution;
        out << YAML::Key << "RestitutionThreshold" << YAML::Value
            << c.RestitutionThreshold;
      });

  SerializeComponent<CameraComponent>(
      out, entity, "CameraComponent", entity.HasCamera, entity.Camera,
      [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "ProjectionType" << YAML::Value << (int)c.Type;
        out << YAML::Key << "Primary" << YAML::Value << c.Primary;
        out << YAML::Key << "FixedAspectRatio" << YAML::Value
            << c.FixedAspectRatio;
        out << YAML::Key << "PerspectiveFOV" << YAML::Value << c.PerspectiveFOV;
        out << YAML::Key << "PerspectiveNear" << YAML::Value
            << c.PerspectiveNear;
        out << YAML::Key << "PerspectiveFar" << YAML::Value << c.PerspectiveFar;
        out << YAML::Key << "OrthographicSize" << YAML::Value
            << c.OrthographicSize;
        out << YAML::Key << "OrthographicNear" << YAML::Value
            << c.OrthographicNear;
        out << YAML::Key << "OrthographicFar" << YAML::Value
            << c.OrthographicFar;
      });

  SerializeComponent<NativeScriptComponent>(
      out, entity, "ScriptComponent", entity.HasScript, entity.m_NativeScript,
      [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "Name" << YAML::Value << c.ScriptName;
      });

  if (entity.HasRelationship) {
    out << YAML::Key << "RelationshipComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "Parent" << YAML::Value
        << (uint64_t)entity.Relationship.Parent;
    out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
    for (auto &child : entity.Relationship.Children) {
      out << (uint64_t)child;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
  } else {
  }

  if (entity.HasCanvas) {
    out << YAML::Key << "CanvasComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "Enabled" << YAML::Value << entity.Canvas.Enabled;
    out << YAML::Key << "IsScreenSpace" << YAML::Value
        << entity.Canvas.IsScreenSpace;
    out << YAML::Key << "IsPixelPerfect" << YAML::Value
        << entity.Canvas.IsPixelPerfect;
    out << YAML::Key << "SortingOrder" << YAML::Value
        << entity.Canvas.SortingOrder;
    out << YAML::EndMap;
  }

  if (entity.HasRectTransform) {
    out << YAML::Key << "RectTransformComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "AnchorsMin";
    WRITE_VEC2(entity.RectTransform.AnchorsMin);
    out << YAML::Key << "AnchorsMax";
    WRITE_VEC2(entity.RectTransform.AnchorsMax);
    out << YAML::Key << "Pivot";
    WRITE_VEC2(entity.RectTransform.Pivot);
    out << YAML::Key << "Position";
    WRITE_VEC2(entity.RectTransform.Position);
    out << YAML::Key << "SizeDelta";
    WRITE_VEC2(entity.RectTransform.SizeDelta);
    out << YAML::Key << "Rotation";
    WRITE_VEC3(entity.RectTransform.Rotation);
    out << YAML::Key << "Scale";
    WRITE_VEC3(entity.RectTransform.Scale);
    out << YAML::EndMap;
  }

  if (entity.HasImage) {
    out << YAML::Key << "ImageComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "Color";
    WRITE_VEC4(entity.Image.Color);
    out << YAML::Key << "Visible" << YAML::Value << entity.Image.Visible;
    out << YAML::EndMap;
  }

  if (entity.HasButton) {
    out << YAML::Key << "ButtonComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "NormalColor";
    WRITE_VEC4(entity.Button.NormalColor);
    out << YAML::Key << "HoverColor";
    WRITE_VEC4(entity.Button.HoverColor);
    out << YAML::Key << "ClickedColor";
    WRITE_VEC4(entity.Button.ClickedColor);
    out << YAML::EndMap;
  }

  if (entity.HasText) {
    out << YAML::Key << "TextComponent";
    out << YAML::BeginMap;
    out << YAML::Key << "TextString" << YAML::Value << entity.Text.TextString;
    out << YAML::Key << "Color";
    WRITE_VEC4(entity.Text.Color);
    out << YAML::EndMap;
  }

  SerializeComponent<LightComponent>(
      out, entity, "LightComponent", entity.HasLight, entity.Light,
      [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "Type" << YAML::Value << (int)c.Type;
        out << YAML::Key << "Color";
        out << YAML::Flow << YAML::BeginSeq << c.Color.r << c.Color.g
            << c.Color.b << YAML::EndSeq;
        out << YAML::Key << "Intensity" << YAML::Value << c.Intensity;
        out << YAML::Key << "Radius" << YAML::Value << c.Radius;
      });

  SerializeComponent<MeshRendererComponent>(
      out, entity, "MeshRendererComponent", entity.HasMeshRenderer,
      entity.MeshRenderer, [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "FilePath" << YAML::Value << c.FilePath;
        out << YAML::Key << "Color";
        WRITE_VEC4(c.Color);
        out << YAML::Key << "DiffusePath" << YAML::Value << c.DiffusePath;
        out << YAML::Key << "NormalPath" << YAML::Value << c.NormalPath;

        if (!c.LODs.empty()) {
          out << YAML::Key << "LODs" << YAML::Value << YAML::BeginSeq;
          for (const auto &lod : c.LODs) {
            out << YAML::BeginMap;
            out << YAML::Key << "FilePath" << YAML::Value
                << (lod.Mesh ? lod.Mesh->GetFilePath() : "");
            out << YAML::Key << "Distance" << YAML::Value << lod.Distance;
            out << YAML::EndMap;
          }
          out << YAML::EndSeq;
        }
      });

  SerializeComponent<SkyboxComponent>(out, entity, "SkyboxComponent",
                                      entity.HasSkybox, entity.Skybox,
                                      [](YAML::Emitter &out, const auto &c) {
                                        out << YAML::Key << "Faces"
                                            << YAML::Value << YAML::Flow
                                            << YAML::BeginSeq;
                                        for (int i = 0; i < 6; i++)
                                          out << c.FacePaths[i];
                                        out << YAML::EndSeq;
                                      });

  SerializeComponent<AudioSourceComponent>(
      out, entity, "AudioSourceComponent", entity.HasAudioSource,
      entity.AudioSource, [](YAML::Emitter &out, const auto &c) {
        out << YAML::Key << "FilePath" << YAML::Value << c.FilePath;
        out << YAML::Key << "Volume" << YAML::Value << c.Volume;
        out << YAML::Key << "Pitch" << YAML::Value << c.Pitch;
        out << YAML::Key << "Range" << YAML::Value << c.Range;
        out << YAML::Key << "Loop" << YAML::Value << c.Loop;
        out << YAML::Key << "PlayOnAwake" << YAML::Value << c.PlayOnAwake;
        out << YAML::Key << "Spatial" << YAML::Value << c.Spatial;
      });

  out << YAML::EndMap;
}

static bool DeserializeEntity(YAML::Node entityNode,
                              Entity &deserializedEntity) {
  auto transformComponent = entityNode["TransformComponent"];
  if (transformComponent) {
    READ_VEC3(transformComponent["Translation"],
              deserializedEntity.Transform.Translation);
    READ_VEC3(transformComponent["Rotation"],
              deserializedEntity.Transform.Rotation);
    READ_VEC3(transformComponent["Scale"], deserializedEntity.Transform.Scale);
  }

  DeserializeComponent<SpriteRendererComponent>(
      entityNode, "SpriteRendererComponent", deserializedEntity,
      deserializedEntity.HasSpriteRenderer, deserializedEntity.SpriteRenderer,
      [](YAML::Node &node, auto &c) {
        if (node["Color"]) {
          READ_VEC4(node["Color"], c.Color);
        }
        if (node["TexturePath"])
          c.TexturePath = node["TexturePath"].as<std::string>();
        if (node["TilingFactor"])
          c.TilingFactor = node["TilingFactor"].as<float>();
        if (!c.TexturePath.empty())
          c.Texture = std::make_shared<Texture2D>(c.TexturePath);
      });

  DeserializeComponent<RigidBodyComponent>(
      entityNode, "RigidBodyComponent", deserializedEntity,
      deserializedEntity.HasRigidBody, deserializedEntity.RigidBody,
      [](YAML::Node &node, auto &c) {
        c.Type = (RigidBodyComponent::BodyType)node["Type"].as<int>();
        c.FixedRotation = node["FixedRotation"].as<bool>();
        if (node["Mass"])
          c.Mass = node["Mass"].as<float>();
        if (node["Friction"])
          c.Friction = node["Friction"].as<float>();
        if (node["RollingFriction"])
          c.RollingFriction = node["RollingFriction"].as<float>();
        if (node["SpinningFriction"])
          c.SpinningFriction = node["SpinningFriction"].as<float>();
        if (node["Restitution"])
          c.Restitution = node["Restitution"].as<float>();
      });

  DeserializeComponent<BoxColliderComponent>(
      entityNode, "BoxColliderComponent", deserializedEntity,
      deserializedEntity.HasBoxCollider, deserializedEntity.BoxCollider,
      [](YAML::Node &node, auto &c) {
        READ_VEC3(node["Offset"], c.Offset);
        READ_VEC3(node["Size"], c.Size);
        if (node["Density"])
          c.Density = node["Density"].as<float>();
        if (node["Friction"])
          c.Friction = node["Friction"].as<float>();
        if (node["Restitution"])
          c.Restitution = node["Restitution"].as<float>();
        if (node["RestitutionThreshold"])
          c.RestitutionThreshold = node["RestitutionThreshold"].as<float>();
      });

  DeserializeComponent<CameraComponent>(
      entityNode, "CameraComponent", deserializedEntity,
      deserializedEntity.HasCamera, deserializedEntity.Camera,
      [](YAML::Node &node, auto &c) {
        c.Type =
            (CameraComponent::ProjectionType)node["ProjectionType"].as<int>();
        c.Primary = node["Primary"].as<bool>();
        c.FixedAspectRatio = node["FixedAspectRatio"].as<bool>();
        c.PerspectiveFOV = node["PerspectiveFOV"].as<float>();
        c.PerspectiveNear = node["PerspectiveNear"].as<float>();
        c.OrthographicNear = node["OrthographicNear"].as<float>();
        c.OrthographicFar = node["OrthographicFar"].as<float>();
        if (node["AntiAliasing"])
          c.AntiAliasing = node["AntiAliasing"].as<bool>();
        if (node["FrustumCulling"])
          c.FrustumCulling = node["FrustumCulling"].as<bool>();
      });

  DeserializeComponent<NativeScriptComponent>(
      entityNode, "ScriptComponent", deserializedEntity,
      deserializedEntity.HasScript, deserializedEntity.m_NativeScript,
      [&](YAML::Node &node, auto &c) {
        if (node["Name"]) {
          std::string scriptName = node["Name"].as<std::string>();
          ScriptableEntity *script = ScriptRegistry::Create(scriptName);
          if (script) {
            deserializedEntity.m_NativeScript.Instance = script;
            deserializedEntity.m_NativeScript.ScriptName = scriptName;
            script->m_Entity = &deserializedEntity;
            script->OnCreate();
            deserializedEntity.HasScript = true;
          }
        }
      });

  DeserializeComponent<LightComponent>(
      entityNode, "LightComponent", deserializedEntity,
      deserializedEntity.HasLight, deserializedEntity.Light,
      [](YAML::Node &node, auto &c) {
        c.Type = (LightComponent::LightType)node["Type"].as<int>();
        READ_VEC3(node["Color"], c.Color);
        c.Intensity = node["Intensity"].as<float>();
        if (node["Radius"])
          c.Radius = node["Radius"].as<float>();
      });

  auto meshRendererComponent = entityNode["MeshRendererComponent"];
  if (meshRendererComponent) {
    auto &mrc = deserializedEntity.MeshRenderer;
    mrc.FilePath = meshRendererComponent["FilePath"].as<std::string>();
    READ_VEC4(meshRendererComponent["Color"], mrc.Color);

    if (!mrc.FilePath.empty())
      mrc.Mesh = std::make_shared<Mesh>(mrc.FilePath);

    auto lods = meshRendererComponent["LODs"];
    if (lods) {
      for (auto lod : lods) {
        std::string lodPath = lod["FilePath"].as<std::string>();
        float distance = lod["Distance"].as<float>();
        if (!lodPath.empty()) {
          mrc.LODs.push_back({std::make_shared<Mesh>(lodPath), distance});
        }
      }
    }

    if (meshRendererComponent["DiffusePath"]) {
      mrc.DiffusePath = meshRendererComponent["DiffusePath"].as<std::string>();
      if (!mrc.DiffusePath.empty())
        mrc.DiffuseMap = std::make_shared<Texture2D>(mrc.DiffusePath);
    }

    if (meshRendererComponent["NormalPath"]) {
      mrc.NormalPath = meshRendererComponent["NormalPath"].as<std::string>();
      if (!mrc.NormalPath.empty())
        mrc.NormalMap = std::make_shared<Texture2D>(mrc.NormalPath);
    }

    deserializedEntity.HasMeshRenderer = true;
  }
  DeserializeComponent<SkyboxComponent>(
      entityNode, "SkyboxComponent", deserializedEntity,
      deserializedEntity.HasSkybox, deserializedEntity.Skybox,
      [](YAML::Node &node, auto &c) {
        auto faces = node["Faces"];
        std::vector<std::string> facePaths;
        for (int i = 0; i < (int)faces.size() && i < 6; i++) {
          c.FacePaths[i] = faces[i].as<std::string>();
          facePaths.push_back(c.FacePaths[i]);
        }
        if (facePaths.size() == 6) {
          c.RendererID = Texture::LoadCubemap(facePaths);
          c.IsLoaded = true;
        }
      });

  DeserializeComponent<AudioSourceComponent>(
      entityNode, "AudioSourceComponent", deserializedEntity,
      deserializedEntity.HasAudioSource, deserializedEntity.AudioSource,
      [](YAML::Node &node, auto &c) {
        c.FilePath = node["FilePath"].as<std::string>();
        if (node["Volume"])
          c.Volume = node["Volume"].as<float>();
        if (node["Pitch"])
          c.Pitch = node["Pitch"].as<float>();
        if (node["Loop"])
          c.Loop = node["Loop"].as<bool>();
        if (node["PlayOnAwake"])
          c.PlayOnAwake = node["PlayOnAwake"].as<bool>();
        if (node["Spatial"])
          c.Spatial = node["Spatial"].as<bool>();
      });

  DeserializeComponent<CanvasComponent>(
      entityNode, "CanvasComponent", deserializedEntity,
      deserializedEntity.HasCanvas, deserializedEntity.Canvas,
      [](YAML::Node &node, auto &c) {
        if (node["IsScreenSpace"])
          c.IsScreenSpace = node["IsScreenSpace"].as<bool>();
        if (node["IsPixelPerfect"])
          c.IsPixelPerfect = node["IsPixelPerfect"].as<bool>();
        if (node["SortingOrder"])
          c.SortingOrder = node["SortingOrder"].as<int>();
        if (node["Enabled"])
          c.Enabled = node["Enabled"].as<bool>();
      });

  DeserializeComponent<RectTransformComponent>(
      entityNode, "RectTransformComponent", deserializedEntity,
      deserializedEntity.HasRectTransform, deserializedEntity.RectTransform,
      [](YAML::Node &node, auto &c) {
        if (node["AnchorsMin"])
          READ_VEC2(node["AnchorsMin"], c.AnchorsMin);
        if (node["AnchorsMax"])
          READ_VEC2(node["AnchorsMax"], c.AnchorsMax);
        if (node["Pivot"])
          READ_VEC2(node["Pivot"], c.Pivot);
        if (node["Position"])
          READ_VEC2(node["Position"], c.Position);
        if (node["SizeDelta"])
          READ_VEC2(node["SizeDelta"], c.SizeDelta);
        if (node["Rotation"])
          READ_VEC3(node["Rotation"], c.Rotation);
        if (node["Scale"])
          READ_VEC3(node["Scale"], c.Scale);
      });

  DeserializeComponent<ImageComponent>(
      entityNode, "ImageComponent", deserializedEntity,
      deserializedEntity.HasImage, deserializedEntity.Image,
      [](YAML::Node &node, auto &c) {
        if (node["Color"])
          READ_VEC4(node["Color"], c.Color);
        if (node["Visible"])
          c.Visible = node["Visible"].as<bool>();
      });

  DeserializeComponent<ButtonComponent>(
      entityNode, "ButtonComponent", deserializedEntity,
      deserializedEntity.HasButton, deserializedEntity.Button,
      [](YAML::Node &node, auto &c) {
        if (node["NormalColor"])
          READ_VEC4(node["NormalColor"], c.NormalColor);
        if (node["HoverColor"])
          READ_VEC4(node["HoverColor"], c.HoverColor);
        if (node["ClickedColor"])
          READ_VEC4(node["ClickedColor"], c.ClickedColor);
      });

  DeserializeComponent<TextComponent>(
      entityNode, "TextComponent", deserializedEntity,
      deserializedEntity.HasText, deserializedEntity.Text,
      [](YAML::Node &node, auto &c) {
        if (node["TextString"])
          c.TextString = node["TextString"].as<std::string>();
        if (node["Color"])
          READ_VEC4(node["Color"], c.Color);
      });

  return true;
}

void SceneSerializer::Serialize(const std::string &filepath) {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "Scene" << YAML::Value << "Untitled";
  out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
  for (const auto &entity : m_Scene->GetEntities())
    SerializeEntity(out, entity);
  out << YAML::EndSeq;

  out << YAML::Key << "Gravity";
  WRITE_VEC3(m_Scene->GetGravity());
  out << YAML::Key << "SkyColor";
  WRITE_VEC3(m_Scene->GetSkyColor());
  out << YAML::Key << "AmbientColor";
  WRITE_VEC3(m_Scene->GetAmbientColor());
  out << YAML::Key << "AmbientIntensity" << YAML::Value
      << m_Scene->GetAmbientIntensity();

  if (!m_Scene->GetSkyTexturePath().empty()) {
    out << YAML::Key << "SkyTexturePath" << YAML::Value
        << m_Scene->GetSkyTexturePath();
  }

  out << YAML::EndMap;
  std::ofstream fout(filepath);
  fout << out.c_str();
}

std::string SceneSerializer::SerializeToString() {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "Scene" << YAML::Value << "Untitled";
  out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
  for (const auto &entity : m_Scene->GetEntities())
    SerializeEntity(out, entity);
  out << YAML::EndSeq;

  out << YAML::Key << "Gravity";
  WRITE_VEC3(m_Scene->GetGravity());
  out << YAML::Key << "SkyColor";
  WRITE_VEC3(m_Scene->GetSkyColor());
  out << YAML::Key << "AmbientColor";
  WRITE_VEC3(m_Scene->GetAmbientColor());
  out << YAML::Key << "AmbientIntensity" << YAML::Value
      << m_Scene->GetAmbientIntensity();

  if (!m_Scene->GetSkyTexturePath().empty()) {
    out << YAML::Key << "SkyTexturePath" << YAML::Value
        << m_Scene->GetSkyTexturePath();
  }

  out << YAML::EndMap;
  return out.c_str();
}

bool SceneSerializer::DeserializeFromString(const std::string &data) {
  YAML::Node node = YAML::Load(data);
  if (!node["Scene"])
    return false;

  if (m_Scene) {
    m_Scene->Clear();
    if (node["Gravity"]) {
      glm::vec3 gravity;
      READ_VEC3(node["Gravity"], gravity);
      m_Scene->SetGravity(gravity);
    }
    if (node["SkyColor"]) {
      glm::vec3 color;
      READ_VEC3(node["SkyColor"], color);
      m_Scene->SetSkyColor(color);
    }
    if (node["AmbientColor"]) {
      glm::vec3 color;
      READ_VEC3(node["AmbientColor"], color);
      m_Scene->SetAmbientColor(color);
    }
    if (node["AmbientIntensity"]) {
      m_Scene->SetAmbientIntensity(node["AmbientIntensity"].as<float>());
    }
    if (node["SkyTexturePath"]) {
      m_Scene->SetSkyTexturePath(node["SkyTexturePath"].as<std::string>());
    }
  }

  auto entities = node["Entities"];
  if (entities) {
    for (auto entityNode : entities) {
      uint64_t uuid = entityNode["Entity"].as<uint64_t>();
      std::string name =
          entityNode["Name"] ? entityNode["Name"].as<std::string>() : "Entity";

      m_Scene->CreateEntity(name);
      Entity &deserializedEntity = m_Scene->GetEntities().back();

      deserializedEntity.UUID.ID = uuid;

      DeserializeEntity(entityNode, deserializedEntity);

      auto relationshipComponent = entityNode["RelationshipComponent"];
      if (relationshipComponent) {
        deserializedEntity.HasRelationship = true;
        if (relationshipComponent["Parent"]) {
          deserializedEntity.Relationship.Parent =
              relationshipComponent["Parent"].as<uint64_t>();
        }
        if (relationshipComponent["Children"]) {
          auto children = relationshipComponent["Children"];
          for (auto childNode : children) {
            deserializedEntity.Relationship.Children.push_back(
                childNode.as<uint64_t>());
          }
        }
      }

    }
  }
  return true;
}

bool SceneSerializer::Deserialize(const std::string &filepath) {
  std::ifstream stream(filepath);
  std::stringstream strStream;
  strStream << stream.rdbuf();
  YAML::Node data = YAML::Load(strStream.str());
  if (!data["Scene"])
    return false;

  if (m_Scene) {
    m_Scene->Clear();
    if (data["Gravity"]) {
      glm::vec3 gravity;
      READ_VEC3(data["Gravity"], gravity);
      m_Scene->SetGravity(gravity);
    }
    if (data["SkyColor"]) {
      glm::vec3 color;
      READ_VEC3(data["SkyColor"], color);
      m_Scene->SetSkyColor(color);
    }
    if (data["AmbientColor"]) {
      glm::vec3 color;
      READ_VEC3(data["AmbientColor"], color);
      m_Scene->SetAmbientColor(color);
    }
    if (data["AmbientIntensity"]) {
      m_Scene->SetAmbientIntensity(data["AmbientIntensity"].as<float>());
    }
    if (data["SkyTexturePath"]) {
      m_Scene->SetSkyTexturePath(data["SkyTexturePath"].as<std::string>());
    }
  }

  auto entities = data["Entities"];
  if (entities) {
    for (auto entityNode : entities) {
      uint64_t uuid = entityNode["Entity"].as<uint64_t>();

      std::string name =
          entityNode["Name"] ? entityNode["Name"].as<std::string>() : "Entity";

      m_Scene->CreateEntity(name);
      Entity &deserializedEntity = m_Scene->GetEntities().back();

      deserializedEntity.UUID.ID = uuid;

      DeserializeEntity(entityNode, deserializedEntity);

      auto relationshipComponent = entityNode["RelationshipComponent"];
      if (relationshipComponent) {
        deserializedEntity.HasRelationship = true;
        if (relationshipComponent["Parent"]) {
          deserializedEntity.Relationship.Parent =
              relationshipComponent["Parent"].as<uint64_t>();
        }
        if (relationshipComponent["Children"]) {
          auto children = relationshipComponent["Children"];
          for (auto childNode : children) {
            deserializedEntity.Relationship.Children.push_back(
                childNode.as<uint64_t>());
          }
        }
      }

    }
  }
  return true;
}

} // namespace Engine
