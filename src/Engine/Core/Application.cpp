#include "../Renderer/Visibility.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "Application.h"
#include "../Scripts/CameraController.h"
#include "../Scripts/PlayerController.h"

#include <GL/glew.h>
#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <limits>
#include <memory>

#include "../Audio/AudioEngine.h"
#include "../Renderer/Buffer.h"
#include "../Renderer/Framebuffer.h"
#include "../Renderer/PlanarReflection.h"
#include "../Renderer/RenderState.h"
#include "../Renderer/RenderSettings.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Renderer2D.h"
#include "../Renderer/Shader.h"
#include "../Renderer/Texture.h"
#include "../Renderer/VertexArray.h"

#include <ImGuizmo.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "../Project/ProjectLoader.h"
#include "../Project/ProjectSerializer.h"
#include "../Project/Project.h"
#include "../Project/AssetRegistry.h"
#include "../Project/ScriptBuilder.h"
#include "../Scene/SceneManager.h"
#include "../Scene/SceneSerializer.h"
#include "../VR/VRRigSystem.h"
#include "Engine/Core/EngineBuildInfo.h"
#include <yaml-cpp/yaml.h>
#include "Input.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Engine {

struct Ray {
  glm::vec3 Origin;
  glm::vec3 Direction;
};

static bool RayIntersectsAABB(const Ray &ray, const glm::vec3 &min,
                              const glm::vec3 &max, float &t) {
  float tmin = 0.0f;
  float tmax = std::numeric_limits<float>::max();

  for (int axis = 0; axis < 3; axis++) {
    float origin = ray.Origin[axis];
    float direction = ray.Direction[axis];
    float axisMin = min[axis];
    float axisMax = max[axis];

    if (std::abs(direction) < 0.000001f) {
      if (origin < axisMin || origin > axisMax)
        return false;
      continue;
    }

    float invDirection = 1.0f / direction;
    float t1 = (axisMin - origin) * invDirection;
    float t2 = (axisMax - origin) * invDirection;
    if (t1 > t2)
      std::swap(t1, t2);

    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);
    if (tmin > tmax)
      return false;
  }

  t = tmin;
  return true;
}

static bool RayIntersectsEntity(Scene &scene, const Ray &ray, Entity &entity,
                                float &t) {
  if (entity.IsUIElement())
    return false;

  if (entity.HasMeshCollider && !entity.MeshCollider.Convex)
    return false;

  glm::vec3 boundsMin(-0.5f);
  glm::vec3 boundsMax(0.5f);
  bool pickable = entity.HasSpriteRenderer || entity.HasCamera ||
                  entity.HasLight || entity.HasBoxCollider;

  if (entity.HasMeshRenderer && entity.MeshRenderer.Mesh) {
    boundsMin = entity.MeshRenderer.Mesh->GetAABBMin();
    boundsMax = entity.MeshRenderer.Mesh->GetAABBMax();
    pickable = true;
  } else if (entity.HasBoxCollider) {
    glm::vec3 halfSize = entity.BoxCollider.Size * 0.5f;
    boundsMin = entity.BoxCollider.Offset - halfSize;
    boundsMax = entity.BoxCollider.Offset + halfSize;
  }

  if (!pickable)
    return false;

  glm::mat4 model = scene.GetWorldTransform(entity);
  glm::mat4 invModel = glm::inverse(model);

  glm::vec4 localOrigin = invModel * glm::vec4(ray.Origin, 1.0f);
  glm::vec4 localDir = invModel * glm::vec4(ray.Direction, 0.0f);

  Ray localRay = {glm::vec3(localOrigin), glm::vec3(localDir)};
  float localT = 0.0f;
  if (!RayIntersectsAABB(localRay, boundsMin, boundsMax, localT))
    return false;

  glm::vec3 localHit = localRay.Origin + localRay.Direction * localT;
  glm::vec3 worldHit = glm::vec3(model * glm::vec4(localHit, 1.0f));
  t = glm::dot(worldHit - ray.Origin, ray.Direction);
  return t >= 0.0f;
}

namespace {
uint32_t Crc32(uint32_t crc, const uint8_t *data, size_t len) {
  static uint32_t table[256];
  static bool tableReady = false;
  if (!tableReady) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
      table[i] = c;
    }
    tableReady = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < len; ++i)
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

// Minimal PNG writer (stored, uncompressed deflate blocks) for screenshots.
bool WritePNG(const std::string &path, const std::vector<uint8_t> &rgb,
              int width, int height) {
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(width * 3 + 1) * height);
  for (int y = height - 1; y >= 0; --y) {  // GL rows are bottom-up
    raw.push_back(0);
    const uint8_t *row = rgb.data() + (size_t)y * width * 3;
    raw.insert(raw.end(), row, row + (size_t)width * 3);
  }

  std::vector<uint8_t> zlib = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t pos = 0; pos < raw.size();) {
    size_t len = std::min<size_t>(65535, raw.size() - pos);
    bool last = pos + len == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back((uint8_t)(len & 0xFF));
    zlib.push_back((uint8_t)(len >> 8));
    zlib.push_back((uint8_t)(~len & 0xFF));
    zlib.push_back((uint8_t)((~len >> 8) & 0xFF));
    zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + len);
    pos += len;
  }
  uint32_t adler = (b << 16) | a;
  for (int shift = 24; shift >= 0; shift -= 8)
    zlib.push_back((uint8_t)(adler >> shift));

  std::ofstream out(path, std::ios::binary);
  if (!out)
    return false;
  auto writeU32 = [&](uint32_t v) {
    uint8_t bytes[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8),
                        (uint8_t)v};
    out.write((const char *)bytes, 4);
  };
  auto writeChunk = [&](const char *type, const std::vector<uint8_t> &data) {
    writeU32((uint32_t)data.size());
    out.write(type, 4);
    if (!data.empty())
      out.write((const char *)data.data(), (std::streamsize)data.size());
    uint32_t crc = Crc32(0, (const uint8_t *)type, 4);
    if (!data.empty())
      crc = Crc32(crc, data.data(), data.size());
    writeU32(crc);
  };

  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out.write((const char *)signature, 8);
  std::vector<uint8_t> header = {
      (uint8_t)(width >> 24),  (uint8_t)(width >> 16),  (uint8_t)(width >> 8),
      (uint8_t)width,          (uint8_t)(height >> 24), (uint8_t)(height >> 16),
      (uint8_t)(height >> 8),  (uint8_t)height,         8, 2, 0, 0, 0};
  writeChunk("IHDR", header);
  writeChunk("IDAT", zlib);
  writeChunk("IEND", {});
  return (bool)out;
}
}

Application *Application::s_Instance = nullptr;

Application::Application(int argc, char** argv) {
  s_Instance = this;
  bool requestVR = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--vr") requestVR = true;
  WindowProps windowProps("Engine", 1280, 720);
  windowProps.VR = requestVR;
#ifdef GAME_MODE_RUNTIME
  windowProps.Title = "Game (Runtime)";
  m_Window = new Window(windowProps);
  m_AppState = AppState::Runtime;
#else
  m_Window = new Window(windowProps);
  m_AppState = AppState::ProjectHub;
#endif
  std::cout << "[App] Starting..." << std::endl;
  Renderer::Init();
  Renderer2D::Init();
  AudioEngine::Init();

  if (requestVR && !m_VRSystem.Init(m_Window->GetNativeWindow(), m_Window->GetContext()))
    std::cerr << "[VR] Could not start VR. Connect the headset and select an OpenXR runtime; continuing on desktop.\n";
#ifndef GAME_MODE_RUNTIME
  try {
    LoadRecentScenes();
  } catch (const std::exception &e) {
    std::cerr << "ERR: Failed to load recent scenes: " << e.what() << std::endl;
    std::ofstream out("imgui.recent");
    out.close();
  }
#endif

  m_Camera = EditorCamera(30.0f, 1778.0f, 0.1f, 1000.0f);
  m_Scene = std::make_shared<Scene>();
  m_SceneHierarchyPanel.SetContext(m_Scene);
  m_SceneHierarchyPanel.SetHistoryCallback([this]() { SaveHistoryState(); });
  m_Grid = new EditorGrid();

#ifndef GAME_MODE_RUNTIME
  auto mPlayerEntity = m_Scene->CreateEntity("Player");
  mPlayerEntity.Transform.Translation = {0.0f, 2.0f, 0.0f};
  mPlayerEntity.AddScript<PlayerController>("PlayerController");

  auto mFloorEntity = m_Scene->CreateEntity();
  mFloorEntity.Transform.Translation = {2.0f, 0.0f, 0.0f};

  mPlayerEntity.HasCamera = true;
  mPlayerEntity.Camera.Primary = true;
  mPlayerEntity.HasRigidBody = true;
  mPlayerEntity.RigidBody.Type = RigidBodyComponent::BodyType::Dynamic;
  mPlayerEntity.RigidBody.FixedRotation = true;

  mFloorEntity.Transform.Translation = {0.0f, -2.0f, 0.0f};
  mFloorEntity.Transform.Scale = {10.0f, 1.0f, 10.0f};
  mFloorEntity.BoxCollider.Size = {5.0f, 0.5f, 5.0f};
  mFloorEntity.HasRigidBody = true;
  mFloorEntity.RigidBody.Type = RigidBodyComponent::BodyType::Static;

  m_Scene->UpdateEntity(mPlayerEntity);
  m_Scene->UpdateEntity(mFloorEntity);

  m_Scene->OnPhysicsStart();
#endif

#ifdef GAME_MODE_RUNTIME

    std::filesystem::path projectPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--vr") continue;
        projectPath = argv[i];
        break;
    }
    if (projectPath.empty()) {
        std::filesystem::path exeDir = std::filesystem::path(argv[0]).parent_path();
        if (exeDir.empty()) exeDir = std::filesystem::current_path();
        std::vector<std::filesystem::path> candidates;
        for (const auto& e : std::filesystem::directory_iterator(exeDir)) {
            if (e.is_regular_file() && e.path().extension() == ".myproject")
                candidates.push_back(e.path());
        }
        if (candidates.size() != 1) {
            std::string msg = candidates.empty()
                ? "no .myproject next to " + exeDir.string()
                : "multiple .myproject files next to " + exeDir.string();
            throw std::runtime_error(msg);
        }
        projectPath = candidates.front();
    }

    auto pr = ProjectLoader::LoadFromFile(projectPath, m_Project);
    if (!pr.Ok) throw std::runtime_error("failed to load project: " + pr.Error);

    auto modPath = m_Project.ProjectRoot / m_Project.GameModulePath;
    auto mr = m_GameModule.Load(modPath);
    if (!mr.Ok) throw std::runtime_error("failed to load game module: " + mr.Error);

    SceneManager::SetProject(&m_Project);
    {
        // Scene assets (meshes, textures) are stored relative to the project root.
        std::error_code ec;
        std::filesystem::current_path(m_Project.ProjectRoot, ec);
        if (ec) std::cerr << "[Runtime] chdir failed: " << ec.message() << "\n";
    }
    if (!SceneManager::Load(m_Project.StartupSceneName))
        throw std::runtime_error("cannot queue startup scene: " + SceneManager::GetLastError());
    auto pending = SceneManager::ConsumePendingLoad();
    const SceneEntry* entry = m_Project.FindScene(*pending);
    SceneSerializer ser(m_Scene);
    auto scenePath = (m_Project.ProjectRoot / entry->RelativePath).string();
    if (!ser.Deserialize(scenePath))
        throw std::runtime_error("failed to deserialize startup scene: " + entry->RelativePath);
    m_CurrentScenePath = scenePath;
    m_Scene->OnPhysicsStart();
#else
    // "Engine.exe <file.myproject> [--play]" opens a project without the hub.
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path arg = argv[i];
        if (arg.extension() != ".myproject") continue;
        std::error_code ec;
        auto file = std::filesystem::absolute(arg, ec);
        if (!OpenProject(ec ? arg : file)) {
            std::cerr << "[App] could not open " << arg.string() << ": "
                      << m_LastProjectError << "\n";
        } else if (i + 1 < argc && std::string(argv[i + 1]) == "--play") {
            TogglePlayMode();
        }
        break;
    }
#endif
}

Application::~Application() {
  m_VRSystem.Shutdown();
  AudioEngine::Shutdown();
  Renderer2D::Shutdown();
  delete m_Window;
}

void Application::LoadScene(const std::string &path) { m_NextScenePath = path; }

void Application::Run() {
  float vertices[] = {

      -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, -0.5f, 0.5f, 0.0f,
      0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
      -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,

      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.5f, -0.5f, -0.5f,
      0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f,
      1.0f, -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,

      -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
      1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
      -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

      -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.5f,
      0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
      1.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,

      0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
      0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

      -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.5f, -0.5f, -0.5f,
      -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f};

  std::shared_ptr<VertexBuffer> vertexBuffer;
  vertexBuffer.reset(new VertexBuffer(vertices, sizeof(vertices)));

  BufferLayout layout = {{GL_FLOAT, 3, "a_Position"},
                         {GL_FLOAT, 3, "a_Normal"},
                         {GL_FLOAT, 2, "a_TexCoord"}};
  vertexBuffer->SetLayout(layout);

  uint32_t indices[] = {
      0,  1,  2,  2,  3,  0,
      4,  5,  6,  6,  7,  4,
      8,  9,  10, 10, 11, 8,
      12, 13, 14, 14, 15, 12,
      16, 17, 18, 18, 19, 16,
      20, 21, 22, 22, 23, 20
  };
  std::shared_ptr<IndexBuffer> indexBuffer;
  indexBuffer.reset(
      new IndexBuffer(indices, sizeof(indices) / sizeof(uint32_t)));

  std::shared_ptr<VertexArray> vertexArray;
  vertexArray.reset(new VertexArray());
  vertexArray->AddVertexBuffer(vertexBuffer);
  vertexArray->SetIndexBuffer(indexBuffer);
  m_VertexArray = vertexArray;

  float backdropVertices[] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                              1.0f,  -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
                              1.0f,  1.0f,  0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                              -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};

  uint32_t backdropIndices[] = {0, 1, 2, 2, 3, 0};

  std::shared_ptr<VertexArray> backdropVAO = std::make_shared<VertexArray>();
  std::shared_ptr<VertexBuffer> backdropVBO = std::make_shared<VertexBuffer>(
      backdropVertices, sizeof(backdropVertices));
  backdropVBO->SetLayout(layout);
  backdropVAO->AddVertexBuffer(backdropVBO);
  std::shared_ptr<IndexBuffer> backdropIBO = std::make_shared<IndexBuffer>(
      backdropIndices, sizeof(backdropIndices) / sizeof(uint32_t));
  backdropVAO->SetIndexBuffer(backdropIBO);
  m_BackdropVAO = backdropVAO;

  std::string vertexSrc = R"(
    #version 330 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in vec2 a_TexCoord;

    uniform mat4 u_ViewProjection;
    uniform mat4 u_Transform;
    uniform mat4 u_LightSpaceMatrix;

    out vec3 v_Normal;
    out vec3 v_FragPos;
    out vec2 v_TexCoord;
    out vec4 v_LightSpacePos;

    void main() {
        v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
        v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;
        v_TexCoord = a_TexCoord;
        v_LightSpacePos = u_LightSpaceMatrix * vec4(v_FragPos, 1.0);
        gl_Position = u_ViewProjection * vec4(v_FragPos, 1.0);
    }
  )";

  std::string fragmentSrc = R"(
    #version 330 core
    layout(location = 0) out vec4 color;
    layout(location = 1) out vec4 gbuf;   // xyz = view-space normal, w = metallic

    in vec3 v_Normal;
    in vec3 v_FragPos;
    in vec2 v_TexCoord;
    in vec4 v_LightSpacePos;

    // Camera position — declared here so CSM code below can use it.
    uniform vec3 u_ViewPos;

    // CSM — 3 cascades.
    uniform sampler2D u_ShadowMap0;
    uniform sampler2D u_ShadowMap1;
    uniform sampler2D u_ShadowMap2;
    uniform mat4 u_LightSpace0;
    uniform mat4 u_LightSpace1;
    uniform mat4 u_LightSpace2;
    uniform float u_CsmSplit0; // view-space z thresholds
    uniform float u_CsmSplit1;
    uniform float u_CsmSplit2;

    uniform int u_ShadowsEnabled;
    uniform int u_ReceiveShadows;
    uniform float u_ShadowBiasScale;
    uniform int u_ShadowPCFRadius;
    uniform float u_MaterialSSRIntensity;

    float SampleCascade(sampler2D sm, mat4 lightSpace, vec3 worldPos,
                        vec3 normal, vec3 lightDir) {
        vec4 lp = lightSpace * vec4(worldPos, 1.0);
        vec3 p = lp.xyz / lp.w;
        p = p * 0.5 + 0.5;
        if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
            return -1.0;   // outside this cascade
        vec2 texel = 1.0 / vec2(textureSize(sm, 0));
        // Compare each PCF tap against the receiver plane at that tap. A single
        // depth for the whole kernel produces stripes on sloped surfaces.
        vec3 dx = dFdx(p), dy = dFdy(p);
        float det = dx.x * dy.y - dx.y * dy.x;
        vec2 slope = abs(det) > 1e-10
            ? vec2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / det
            : vec2(0.0);
        slope = clamp(slope, vec2(-10.0), vec2(10.0));
        float texelSlope = dot(abs(slope), texel);
        float bias = (0.25 * max(texel.x, texel.y) + min(0.5 * texelSlope, 0.002)) *
                     u_ShadowBiasScale;
        float shadow = 0.0;
        int radius = clamp(u_ShadowPCFRadius, 0, 4);
        int samples = 0;
        for (int x = -4; x <= 4; ++x)
            for (int y = -4; y <= 4; ++y) {
                if (abs(x) > radius || abs(y) > radius) continue;
                vec2 offset = vec2(x, y) * texel;
                float d = texture(sm, p.xy + offset).r;
                shadow += (p.z + dot(slope, offset) - bias > d) ? 1.0 : 0.0;
                samples++;
            }
        return samples > 0 ? shadow / float(samples) : 0.0;
    }

    float ShadowCalculation(vec3 normal, vec3 lightDir) {
        // Pick cascade by distance from camera (view-space depth).
        // A fragment outside its cascade's map falls through to the next one.
        float viewDist = length(u_ViewPos - v_FragPos);
        float shadow = -1.0;
        if (viewDist < u_CsmSplit0)
            shadow = SampleCascade(u_ShadowMap0, u_LightSpace0, v_FragPos,
                                   normal, lightDir);
        if (shadow < 0.0 && viewDist < u_CsmSplit1)
            shadow = SampleCascade(u_ShadowMap1, u_LightSpace1, v_FragPos,
                                   normal, lightDir);
        if (shadow < 0.0 && viewDist < u_CsmSplit2)
            shadow = SampleCascade(u_ShadowMap2, u_LightSpace2, v_FragPos,
                                   normal, lightDir);
        return max(shadow, 0.0);
    }

    uniform vec3 u_SunDirection;
    uniform vec3 u_SunColor;
    uniform float u_SunIntensity;

    uniform vec3 u_AmbientColor;
    uniform float u_AmbientIntensity;
    uniform int u_LinearMaterialColors;
    uniform float u_MinRoughness;
    uniform vec2 u_AmbientHemisphere;

    #define MAX_POINT_LIGHTS 8
    uniform int u_PointLightCount;
    uniform vec3 u_PointLightPositions[MAX_POINT_LIGHTS];
    uniform vec3 u_PointLightColors[MAX_POINT_LIGHTS];
    uniform float u_PointLightIntensities[MAX_POINT_LIGHTS];
    uniform float u_PointLightRadii[MAX_POINT_LIGHTS];

    #define MAX_SPOT_LIGHTS 4
    uniform int   u_SpotLightCount;
    uniform vec3  u_SpotPositions[MAX_SPOT_LIGHTS];
    uniform vec3  u_SpotDirections[MAX_SPOT_LIGHTS];
    uniform vec3  u_SpotColors[MAX_SPOT_LIGHTS];
    uniform float u_SpotIntensities[MAX_SPOT_LIGHTS];
    uniform float u_SpotRadii[MAX_SPOT_LIGHTS];
    uniform float u_SpotCosInner[MAX_SPOT_LIGHTS];
    uniform float u_SpotCosOuter[MAX_SPOT_LIGHTS];

    #define MAX_AREA_LIGHTS 4
    uniform int   u_AreaLightCount;
    uniform vec3  u_AreaPositions[MAX_AREA_LIGHTS];
    uniform vec3  u_AreaForwards[MAX_AREA_LIGHTS];   // surface normal
    uniform vec3  u_AreaRights[MAX_AREA_LIGHTS];
    uniform vec3  u_AreaUps[MAX_AREA_LIGHTS];
    uniform vec3  u_AreaColors[MAX_AREA_LIGHTS];
    uniform float u_AreaIntensities[MAX_AREA_LIGHTS];
    uniform float u_AreaRadii[MAX_AREA_LIGHTS];
    uniform vec2  u_AreaSizes[MAX_AREA_LIGHTS];

    uniform vec4 u_Color;
    uniform sampler2D u_Texture;
    uniform int u_HasTexture;
    uniform float u_TilingFactor;

    uniform sampler2D u_DiffuseMap;
    uniform int u_HasDiffuseMap;
    uniform sampler2D u_NormalMap;
    uniform int u_HasNormalMap;
    uniform int u_IsUnlit;
    uniform int u_Translucent;   // see-through pass: Color alpha (and the texture's) is kept

    // PBR material parameters.
    uniform float u_Metallic;
    uniform float u_Roughness;
    uniform float u_AO;

    // View matrix (rotation part) for transforming normals to view space for G-buffer.
    uniform mat3 u_ViewRotation;

    // Planar reflection (for ground/mirror entities).
    uniform sampler2D u_PlanarReflection;
    uniform int       u_HasPlanarReflection;
    uniform mat4      u_ViewProjectionPlanar; // reflected camera VP, for projecting fragment to sample UV

    // IBL (optional).
    uniform samplerCube u_IrradianceMap;
    uniform samplerCube u_PrefilterMap;
    uniform sampler2D   u_BRDFLUT;
    uniform int         u_HasIBL;

    const float PI = 3.14159265359;

    float DistributionGGX(vec3 N, vec3 H, float a) {
        float a2 = a*a;
        float NdotH = max(dot(N, H), 0.0);
        float NdotH2 = NdotH*NdotH;
        float d = (NdotH2 * (a2 - 1.0) + 1.0);
        return a2 / (PI * d * d);
    }
    float GeometrySchlickGGX(float NdotV, float k) {
        return NdotV / (NdotV * (1.0 - k) + k);
    }
    float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
        float r = rough + 1.0;
        float k = (r*r) / 8.0;
        return GeometrySchlickGGX(max(dot(N, V), 0.0), k) *
               GeometrySchlickGGX(max(dot(N, L), 0.0), k);
    }
    vec3 FresnelSchlick(float cosTheta, vec3 F0) {
        return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    }
    vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float rough) {
        return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    }

    void main() {
        if (u_IsUnlit > 0) {
            vec4 texColor = vec4(1.0);
            if (u_HasTexture > 0) texColor = texture(u_Texture, v_TexCoord * u_TilingFactor);
            if (u_HasDiffuseMap > 0) texColor *= texture(u_DiffuseMap, v_TexCoord * u_TilingFactor);
            color = u_Color * texColor;
            gbuf = vec4(0.0, 0.0, 1.0, 0.0);   // no reflection on unlit
            return;
        }

        vec3 N = normalize(v_Normal);
        if (u_HasNormalMap > 0) {
            vec3 n = texture(u_NormalMap, v_TexCoord * u_TilingFactor).rgb;
            n = normalize(n * 2.0 - 1.0);
            N = normalize(N + n * 0.5);
        }

        vec4 texColor = vec4(1.0);
        if (u_HasTexture > 0) texColor = texture(u_Texture, v_TexCoord * u_TilingFactor);
        if (u_HasDiffuseMap > 0) texColor *= texture(u_DiffuseMap, v_TexCoord * u_TilingFactor);
        vec3 albedo = (u_Color.rgb * texColor.rgb);

        if (u_LinearMaterialColors > 0) {
            vec3 bounded = clamp(albedo, 0.0, 1.0);
            albedo = mix(bounded / 12.92, pow((bounded + 0.055) / 1.055, vec3(2.4)),
                         step(vec3(0.04045), bounded)) + max(albedo - 1.0, 0.0);
        }
        float metallic  = clamp(u_Metallic, 0.0, 1.0);
        float roughness = clamp(u_Roughness, u_MinRoughness, 1.0);

        vec3 V = normalize(u_ViewPos - v_FragPos);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        vec3 Lo = vec3(0.0);

        // Directional (sun).
        {
            vec3 L = normalize(-u_SunDirection);
            vec3 H = normalize(V + L);
            vec3 radiance = u_SunColor * u_SunIntensity;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 num = NDF * G * F;
            float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = num / denom;

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            float NdotL = max(dot(N, L), 0.0);
            float shadow = 0.0;
            if (u_ShadowsEnabled > 0 && u_ReceiveShadows > 0) shadow = ShadowCalculation(N, L);
            Lo += (1.0 - shadow) * (kD * albedo / PI + specular) * radiance * NdotL;
        }

        // Point lights.
        for (int i = 0; i < u_PointLightCount; ++i) {
            vec3 L = u_PointLightPositions[i] - v_FragPos;
            float dist = length(L);
            if (dist >= u_PointLightRadii[i]) continue;
            L /= dist;
            vec3 H = normalize(V + L);
            float att = 1.0 - (dist / u_PointLightRadii[i]);
            att *= att;
            vec3 radiance = u_PointLightColors[i] * u_PointLightIntensities[i] * att;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 num = NDF * G * F;
            float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = num / denom;

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }

        // Spot lights — like point lights but multiplied by cone falloff.
        for (int i = 0; i < u_SpotLightCount; ++i) {
            vec3 L = u_SpotPositions[i] - v_FragPos;
            float dist = length(L);
            if (dist >= u_SpotRadii[i]) continue;
            L /= dist;
            vec3 spotForward = normalize(-u_SpotDirections[i]); // points TOWARD the light's beam target
            float cosTheta = dot(-L, spotForward);
            if (cosTheta < u_SpotCosOuter[i]) continue;
            float cone = clamp((cosTheta - u_SpotCosOuter[i]) /
                               max(u_SpotCosInner[i] - u_SpotCosOuter[i], 0.001), 0.0, 1.0);
            cone = cone * cone * (3.0 - 2.0 * cone); // smoothstep

            float att = 1.0 - (dist / u_SpotRadii[i]);
            att *= att;

            vec3 H = normalize(V + L);
            vec3 radiance = u_SpotColors[i] * u_SpotIntensities[i] * att * cone;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3 num = NDF * G * F;
            float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = num / denom;
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }

        // Area lights — analytic rectangular light, closest point on rect approximation.
        for (int i = 0; i < u_AreaLightCount; ++i) {
            vec3 P = u_AreaPositions[i];
            vec3 nrm = u_AreaForwards[i];
            vec3 right = u_AreaRights[i];
            vec3 up = u_AreaUps[i];
            float halfX = u_AreaSizes[i].x * 0.5;
            float halfY = u_AreaSizes[i].y * 0.5;
            // Project fragment onto rect plane + clamp to rect bounds.
            vec3 toFrag = v_FragPos - P;
            float localX = clamp(dot(toFrag, right), -halfX, halfX);
            float localY = clamp(dot(toFrag, up),    -halfY, halfY);
            vec3 closestPoint = P + right * localX + up * localY;
            vec3 L = closestPoint - v_FragPos;
            float dist = length(L);
            if (dist >= u_AreaRadii[i]) continue;
            L /= dist;
            // Area front-face check — only emits if fragment is in front of the panel.
            float NdotAreaN = max(dot(-L, nrm), 0.0);
            if (NdotAreaN <= 0.0) continue;

            vec3 H = normalize(V + L);
            float att = 1.0 - (dist / u_AreaRadii[i]);
            att = att * att;
            vec3 radiance = u_AreaColors[i] * u_AreaIntensities[i] * att * NdotAreaN;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3 num = NDF * G * F;
            float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = num / denom;
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }

        // Image-Based Lighting.
        vec3 ambient;
        if (u_HasIBL > 0) {
            // Full cubemap-based IBL (activated once skybox is precomputed).
            vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
            vec3 kS = F;
            vec3 kD = (1.0 - kS) * (1.0 - metallic);
            vec3 irradiance = texture(u_IrradianceMap, N).rgb;
            vec3 diffuseIBL = irradiance * albedo;

            vec3 R = reflect(-V, N);
            const float MAX_REFLECTION_LOD = 5.0;
            vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
            vec2 envBRDF = texture(u_BRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
            vec3 specularIBL = prefiltered * (F * envBRDF.x + envBRDF.y);

            ambient = (kD * diffuseIBL + specularIBL) * u_AO;
        } else {
            // Analytical pseudo-IBL: hemisphere approximation.
            // Sky (bluish toward up), ground (warm/dark toward down) — physically plausible
            // ambient term that varies with surface normal.
            vec3 skyColor    = u_AmbientColor * u_AmbientIntensity * u_AmbientHemisphere.x;
            vec3 groundColor = u_AmbientColor * u_AmbientIntensity * u_AmbientHemisphere.y;
            float hemi = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
            vec3 irradiance = mix(groundColor, skyColor, hemi);

            // Fresnel-modulated diffuse/specular split.
            vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
            vec3 kD = (1.0 - F) * (1.0 - metallic);

            vec3 diffuse = kD * albedo * irradiance;
            // Analytical specular reflection of the sky: attenuated by roughness,
            // boosted by Fresnel for glancing angles (gives rim light on metals).
            vec3 reflectionCol = mix(skyColor, groundColor,
                                     clamp(-reflect(V, N).y * 0.5 + 0.5, 0.0, 1.0));
            vec3 specular = F * reflectionCol * (1.0 - roughness);

            ambient = (diffuse + specular) * u_AO;
        }

        vec3 result = ambient + Lo;

        // Planar reflection: sample pre-rendered mirror buffer using projected UV.
        if (u_HasPlanarReflection > 0) {
            vec4 clip = u_ViewProjectionPlanar * vec4(v_FragPos, 1.0);
            vec2 ruv = (clip.xy / clip.w) * 0.5 + 0.5;
            if (ruv.x > 0.0 && ruv.x < 1.0 && ruv.y > 0.0 && ruv.y < 1.0) {
                vec3 refl = texture(u_PlanarReflection, ruv).rgb;
                // Blend weight from Fresnel + user-tunable strength (the
                // material's SSR intensity, when set, scales the mirror too).
                float NdotV = max(dot(N, V), 0.0);
                float fresnel = pow(1.0 - NdotV, 4.0);
                float strength = u_MaterialSSRIntensity > 0.0 ? min(u_MaterialSSRIntensity, 1.0) : 1.0;
                float w = mix(mix(0.4, 0.9, fresnel), 0.98, metallic) * strength;
                result = mix(result, refl, w);
            }
        }

        // Force opaque output — scene has GL_BLEND enabled for other passes,
        // so we shouldn't leak partial alpha from materials into opaque meshes.
        // Only the see-through pass (glass) keeps the material's alpha.
        color = vec4(result, u_Translucent > 0 ? u_Color.a * texColor.a : 1.0);

        // G-buffer for SSR: view-space normal + reflectivity factor.
        // Rough surfaces still get a small SSR contribution so the toggle remains visible,
        // while very rough terrain is kept low enough to avoid screen-space smearing.
        vec3 viewN = normalize(u_ViewRotation * N);
        float gloss = 1.0 - roughness;
        float dielectricReflectivity = (0.04 + gloss * gloss * 0.24) *
                                      (1.0 - smoothstep(0.70, 1.0, roughness));
        float metalReflectivity = metallic * (0.65 + gloss * 0.35);
        float reflectivity = clamp(max(metalReflectivity, dielectricReflectivity) *
                                   u_MaterialSSRIntensity, 0.0, 1.0);
        if (u_HasPlanarReflection > 0) reflectivity *= 0.6;
        gbuf = vec4(viewN, reflectivity);
    }
  )";

  std::shared_ptr<Shader> shader;

  shader.reset(new Shader(vertexSrc, fragmentSrc));
  // Set every sampler before the first draw, even when its feature is disabled.
  // Leaving cubemaps at unit 0 aliases sampler2D and invalidates draws on Mesa.
  shader->Bind();
  shader->UploadUniformInt("u_Texture", 0);
  shader->UploadUniformInt("u_DiffuseMap", 1);
  shader->UploadUniformInt("u_NormalMap", 2);
  shader->UploadUniformInt("u_ShadowMap0", 3);
  shader->UploadUniformInt("u_IrradianceMap", 4);
  shader->UploadUniformInt("u_PrefilterMap", 5);
  shader->UploadUniformInt("u_BRDFLUT", 6);
  shader->UploadUniformInt("u_PlanarReflection", 7);
  shader->UploadUniformInt("u_ShadowMap1", 10);
  shader->UploadUniformInt("u_ShadowMap2", 11);
  shader->Unbind();

  FramebufferSpecification fbSpec;
  fbSpec.Width = 1280;
  fbSpec.Height = 720;
  fbSpec.Samples = 4;
  fbSpec.Format = FramebufferFormat::RGBA16F;
  fbSpec.ExtraColorAttachments = { FramebufferFormat::RGBA16F };
  fbSpec.HasDepth = true;
  fbSpec.DepthAsTexture = false;
  std::shared_ptr<Framebuffer> framebuffer;
  framebuffer.reset(Framebuffer::Create(fbSpec));

  FramebufferSpecification resolveSpec = fbSpec;
  resolveSpec.Samples = 1;
  resolveSpec.DepthAsTexture = true;
  std::shared_ptr<Framebuffer> resolveFB;
  resolveFB.reset(Framebuffer::Create(resolveSpec));

  FramebufferSpecification ssaoSpec;
  ssaoSpec.Width = fbSpec.Width;
  ssaoSpec.Height = fbSpec.Height;
  ssaoSpec.Samples = 1;
  ssaoSpec.Format = FramebufferFormat::RGBA8;
  ssaoSpec.HasDepth = false;
  std::shared_ptr<Framebuffer> ssaoFB, ssaoBlurFB;
  ssaoFB.reset(Framebuffer::Create(ssaoSpec));
  ssaoBlurFB.reset(Framebuffer::Create(ssaoSpec));

  FramebufferSpecification ldrSpec = fbSpec;
  ldrSpec.Samples = 1;
  ldrSpec.Format = FramebufferFormat::RGBA8;
  ldrSpec.ExtraColorAttachments.clear();
  ldrSpec.HasDepth = true; // World-space UI shares the resolved scene depth.
  std::shared_ptr<Framebuffer> ldrFB;
  ldrFB.reset(Framebuffer::Create(ldrSpec));

  std::shared_ptr<Framebuffer> prevFrameFB;
  ldrSpec.HasDepth = false;
  prevFrameFB.reset(Framebuffer::Create(ldrSpec));

  FramebufferSpecification ssrSpec;
  ssrSpec.Width = fbSpec.Width;
  ssrSpec.Height = fbSpec.Height;
  ssrSpec.Samples = 1;
  ssrSpec.Format = FramebufferFormat::RGBA16F;
  ssrSpec.HasDepth = false;
  std::shared_ptr<Framebuffer> ssrFB;
  ssrFB.reset(Framebuffer::Create(ssrSpec));

  FramebufferSpecification planarSpec;
  planarSpec.Width = fbSpec.Width / 2;
  planarSpec.Height = fbSpec.Height / 2;
  planarSpec.Samples = 1;
  planarSpec.Format = FramebufferFormat::RGBA16F;
  planarSpec.HasDepth = true;
  std::shared_ptr<Framebuffer> planarFB;
  planarFB.reset(Framebuffer::Create(planarSpec));

  FramebufferSpecification bloomSpec;
  bloomSpec.Width = fbSpec.Width / 2;
  bloomSpec.Height = fbSpec.Height / 2;
  bloomSpec.Samples = 1;
  bloomSpec.Format = FramebufferFormat::RGBA16F;
  bloomSpec.HasDepth = false;
  std::shared_ptr<Framebuffer> bloomA, bloomB;
  bloomA.reset(Framebuffer::Create(bloomSpec));
  bloomB.reset(Framebuffer::Create(bloomSpec));

  FramebufferSpecification shadowSpec;
  shadowSpec.Width = 2048;
  shadowSpec.Height = 2048;
  shadowSpec.Samples = 1;
  shadowSpec.Format = FramebufferFormat::Depth;
  shadowSpec.HasDepth = true;
  shadowSpec.DepthAsTexture = true;
  std::shared_ptr<Framebuffer> shadowFBs[3];
  for (int i = 0; i < 3; ++i)
    shadowFBs[i].reset(Framebuffer::Create(shadowSpec));

  const float csmHalfSize[3]   = { 8.0f, 22.0f, 60.0f };

  const float csmSplitDist[3]  = { 12.0f, 35.0f, 120.0f };

  auto resizeRenderTargets = [&](uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 ||
        (width == fbSpec.Width && height == fbSpec.Height))
      return;
    fbSpec.Width = width;
    fbSpec.Height = height;
    framebuffer->Resize(width, height);
    resolveFB->Resize(width, height);
    ldrFB->Resize(width, height);
    prevFrameFB->Resize(width, height);
    ssaoFB->Resize(width / 2, height / 2);
    ssaoBlurFB->Resize(width / 2, height / 2);
    ssrFB->Resize(width / 2, height / 2);
    planarFB->Resize(std::min(width / 2, 512u), std::min(height / 2, 512u));
    bloomA->Resize(width / 2, height / 2);
    bloomB->Resize(width / 2, height / 2);
  };

  unsigned int fsQuadVAO = 0;
  glGenVertexArrays(1, &fsQuadVAO);

  const char* postVSrc = R"(
    #version 330 core
    out vec2 v_UV;
    void main() {
        // Full-screen triangle.
        vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                      (gl_VertexID == 2) ? 3.0 : -1.0);
        v_UV = p * 0.5 + 0.5;
        gl_Position = vec4(p, 0.0, 1.0);
    }
  )";
  const char* postFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    uniform sampler2D u_Scene;
    uniform sampler2D u_Bloom;
    uniform sampler2D u_SSAO;
    uniform sampler2D u_SSR;      // rgb = reflection color, a = reflection mask
    uniform sampler2D u_Depth;
    uniform sampler2D u_GBuffer;
    uniform int   u_UseSSAO;
    uniform int   u_UseSSR;
    uniform int   u_DebugView;
    uniform int   u_SpatialAA;
    uniform int   u_CleanStereo;
    uniform float u_BloomStrength;
    uniform float u_Exposure;
    uniform float u_Vignette;

    // ---- Filters ----
    uniform float u_ChromaticAberration;  // px offset between R and B channels
    uniform float u_FilmGrain;             // 0-1 noise strength
    uniform float u_Time;                  // animates grain
    uniform float u_FogDensity;            // exp fog density
    uniform vec3  u_FogColor;              // fog tint
    uniform float u_FogStart;              // world distance where fog begins
    uniform int   u_GodRaysEnabled;
    uniform vec2  u_SunScreenPos;          // sun position in UV [0,1], or -1 if behind camera
    uniform float u_GodRaysStrength;
    uniform float u_DOFFocusDepth;         // 0..1 (linearized: near=0, far=1)
    uniform float u_DOFRange;              // how far around focus is sharp (0-0.2 typical)
    uniform float u_DOFStrength;
    uniform float u_LensFlareStrength;     // 0 = off
    uniform float u_MotionBlur;            // 0-1 strength
    uniform float u_TAAStrength;           // 0-1 mix with previous frame (subtle AA)
    uniform sampler2D u_PrevFrame;         // previous LDR frame (for TAA)
    uniform mat4  u_InvViewProjection;     // current inverse VP (reconstruct world pos)
    uniform mat4  u_PrevViewProjection;    // previous VP (project to last frame's UV)

    // --- Color grading ---
    uniform float u_Contrast;    // 1 = neutral
    uniform float u_Saturation;  // 1 = neutral
    uniform float u_Temperature; // -1..+1  (cool..warm)
    uniform float u_Tint;        // -1..+1  (magenta..green)
    uniform vec3  u_Lift;        // shadows shift
    uniform vec3  u_Gamma;       // midtones
    uniform vec3  u_Gain;        // highlights

    // --- Retro look ---
    uniform float u_PixelHeight;     // virtual vertical resolution, 0 = native
    uniform float u_ColorLevels;     // posterize levels per channel, 0 = off
    uniform float u_DitherStrength;  // 0-1 ordered dither
    uniform float u_OutlineStrength; // 0-1 depth-edge darkening
    uniform int   u_FogAffectsSky;   // 0 = fog only touches geometry

    // Camera projection for depth linearization.
    uniform float u_Near;
    uniform float u_Far;

    float LinearizeDepth(float d) {
        float z = d * 2.0 - 1.0;
        return (2.0 * u_Near * u_Far) / (u_Far + u_Near - z * (u_Far - u_Near));
    }
    float NormalizedDepth(float d) {
        return clamp(LinearizeDepth(d) / u_Far, 0.0, 1.0);
    }

    // ACES filmic tone mapping (Narkowicz 2015).
    vec3 ACES(vec3 x) {
        const float a = 2.51; const float b = 0.03;
        const float c = 2.43; const float d = 0.59; const float e = 0.14;
        return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
    }

    vec3 ColorGrade(vec3 c) {
        // Temperature (warm/cool) + tint (magenta/green).
        vec3 warm = vec3(1.0, 0.95, 0.85);
        vec3 cool = vec3(0.85, 0.95, 1.1);
        vec3 tempCol = mix(cool, warm, clamp(u_Temperature * 0.5 + 0.5, 0.0, 1.0));
        if (u_CleanStereo > 0)
            tempCol = u_Temperature >= 0.0 ? mix(vec3(1.0), warm, clamp(u_Temperature, 0.0, 1.0))
                                           : mix(vec3(1.0), cool, clamp(-u_Temperature, 0.0, 1.0));
        c *= tempCol;
        c.g *= 1.0 + u_Tint * 0.15;
        c.r *= 1.0 - u_Tint * 0.05;
        c.b *= 1.0 - u_Tint * 0.05;
        // Lift / Gamma / Gain (ASC-CDL style).
        c = (c * u_Gain) + u_Lift;
        c = pow(max(c, 0.0), 1.0 / max(u_Gamma, vec3(0.01)));
        // Contrast around 0.5.
        c = (c - 0.5) * u_Contrast + 0.5;
        // Saturation.
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(lum), c, u_Saturation);
        return c;
    }

    float Rand(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

    // Subtle lens flare — 4 soft ghosts at fractional positions, only when sun on-screen.
    vec3 LensFlare(vec2 uv, vec2 sunUV) {
        if (sunUV.x < 0.0 || sunUV.x > 1.0 || sunUV.y < 0.0 || sunUV.y > 1.0) return vec3(0.0);
        vec2 diff = sunUV - vec2(0.5);
        vec3 acc = vec3(0.0);
        float ts[4]    = float[](0.35, -0.15, 0.70, 1.10);
        float radii[4] = float[](0.05, 0.025, 0.015, 0.06);
        vec3  tints[4] = vec3[](vec3(1.0, 0.7, 0.5),
                                vec3(0.5, 0.8, 1.0),
                                vec3(1.0, 0.9, 0.6),
                                vec3(0.9, 0.5, 1.0));
        for (int i = 0; i < 4; ++i) {
            vec2 pos = vec2(0.5) + diff * ts[i];
            float r = length(uv - pos);
            float g = smoothstep(radii[i], 0.0, r);
            acc += tints[i] * g * 0.20;
        }
        return acc;
    }

    // Proper god rays: radial blur of sky-mask (depth = far plane) from sun position.
    vec3 GodRays(vec2 uv, vec2 sunUV) {
        if (sunUV.x < -0.5 || sunUV.x > 1.5 || sunUV.y < -0.5 || sunUV.y > 1.5) return vec3(0.0);
        const int STEPS = 50;
        const float DECAY = 0.955;
        const float DENSITY = 1.1;
        const float WEIGHT = 0.045;
        vec2 delta = (uv - sunUV) * (DENSITY / float(STEPS));
        vec2 coord = uv;
        float illum = 1.0;
        float acc = 0.0;
        for (int i = 0; i < STEPS; ++i) {
            coord -= delta;
            if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) break;
            float d = texture(u_Depth, coord).r;
            float skyMask = (d >= 0.9995) ? 1.0 : 0.0;
            acc += skyMask * illum * WEIGHT;
            illum *= DECAY;
        }
        float centerDist = length(uv - sunUV);
        float falloff = exp(-centerDist * 1.5);
        return vec3(1.0, 0.92, 0.75) * acc * falloff;
    }

    // Virtual low-res pixel grid: every fragment inside one "big pixel" samples
    // the same UV, so the whole post chain comes out pixelated.
    vec2 g_PixUV;
    vec2 g_PixGrid;
    vec2 g_PixTexel;
    void SetupPixelGrid() {
        vec2 res = vec2(textureSize(u_Scene, 0));
        g_PixUV = v_UV;
        g_PixGrid = floor(v_UV * res);
        g_PixTexel = 1.0 / res;
        if (u_PixelHeight > 0.5 && u_PixelHeight < res.y) {
            vec2 lowRes = max(floor(vec2(u_PixelHeight * res.x / res.y, u_PixelHeight) + 0.5),
                              vec2(1.0));
            g_PixGrid = floor(v_UV * lowRes);
            g_PixUV = (g_PixGrid + 0.5) / lowRes;
            g_PixTexel = 1.0 / lowRes;
        }
    }

    float Bayer4(vec2 p) {
        const float m[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                    3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        int idx = int(mod(p.x, 4.0)) + int(mod(p.y, 4.0)) * 4;
        return (m[idx] + 0.5) / 16.0;
    }

    // From here on every v_UV is the pixel-grid UV.
    #define v_UV g_PixUV

    void main() {
        SetupPixelGrid();

        // Chromatic aberration (radial).
        vec2 toCenter = v_UV - vec2(0.5);
        float caOffset = u_ChromaticAberration * 0.01;
        vec3 hdr;
        if (caOffset > 0.0001) {
            hdr.r = texture(u_Scene, v_UV - toCenter * caOffset).r;
            hdr.g = texture(u_Scene, v_UV).g;
            hdr.b = texture(u_Scene, v_UV + toCenter * caOffset).b;
        } else {
            hdr = texture(u_Scene, v_UV).rgb;
        }
        // Spatial edge smoothing has no history shared between the eyes.
        if (u_SpatialAA > 0) {
            vec2 pixel = 1.0 / vec2(textureSize(u_Scene, 0));
            vec3 north = texture(u_Scene, v_UV + vec2(0,pixel.y)).rgb;
            vec3 south = texture(u_Scene, v_UV - vec2(0,pixel.y)).rgb;
            vec3 east = texture(u_Scene, v_UV + vec2(pixel.x,0)).rgb;
            vec3 west = texture(u_Scene, v_UV - vec2(pixel.x,0)).rgb;
            vec3 weights = vec3(.299,.587,.114);
            float n=dot(north,weights),s=dot(south,weights),e=dot(east,weights),w=dot(west,weights),c=dot(hdr,weights);
            float low=min(c,min(min(n,s),min(e,w))), high=max(c,max(max(n,s),max(e,w)));
            float contrast=high-low;
            if(contrast>max(.04,high*.12)) {
                float blend=clamp(abs((n+s+e+w)*.25-c)/max(contrast,.001),0.0,1.0)*.45;
                hdr=mix(hdr,(north+south+east+west)*.25,blend);
            }
        }
        vec3 bloom = texture(u_Bloom, v_UV).rgb;

        if (u_DebugView == 1) {
            float d = texture(u_Depth, v_UV).r;
            o_Color = vec4(vec3(d), 1.0);
            return;
        }
        if (u_DebugView == 2) {
            vec3 n = texture(u_GBuffer, v_UV).xyz * 0.5 + 0.5;
            o_Color = vec4(n, 1.0);
            return;
        }
        if (u_DebugView == 3) {
            float r = texture(u_GBuffer, v_UV).w;
            o_Color = vec4(vec3(r), 1.0);
            return;
        }
        if (u_DebugView == 4) {
            vec4 ssr = texture(u_SSR, v_UV);
            o_Color = vec4(mix(ssr.rgb, vec3(ssr.a), 0.35), 1.0);
            return;
        }
        if (u_DebugView == 5) {
            float ao = texture(u_SSAO, v_UV).r;
            o_Color = vec4(vec3(ao), 1.0);
            return;
        }

        // SSR — replace specular reflection with screen-space hit where valid.
        if (u_UseSSR > 0) {
            vec4 ssr = texture(u_SSR, v_UV);
            // mix with hit color weighted by reflection mask (alpha).
            hdr = mix(hdr, ssr.rgb, ssr.a);
        }

        float ao = 1.0;
        if (u_UseSSAO > 0) ao = texture(u_SSAO, v_UV).r;
        hdr *= ao;

        // --- Depth of Field (fake via 9-tap blur mixed by focus distance) ---
        float depth01 = NormalizedDepth(texture(u_Depth, v_UV).r);
        if (u_DOFStrength > 0.001) {
            float focusDist = abs(depth01 - u_DOFFocusDepth);
            float blurAmt = clamp((focusDist - u_DOFRange) * 4.0, 0.0, 1.0) * u_DOFStrength;
            if (blurAmt > 0.01) {
                vec2 texel = 1.0 / vec2(textureSize(u_Scene, 0));
                vec3 b = vec3(0.0);
                const int R = 2;
                float total = 0.0;
                for (int x = -R; x <= R; ++x)
                    for (int y = -R; y <= R; ++y) {
                        float w = exp(-(x*x + y*y) * 0.3);
                        b += texture(u_Scene, v_UV + vec2(x, y) * texel * 3.0).rgb * w;
                        total += w;
                    }
                b /= total;
                hdr = mix(hdr, b, blurAmt);
            }
        }

        // --- Outline: darken the far side of depth discontinuities ---
        if (u_OutlineStrength > 0.001) {
            float dC = LinearizeDepth(texture(u_Depth, v_UV).r);
            float dN = dC;
            dN = min(dN, LinearizeDepth(texture(u_Depth, v_UV + vec2(g_PixTexel.x, 0.0)).r));
            dN = min(dN, LinearizeDepth(texture(u_Depth, v_UV - vec2(g_PixTexel.x, 0.0)).r));
            dN = min(dN, LinearizeDepth(texture(u_Depth, v_UV + vec2(0.0, g_PixTexel.y)).r));
            dN = min(dN, LinearizeDepth(texture(u_Depth, v_UV - vec2(0.0, g_PixTexel.y)).r));
            float edge = smoothstep(0.06, 0.14, (dC - dN) / max(dC, 0.001));
            hdr *= 1.0 - edge * u_OutlineStrength;
        }

        // --- Volumetric exponential-squared fog (smooth falloff, no hard edge) ---
        if (u_FogDensity > 0.0 &&
            (u_FogAffectsSky > 0 || texture(u_Depth, v_UV).r < 0.99999)) {
            float linearD = LinearizeDepth(texture(u_Depth, v_UV).r);
            float x = max(linearD - u_FogStart, 0.0) * u_FogDensity;
            float fogFactor = 1.0 - exp(-x * x);
            hdr = mix(hdr, u_FogColor, clamp(fogFactor, 0.0, 1.0));
        }

        // --- God rays (screen-space radial blur from sun UV) ---
        if (u_GodRaysEnabled > 0 && u_SunScreenPos.x >= 0.0) {
            hdr += GodRays(v_UV, u_SunScreenPos) * u_GodRaysStrength;
        }

        // --- Lens flare (additive) ---
        if (u_LensFlareStrength > 0.0 && u_SunScreenPos.x >= 0.0) {
            hdr += LensFlare(v_UV, u_SunScreenPos) * u_LensFlareStrength;
        }

        // Bloom + exposure.
        hdr += bloom * u_BloomStrength;
        hdr *= u_Exposure;

        // Tone map.
        vec3 mapped = ACES(hdr);

        // Color grade in sRGB-ish space.
        mapped = ColorGrade(mapped);

        // Linear -> sRGB gamma.
        mapped = pow(clamp(mapped, 0.0, 1.0), vec3(1.0/2.2));

        // Vignette.
        vec2 vd = v_UV - vec2(0.5);
        float vv = 1.0 - dot(vd, vd) * u_Vignette;
        mapped *= vv;

        // Film grain.
        if (u_FilmGrain > 0.0) {
            float n = Rand(v_UV * (1.0 + u_Time * 17.0)) - 0.5;
            mapped += n * u_FilmGrain * 0.15;
        }

        // Posterize to a limited palette, with ordered dithering between steps.
        if (u_ColorLevels > 1.5) {
            float steps = u_ColorLevels - 1.0;
            float threshold = (Bayer4(g_PixGrid) - 0.5) * u_DitherStrength;
            mapped = floor(clamp(mapped, 0.0, 1.0) * steps + 0.5 + threshold) / steps;
        }

        // Motion blur — camera reprojection (real, not ghosting trick).
        // Reconstruct world pos from depth, reproject with previous VP,
        // velocity = current UV - previous UV, sample along velocity.
        if (u_MotionBlur > 0.01) {
            float d = texture(u_Depth, v_UV).r;
            if (d < 0.9999) {
                vec4 clip = vec4(v_UV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
                vec4 worldH = u_InvViewProjection * clip;
                vec3 world = worldH.xyz / worldH.w;
                vec4 prevClip = u_PrevViewProjection * vec4(world, 1.0);
                vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                vec2 velocity = (v_UV - prevUV) * u_MotionBlur * 2.0;
                // Clamp velocity to avoid wild smears.
                float vLen = length(velocity);
                if (vLen > 0.03) velocity *= 0.03 / vLen;
                const int TAPS = 6;
                vec3 acc = mapped;
                for (int i = 1; i < TAPS; ++i) {
                    float t = float(i) / float(TAPS - 1);
                    acc += texture(u_PrevFrame, v_UV - velocity * t).rgb;
                }
                mapped = acc / float(TAPS);
            }
        }

        // TAA-lite — same idea, tighter threshold for finer edge smoothing.
        if (u_TAAStrength > 0.01) {
            vec3 prev = texture(u_PrevFrame, v_UV).rgb;
            float diff = length(mapped - prev);
            float mask = 1.0 - smoothstep(0.03, 0.12, diff);
            mapped = mix(mapped, prev, clamp(u_TAAStrength, 0.0, 0.5) * mask);
        }

        o_Color = vec4(mapped, 1.0);
    }
  )";

  std::shared_ptr<Shader> postShader(new Shader(postVSrc, postFSrc));

  const char* brightFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    uniform sampler2D u_Scene;
    uniform float u_Threshold;
    void main() {
        vec3 c = texture(u_Scene, v_UV).rgb;
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float k = max(lum - u_Threshold, 0.0) / max(lum, 1e-4);
        o_Color = vec4(c * k, 1.0);
    }
  )";
  std::shared_ptr<Shader> brightShader(new Shader(postVSrc, brightFSrc));

  const char* blurFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    uniform sampler2D u_Tex;
    uniform vec2 u_Direction; // (1,0) horizontal or (0,1) vertical
    uniform vec2 u_TexelSize;
    void main() {
        float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
        vec3 result = texture(u_Tex, v_UV).rgb * w[0];
        for (int i = 1; i < 5; ++i) {
            vec2 off = u_Direction * u_TexelSize * float(i);
            result += texture(u_Tex, v_UV + off).rgb * w[i];
            result += texture(u_Tex, v_UV - off).rgb * w[i];
        }
        o_Color = vec4(result, 1.0);
    }
  )";
  std::shared_ptr<Shader> blurShader(new Shader(postVSrc, blurFSrc));

  const char* ssaoFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    uniform sampler2D u_Depth;
    uniform mat4 u_Projection;
    uniform mat4 u_InvProjection;
    uniform float u_Radius;
    uniform float u_Bias;
    uniform float u_Strength;
    uniform vec2 u_NoiseScale;

    vec3 ViewPosFromDepth(vec2 uv) {
        float d = texture(u_Depth, uv).r;
        vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        vec4 view = u_InvProjection * clip;
        return view.xyz / view.w;
    }

    uniform sampler2D u_Normals;
    uniform mat3 u_ViewRotation;

    void main() {
        float centerDepth = texture(u_Depth, v_UV).r;
        if (centerDepth >= 0.99999) { o_Color = vec4(1.0); return; }
        vec3 p = ViewPosFromDepth(v_UV);
        vec3 n = texture(u_Normals, v_UV).xyz;
        if (dot(n,n) < 0.25) { o_Color = vec4(1.0); return; }
        n = normalize(transpose(u_ViewRotation) * n);
        vec3 reference = abs(n.y) < 0.95 ? vec3(0,1,0) : vec3(1,0,0);
        vec3 t = normalize(cross(reference,n));
        vec3 b = cross(n,t);
        mat3 tbn = u_ViewRotation * mat3(t,b,n);

        // Fixed world-space samples avoid screen noise swimming with the head.
        const int KERNEL = 12;
        float occlusion = 0.0;
        for (int i = 0; i < KERNEL; ++i) {
            float angle = float(i) * 2.39996323;
            float s = 0.2 + 0.8 * fract(float(i) * 0.6180339);
            vec3 sample = normalize(vec3(cos(angle), sin(angle), 0.6)) * s;
            vec3 samplePos = p + tbn * sample * u_Radius;

            vec4 sp = u_Projection * vec4(samplePos, 1.0);
            if (sp.w <= 0.0)
                continue;
            sp.xyz /= sp.w;
            sp.xy = sp.xy * 0.5 + 0.5;
            if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0)
                continue;

            float sampleSceneDepth = texture(u_Depth, sp.xy).r;
            if (sampleSceneDepth >= 0.9999)
                continue;
            float sampleDepth = ViewPosFromDepth(sp.xy).z;
            float rangeCheck = smoothstep(0.0, 1.0, u_Radius / max(abs(p.z - sampleDepth), 0.0001));
            occlusion += (sampleDepth >= samplePos.z + u_Bias ? 1.0 : 0.0) * rangeCheck;
        }
        occlusion = 1.0 - (occlusion / float(KERNEL)) * u_Strength;
        o_Color = vec4(vec3(occlusion), 1.0);
    }
  )";
  std::shared_ptr<Shader> ssaoShader(new Shader(postVSrc, ssaoFSrc));

  const char* ssaoBlurFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    uniform sampler2D u_SSAO;
    uniform sampler2D u_Depth;
    uniform mat4 u_InvProjection;
    float ViewDepth(vec2 uv) {
        vec4 p = u_InvProjection * vec4(uv*2.0-1.0, texture(u_Depth,uv).r*2.0-1.0,1.0);
        return p.z/p.w;
    }
    void main() {
        vec2 texel = 1.0 / vec2(textureSize(u_SSAO, 0));
        float center = ViewDepth(v_UV), result = 0.0, total = 0.0;
        for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) {
            vec2 uv=clamp(v_UV+vec2(x,y)*texel,vec2(0),vec2(1));
            float weight=exp(-abs(ViewDepth(uv)-center)*40.0) * (x==0?2.0:1.0) * (y==0?2.0:1.0);
            result+=texture(u_SSAO,uv).r*weight; total+=weight;
        }
        o_Color=vec4(vec3(result/max(total,0.0001)),1.0);
    }
  )";
  std::shared_ptr<Shader> ssaoBlurShader(new Shader(postVSrc, ssaoBlurFSrc));

  const char* ssrFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;

    uniform sampler2D u_Scene;
    uniform sampler2D u_Depth;
    uniform sampler2D u_GBuffer;  // xyz = view-space normal, w = reflectivity
    uniform mat4 u_Projection;
    uniform mat4 u_InvProjection;
    uniform float u_Strength;

    vec3 ViewPosFromUV(vec2 uv) {
        float d = texture(u_Depth, uv).r;
        vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        vec4 v = u_InvProjection * clip;
        return v.xyz / v.w;
    }

    vec2 ProjectToUV(vec3 viewPos) {
        vec4 clip = u_Projection * vec4(viewPos, 1.0);
        return (clip.xy / clip.w) * 0.5 + 0.5;
    }

    float Hash(vec2 p) {
        return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
    }

    vec3 SampleReflectionColor(vec2 uv, float blurAmount) {
        vec2 texel = 1.0 / vec2(textureSize(u_Scene, 0));
        vec2 off = texel * blurAmount;
        vec3 c = texture(u_Scene, uv).rgb * 0.50;
        c += texture(u_Scene, uv + vec2( off.x, 0.0)).rgb * 0.125;
        c += texture(u_Scene, uv + vec2(-off.x, 0.0)).rgb * 0.125;
        c += texture(u_Scene, uv + vec2(0.0,  off.y)).rgb * 0.125;
        c += texture(u_Scene, uv + vec2(0.0, -off.y)).rgb * 0.125;
        return c;
    }

    void main() {
        float depth = texture(u_Depth, v_UV).r;
        if (depth >= 0.9999) { o_Color = vec4(0.0); return; }

        vec4 gb = texture(u_GBuffer, v_UV);
        float reflectivity = gb.w;
        if (reflectivity < 0.03) { o_Color = vec4(0.0); return; }

        float nLen = length(gb.xyz);
        if (nLen < 0.1) { o_Color = vec4(0.0); return; }
        vec3 viewN = gb.xyz / nLen;

        vec3 viewPos = ViewPosFromUV(v_UV);
        vec3 V = normalize(-viewPos);
        vec3 R = reflect(-V, viewN);

        // Rays pointing toward/behind the camera produce garbage.
        if (R.z > -0.05) { o_Color = vec4(0.0); return; }

        // Linear march in view space. Keep SSR short and selective; long screen-space
        // rays are the main source of smeared, camera-locked artifacts.
        const int STEPS = 80;
        const float MAX_DIST = 24.0;
        float stepSize = MAX_DIST / float(STEPS);
        float jitter = 0.35;
        vec3 rayPos = viewPos + viewN * 0.05 + R * stepSize * jitter;

        vec2 hitUV = vec2(-1.0);
        vec3 hitVP = vec3(0.0);
        vec3 prevVP = viewPos;
        float hitTravel = 0.0;

        for (int i = 0; i < STEPS; ++i) {
            rayPos += R * stepSize;
            vec2 uv = ProjectToUV(rayPos);
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
            float sceneDepth = texture(u_Depth, uv).r;
            if (sceneDepth >= 0.9999) {
                prevVP = rayPos;
                continue;
            }
            vec3 sceneVP = ViewPosFromUV(uv);
            // Ray is now BEHIND scene surface at this sample.
            if (rayPos.z < sceneVP.z) {
                // Reject thick hits; those are usually background/occluder jumps that smear across screen.
                float thickness = max(0.06, -rayPos.z * 0.018);
                if (sceneVP.z - rayPos.z < thickness) {
                    hitUV = uv;
                    hitVP = rayPos;
                    hitTravel = float(i + 1) * stepSize;
                    break;
                } else {
                    break;
                }
            }
            prevVP = rayPos;
        }

        if (hitUV.x < 0.0) { o_Color = vec4(0.0); return; }

        // Binary-search refinement (5 iterations ~3% accuracy).
        vec3 lo = prevVP;
        vec3 hi = hitVP;
        for (int i = 0; i < 5; ++i) {
            vec3 mid = (lo + hi) * 0.5;
            vec2 uv = ProjectToUV(mid);
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
                break;
            vec3 sceneVP = ViewPosFromUV(uv);
            if (mid.z < sceneVP.z) { hi = mid; hitUV = uv; }
            else                   { lo = mid; }
        }

        // Fresnel — stronger reflection at grazing angles.
        float NdotV = max(dot(viewN, V), 0.0);
        float fresnel = pow(1.0 - NdotV, 5.0);
        float reflectWeight = mix(reflectivity * 0.25, reflectivity, fresnel);

        // Fade near screen borders to hide seams.
        vec2 edge = smoothstep(vec2(0.0), vec2(0.15), hitUV) *
                    smoothstep(vec2(0.0), vec2(0.15), 1.0 - hitUV);
        float edgeFade = edge.x * edge.y;
        float distanceFade = 1.0 - smoothstep(MAX_DIST * 0.25, MAX_DIST, hitTravel);
        float screenTravel = smoothstep(0.015, 0.08, length(hitUV - v_UV));

        vec4 hitGb = texture(u_GBuffer, hitUV);
        float hitNLen = length(hitGb.xyz);
        if (hitNLen < 0.1) { o_Color = vec4(0.0); return; }
        vec3 hitN = hitGb.xyz / hitNLen;
        float normalFade = smoothstep(0.05, 0.35, dot(hitN, -R));

        float blurAmount = mix(2.5, 0.75, clamp(reflectivity, 0.0, 1.0));
        vec3 col = SampleReflectionColor(hitUV, blurAmount);
        float mask = clamp(reflectWeight * edgeFade * distanceFade *
                           screenTravel * normalFade * u_Strength * 0.85,
                           0.0, 0.85);
        o_Color = vec4(col, mask);
    }
  )";
  std::shared_ptr<Shader> ssrShader(new Shader(postVSrc, ssrFSrc));

  const char* planarVSrc = R"(
    #version 330 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in vec2 a_TexCoord;
    uniform mat4 u_ViewProjection;
    uniform mat4 u_Transform;
    out vec3 v_WorldPos;
    out vec3 v_Normal;
    out vec2 v_TexCoord;
    void main() {
        v_WorldPos = vec3(u_Transform * vec4(a_Position, 1.0));
        v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;
        v_TexCoord = a_TexCoord;
        gl_Position = u_ViewProjection * vec4(v_WorldPos, 1.0);
    }
  )";
  const char* planarFSrc = R"(
    #version 330 core
    in vec3 v_WorldPos;
    in vec3 v_Normal;
    in vec2 v_TexCoord;
    out vec4 o_Color;
    uniform vec4  u_Albedo;
    uniform sampler2D u_DiffuseMap;
    uniform int u_HasDiffuseMap;
    uniform int u_LinearMaterialColors;
    uniform vec4 u_ClipPlane;
    uniform int u_PointCount;
    uniform vec4 u_PointPositionRadius[8];
    uniform vec3 u_PointColor[8];
    uniform vec3  u_SunDir;
    uniform vec3  u_SunColor;
    uniform float u_SunIntensity;
    uniform vec3  u_AmbientColor;
    uniform float u_AmbientIntensity;
    void main() {
        // The wall behind a mirror must not hide the reflected room.
        if (dot(u_ClipPlane, vec4(v_WorldPos, 1.0)) < 0.001) discard;
        vec4 albedo = u_Albedo;
        if (u_HasDiffuseMap > 0) albedo *= texture(u_DiffuseMap, v_TexCoord);
        if (albedo.a < 0.5) discard;
        if (u_LinearMaterialColors > 0) {
            vec3 bounded = clamp(albedo.rgb,0.0,1.0);
            albedo.rgb = mix(bounded/12.92,pow((bounded+.055)/1.055,vec3(2.4)),
                             step(vec3(.04045),bounded)) + max(albedo.rgb-1.0,0.0);
        }
        vec3 N = normalize(v_Normal);
        vec3 L = normalize(-u_SunDir);
        float NdotL = max(dot(N, L), 0.0);
        vec3 direct = u_SunColor * u_SunIntensity * NdotL;
        vec3 ambient = u_AmbientColor * u_AmbientIntensity;
        for (int i = 0; i < u_PointCount; ++i) {
            vec3 toLight = u_PointPositionRadius[i].xyz - v_WorldPos;
            float distance = length(toLight);
            float attenuation = max(1.0 - distance / max(u_PointPositionRadius[i].w, 0.001), 0.0);
            direct += u_PointColor[i] * attenuation * attenuation *
                      max(dot(N, toLight / max(distance, 0.001)), 0.0);
        }
        o_Color = vec4(albedo.rgb * (ambient + direct), 1.0);
    }
  )";
  std::shared_ptr<Shader> planarShader(new Shader(planarVSrc, planarFSrc));

  const char* depthVSrc = R"(
    #version 330 core
    layout(location = 0) in vec3 a_Position;
    uniform mat4 u_LightSpaceMatrix;
    uniform mat4 u_Transform;
    void main() {
        gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
    }
  )";
  const char* depthFSrc = R"(
    #version 330 core
    void main() {}
  )";
  std::shared_ptr<Shader> depthShader(new Shader(depthVSrc, depthFSrc));

  const char *waterVSrc = R"(
    #version 330 core
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in vec2 a_TexCoord;

    uniform mat4 u_ViewProjection;
    uniform mat4 u_Transform;
    uniform float u_Time;
    uniform float u_Amplitude;
    uniform float u_Steepness;
    uniform float u_Speed;
    uniform float u_Scale;

    out vec3 v_WorldPos;
    out vec3 v_Normal;
    out vec2 v_TexCoord;
    out vec4 v_ClipPos;
    out float v_WaveHeight;

    // Sum 4 Gerstner waves and accumulate vertex offset + analytic normal.
    void Gerstner(in vec3 worldP, in vec2 dir, in float w, in float Q, in float A, in float phase,
                  inout vec3 outPos, inout vec3 outNormal) {
        float WA = w * A;
        float arg = w * dot(dir, worldP.xz) + phase * u_Time;
        float c = cos(arg);
        float s = sin(arg);
        outPos.x += Q * A * dir.x * c;
        outPos.z += Q * A * dir.y * c;
        outPos.y += A * s;
        // Normal contribution.
        outNormal.x -= dir.x * WA * c;
        outNormal.z -= dir.y * WA * c;
        outNormal.y -= Q * WA * s;
    }

    void main() {
        vec3 wp = vec3(u_Transform * vec4(a_Position, 1.0));
        vec3 displaced = wp;
        vec3 nrm = vec3(0.0, 1.0, 0.0);

        float A1 = u_Amplitude;
        float A2 = u_Amplitude * 0.55;
        float A3 = u_Amplitude * 0.35;
        float A4 = u_Amplitude * 0.22;
        float baseW = u_Scale * 0.6;
        Gerstner(wp, normalize(vec2( 1.0,  0.4)), baseW * 1.0, u_Steepness, A1, 1.0, displaced, nrm);
        Gerstner(wp, normalize(vec2(-0.6,  1.0)), baseW * 1.7, u_Steepness * 0.9, A2, 1.3, displaced, nrm);
        Gerstner(wp, normalize(vec2( 0.7, -0.8)), baseW * 2.6, u_Steepness * 0.7, A3, 1.7, displaced, nrm);
        Gerstner(wp, normalize(vec2(-1.0, -0.3)), baseW * 4.1, u_Steepness * 0.5, A4, 2.1, displaced, nrm);

        v_WorldPos   = displaced;
        v_Normal     = normalize(nrm);
        v_TexCoord   = a_TexCoord * 4.0;
        v_WaveHeight = displaced.y - wp.y;
        v_ClipPos    = u_ViewProjection * vec4(displaced, 1.0);
        gl_Position  = v_ClipPos;
    }
  )";

  const char *waterFSrc = R"(
    #version 330 core
    in vec3 v_WorldPos;
    in vec3 v_Normal;
    in vec2 v_TexCoord;
    in vec4 v_ClipPos;
    in float v_WaveHeight;
    out vec4 o_Color;

    uniform vec3 u_ViewPos;
    uniform vec3 u_ShallowColor;
    uniform vec3 u_DeepColor;
    uniform vec3 u_FoamColor;
    uniform vec3 u_SunDir;
    uniform vec3 u_SunColor;
    uniform float u_SunIntensity;
    uniform float u_FresnelPower;
    uniform float u_Refraction;
    uniform float u_DepthFade;
    uniform float u_FoamThreshold;
    uniform float u_Time;
    uniform sampler2D u_SceneColor;
    uniform sampler2D u_SceneDepth;
    uniform sampler2D u_PlanarReflection;
    uniform int       u_HasPlanarReflection;
    uniform float     u_Near;
    uniform float     u_Far;

    float LinearizeDepth(float d) {
        float z = d * 2.0 - 1.0;
        return (2.0 * u_Near * u_Far) / (u_Far + u_Near - z * (u_Far - u_Near));
    }

    void main() {
        vec3 N = normalize(v_Normal);

        // Pseudo-detail normal — animated by time over UV (procedural noise).
        vec2 nuv = v_TexCoord + vec2(u_Time * 0.04, u_Time * 0.025);
        vec3 nDetail = vec3(
            sin(nuv.x * 9.0 + u_Time * 1.6) + sin(nuv.y * 13.0 + u_Time * 0.9),
            0.0,
            cos(nuv.y * 11.0 + u_Time * 1.1) + cos(nuv.x * 15.0 + u_Time * 0.6)
        );
        nDetail.y = 6.0;
        nDetail = normalize(nDetail);
        N = normalize(N * 0.7 + nDetail * 0.3);

        // Screen UV.
        vec2 ndc = v_ClipPos.xy / v_ClipPos.w;
        vec2 screenUV = ndc * 0.5 + 0.5;

        vec3 V = normalize(u_ViewPos - v_WorldPos);
        float NdotV = max(dot(N, V), 0.0);
        float fresnel = pow(1.0 - NdotV, u_FresnelPower);

        // Refraction — sample scene color slightly offset by normal.
        vec2 refractUV = clamp(screenUV + N.xz * u_Refraction, 0.001, 0.999);
        vec3 refractCol = texture(u_SceneColor, refractUV).rgb;

        // Depth fade — how deep is water at this pixel.
        float sceneDepth = texture(u_SceneDepth, screenUV).r;
        float waterDepthLin = LinearizeDepth(gl_FragCoord.z);
        float sceneDepthLin = LinearizeDepth(sceneDepth);
        float depthDelta = max(sceneDepthLin - waterDepthLin, 0.0);
        float deepMix = clamp(depthDelta / u_DepthFade, 0.0, 1.0);

        // Open ocean (no geometry under water = sky leaked into refraction): treat as deep.
        bool openOcean = sceneDepth >= 0.9999;
        if (openOcean) deepMix = 1.0;

        vec3 waterCol = mix(u_ShallowColor, u_DeepColor, deepMix);
        // Always keep at least a baseline of water tint so refraction doesn't show pure sky.
        float minTint = openOcean ? 1.0 : 0.45;
        vec3 transmitted = mix(refractCol, waterCol, max(deepMix, minTint));

        // Reflection — sample planar reflection if available, else use sun glint.
        vec3 reflColor;
        if (u_HasPlanarReflection > 0) {
            vec2 reflUV = clamp(screenUV + N.xz * u_Refraction * 0.5, 0.001, 0.999);
            // Planar render mirrors Y, so flip vertically.
            reflUV.y = 1.0 - reflUV.y;
            reflColor = texture(u_PlanarReflection, reflUV).rgb;
        } else {
            // Fallback: sky-tinted reflection from N.
            float skyMask = max(N.y, 0.0);
            reflColor = mix(vec3(0.5, 0.6, 0.7), vec3(0.7, 0.85, 1.0), skyMask);
        }

        // Sun specular glint (Blinn-Phong sharp).
        vec3 H = normalize(V + normalize(-u_SunDir));
        float spec = pow(max(dot(N, H), 0.0), 200.0);
        reflColor += u_SunColor * u_SunIntensity * spec * 1.2;

        vec3 color = mix(transmitted, reflColor, fresnel);

        // Foam — bright foam where wave height crosses threshold, OR where water meets shallow geometry.
        float foamWave  = smoothstep(u_FoamThreshold, u_FoamThreshold + 0.05, v_WaveHeight);
        float foamShore = 1.0 - smoothstep(0.0, 0.6, depthDelta);
        // Shore-foam pattern using procedural noise for break-up.
        float pattern = fract(sin(dot(v_TexCoord * 30.0, vec2(12.9898, 78.233))) * 43758.5453);
        foamShore *= step(0.55, pattern + sin(u_Time * 2.0 + v_TexCoord.x * 8.0) * 0.2);
        float foam = clamp(foamWave + foamShore, 0.0, 1.0);
        color = mix(color, u_FoamColor, foam);

        o_Color = vec4(color, 1.0);
    }
  )";
  std::shared_ptr<Shader> waterShader(new Shader(waterVSrc, waterFSrc));

  const char *particleVSrc = R"(
    #version 330 core
    layout (location = 0) in vec4 a_PosSize;   // xyz = world pos, w = size
    layout (location = 1) in vec4 a_Color;
    uniform mat4 u_View;
    uniform mat4 u_Projection;
    out vec2 v_UV;
    out vec4 v_Color;
    void main() {
      // Build a camera-facing quad from gl_VertexID (0..3 for triangle strip).
      vec2 corners[4] = vec2[4](vec2(-0.5,-0.5), vec2(0.5,-0.5),
                                 vec2(-0.5, 0.5), vec2(0.5, 0.5));
      vec2 c = corners[gl_VertexID];
      v_UV = c + 0.5;
      v_Color = a_Color;
      vec3 right = vec3(u_View[0][0], u_View[1][0], u_View[2][0]);
      vec3 up    = vec3(u_View[0][1], u_View[1][1], u_View[2][1]);
      vec3 worldPos = a_PosSize.xyz + (right * c.x + up * c.y) * a_PosSize.w;
      gl_Position = u_Projection * u_View * vec4(worldPos, 1.0);
    }
  )";
  const char *particleFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    in vec4 v_Color;
    uniform sampler2D u_Texture;
    uniform int u_HasTexture;
    out vec4 o_Color;
    void main() {
      vec4 col = v_Color;
      if (u_HasTexture == 1) {
        col *= texture(u_Texture, v_UV);
      } else {
        // Soft circular falloff if no texture.
        float d = length(v_UV - 0.5) * 2.0;
        float a = clamp(1.0 - d, 0.0, 1.0);
        a = a * a;
        col.a *= a;
      }
      if (col.a < 0.01) discard;
      o_Color = col;
    }
  )";
  std::shared_ptr<Shader> particleShader(new Shader(particleVSrc, particleFSrc));

  GLuint particleVAO = 0, particleVBO = 0;
  glGenVertexArrays(1, &particleVAO);
  glGenBuffers(1, &particleVBO);
  glBindVertexArray(particleVAO);
  glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
  glBufferData(GL_ARRAY_BUFFER, 64 * 1024 * 32, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (void*)0);
  glVertexAttribDivisor(0, 1);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 32, (void*)16);
  glVertexAttribDivisor(1, 1);
  glBindVertexArray(0);

  float g_Exposure = 0.75f;
  float g_BloomStrength = 0.08f;
  float g_BloomThreshold = 1.2f;
  int   g_BlurIterations = 4;
  float g_Vignette = 0.3f;
  float g_SSAORadius = 0.5f;
  float g_SSAOBias = 0.025f;
  float g_SSAOStrength = 0.5f;
  bool  g_SSAOEnabled = true;
  float g_IBLIntensity = 0.3f;
  bool  g_SSREnabled = true;
  float g_SSRStrength = 1.0f;
  int   g_DebugView = 0;

  bool      g_UseProceduralSky = true;
  glm::vec3 g_SkyTop     = {0.15f, 0.32f, 0.70f};
  glm::vec3 g_SkyHorizon = {0.78f, 0.85f, 0.95f};
  glm::vec3 g_GroundCol  = {0.15f, 0.14f, 0.12f};
  float     g_SunDiscSize = 0.0006f;
  float     g_SunDiscIntensity = 1.5f;

  bool      g_AutoExposure = true;
  float     g_AutoExposureSpeed = 1.5f;
  float     g_AutoExposureMin = 0.25f;
  float     g_AutoExposureMax = 2.0f;
  float     g_CurrentAvgLum = 0.18f;

  float     g_LensDirtStrength = 0.0f;

  float g_ChromaticAberration = 0.0f;
  float g_FilmGrain           = 0.0f;
  float g_FogDensity          = 0.0f;
  glm::vec3 g_FogColor        = {0.6f, 0.65f, 0.72f};
  float g_FogStart            = 15.0f;
  bool  g_GodRaysEnabled      = false;
  float g_GodRaysStrength     = 0.5f;
  float g_LensFlareStrength   = 0.0f;
  float g_DOFFocusDepth       = 0.2f;
  float g_DOFRange            = 0.08f;
  float g_DOFStrength         = 0.0f;
  float g_MotionBlur          = 0.0f;
  float g_TAAStrength         = 0.0f;

  float g_Contrast    = 1.0f;
  float g_Saturation  = 1.0f;
  float g_Temperature = 0.0f;
  float g_Tint        = 0.0f;
  glm::vec3 g_Lift  = {0.0f, 0.0f, 0.0f};
  glm::vec3 g_Gamma = {1.0f, 1.0f, 1.0f};
  glm::vec3 g_Gain  = {1.0f, 1.0f, 1.0f};

#include "../Scene/SceneSerializer.h"

#ifdef GAME_MODE_RUNTIME
  // The project's startup scene was already loaded by the constructor; only
  // fall back to a loose Default.scene when there is no project.
  if (m_Project.Name.empty()) {
    m_Scene->Clear();
    SceneSerializer serializer(m_Scene);
    if (serializer.Deserialize("Default.scene")) {
      std::cout << "Loaded Default.scene successfully" << std::endl;
      std::cout << "[Runtime] Entity count: " << m_Scene->GetEntities().size()
                << std::endl;
      m_Scene->OnPhysicsStart();
    } else {
      std::cout << "Could not load Default.scene, using manual scene setup..."
                << std::endl;
    }
  }
#endif
  const char *skyboxVertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        out vec3 TexCoords;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            TexCoords = aPos;
            vec4 pos = projection * view * vec4(aPos, 1.0);
            gl_Position = pos.xyww;
        }
    )";

  const char *skyboxFragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        in vec3 TexCoords;
        uniform samplerCube skybox;
        uniform int   u_UseProcedural;
        uniform vec3  u_SunDir;         // light direction (points FROM sun)
        uniform vec3  u_SkyTop;
        uniform vec3  u_SkyHorizon;
        uniform vec3  u_GroundColor;
        uniform float u_SunDiscSize;
        uniform float u_SunIntensity;

        // Analytical atmospheric sky — no cubemap edges, physically-plausible.
        vec3 ProceduralSky(vec3 dir, vec3 sunDir) {
            dir = normalize(dir);
            // Ground vs sky blend.
            float upness = clamp(dir.y, -1.0, 1.0);
            vec3 sky;
            if (upness >= 0.0) {
                // Exponential gradient from horizon to zenith.
                float t = 1.0 - pow(1.0 - upness, 2.5);
                sky = mix(u_SkyHorizon, u_SkyTop, t);
            } else {
                sky = mix(u_SkyHorizon, u_GroundColor,
                          smoothstep(0.0, -0.25, upness));
            }

            // Sunset tint — horizon warms when sun is near horizon.
            float sunHeight = -sunDir.y;                 // 1 = noon, 0 = horizon
            float twilight  = smoothstep(0.5, 0.0, sunHeight);
            vec3 sunset = vec3(1.0, 0.45, 0.15);
            // Proximity to horizon in view.
            float horizonMask = 1.0 - smoothstep(0.0, 0.4, upness);
            // Proximity to sun direction in horizontal plane.
            vec2 sunH = normalize(vec2(-sunDir.x, -sunDir.z) + 1e-4);
            vec2 dirH = normalize(vec2(dir.x, dir.z) + 1e-4);
            float sunAzim = max(dot(sunH, dirH), 0.0);
            sky = mix(sky, sunset, twilight * horizonMask * sunAzim * 0.75);

            // Sun disc + halo.
            float sunDot = max(dot(dir, -sunDir), 0.0);
            float disc = smoothstep(1.0 - u_SunDiscSize, 1.0 - u_SunDiscSize * 0.5, sunDot);
            sky += vec3(1.0, 0.95, 0.85) * disc * u_SunIntensity * 4.0;
            // Tight halo.
            sky += vec3(1.0, 0.9, 0.75) * pow(sunDot, 300.0) * 2.0;
            // Softer glow.
            sky += vec3(1.0, 0.85, 0.65) * pow(sunDot, 20.0) * 0.35;

            return max(sky, vec3(0.0));
        }

        void main() {
            if (u_UseProcedural > 0) {
                FragColor = vec4(ProceduralSky(TexCoords, u_SunDir), 1.0);
            } else {
                FragColor = texture(skybox, TexCoords);
            }
        }
    )";

  std::shared_ptr<Shader> skyboxShader = std::make_shared<Shader>(
      skyboxVertexShaderSource, skyboxFragmentShaderSource);

  float skyboxVertices[] = {
      -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,
      -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f,
      1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,
      -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,
      -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
      -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,
      -1.0f, 1.0f,  -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,
      -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, 1.0f,
      -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f,
      -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f,
      1.0f,  1.0f,  -1.0f, 1.0f};

  std::shared_ptr<VertexArray> skyboxVAO = std::make_shared<VertexArray>();
  std::shared_ptr<VertexBuffer> skyboxVBO =
      std::make_shared<VertexBuffer>(skyboxVertices, sizeof(skyboxVertices));
  skyboxVBO->SetLayout({{GL_FLOAT, 3, "aPos"}});
  skyboxVAO->AddVertexBuffer(skyboxVBO);

  struct IBLEnvironment {
    uint32_t IrradianceMap = 0;
    uint32_t PrefilterMap  = 0;
    uint32_t BRDFLUT       = 0;
    uint32_t SourceCubemap = 0;
    bool     Ready         = false;
  };
  IBLEnvironment ibl;

  const char* cubeVSrc = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 u_Projection;
    uniform mat4 u_View;
    out vec3 v_LocalPos;
    void main() {
        v_LocalPos = aPos;
        gl_Position = u_Projection * u_View * vec4(aPos, 1.0);
    }
  )";
  const char* irradianceFSrc = R"(
    #version 330 core
    in vec3 v_LocalPos;
    out vec4 o_Color;
    uniform samplerCube u_EnvMap;
    const float PI = 3.14159265359;
    void main() {
        vec3 N = normalize(v_LocalPos);
        vec3 irradiance = vec3(0.0);
        vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 right = normalize(cross(up, N));
        up = normalize(cross(N, right));
        float sampleDelta = 0.025;
        float nrSamples = 0.0;
        for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
            for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
                vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
                vec3 s = t.x * right + t.y * up + t.z * N;
                irradiance += texture(u_EnvMap, s).rgb * cos(theta) * sin(theta);
                nrSamples++;
            }
        }
        irradiance = PI * irradiance / nrSamples;
        o_Color = vec4(irradiance, 1.0);
    }
  )";
  const char* prefilterFSrc = R"(
    #version 330 core
    in vec3 v_LocalPos;
    out vec4 o_Color;
    uniform samplerCube u_EnvMap;
    uniform float u_Roughness;
    const float PI = 3.14159265359;

    float RadicalInverse_VdC(uint bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return float(bits) * 2.3283064365386963e-10;
    }
    vec2 Hammersley(uint i, uint N) {
        return vec2(float(i)/float(N), RadicalInverse_VdC(i));
    }
    vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
        float a = roughness * roughness;
        float phi = 2.0 * PI * Xi.x;
        float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
        float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
        vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 tangent = normalize(cross(up, N));
        vec3 bitangent = cross(N, tangent);
        return normalize(tangent * H.x + bitangent * H.y + N * H.z);
    }

    void main() {
        vec3 N = normalize(v_LocalPos);
        vec3 R = N; vec3 V = R;
        const uint SAMPLE_COUNT = 1024u;
        vec3 prefilteredColor = vec3(0.0);
        float totalWeight = 0.0;
        for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
            vec2 Xi = Hammersley(i, SAMPLE_COUNT);
            vec3 H = ImportanceSampleGGX(Xi, N, u_Roughness);
            vec3 L = normalize(2.0 * dot(V, H) * H - V);
            float NdotL = max(dot(N, L), 0.0);
            if (NdotL > 0.0) {
                prefilteredColor += texture(u_EnvMap, L).rgb * NdotL;
                totalWeight += NdotL;
            }
        }
        prefilteredColor /= totalWeight;
        o_Color = vec4(prefilteredColor, 1.0);
    }
  )";
  const char* brdfLutFSrc = R"(
    #version 330 core
    in vec2 v_UV;
    out vec4 o_Color;
    const float PI = 3.14159265359;

    float RadicalInverse_VdC(uint bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return float(bits) * 2.3283064365386963e-10;
    }
    vec2 Hammersley(uint i, uint N) { return vec2(float(i)/float(N), RadicalInverse_VdC(i)); }
    vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
        float a = roughness * roughness;
        float phi = 2.0 * PI * Xi.x;
        float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
        float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
        vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 tangent = normalize(cross(up, N));
        vec3 bitangent = cross(N, tangent);
        return normalize(tangent * H.x + bitangent * H.y + N * H.z);
    }
    float GeometrySchlickGGX(float NdotV, float roughness) {
        float a = roughness;
        float k = (a*a) / 2.0;
        return NdotV / (NdotV * (1.0 - k) + k);
    }
    float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
        float NdotV = max(dot(N, V), 0.0);
        float NdotL = max(dot(N, L), 0.0);
        return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
    }

    vec2 IntegrateBRDF(float NdotV, float roughness) {
        vec3 V;
        V.x = sqrt(1.0 - NdotV*NdotV); V.y = 0.0; V.z = NdotV;
        float A = 0.0, B = 0.0;
        vec3 N = vec3(0, 0, 1);
        const uint SAMPLE_COUNT = 1024u;
        for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
            vec2 Xi = Hammersley(i, SAMPLE_COUNT);
            vec3 H = ImportanceSampleGGX(Xi, N, roughness);
            vec3 L = normalize(2.0 * dot(V, H) * H - V);
            float NdotL = max(L.z, 0.0);
            float NdotH = max(H.z, 0.0);
            float VdotH = max(dot(V, H), 0.0);
            if (NdotL > 0.0) {
                float G = GeometrySmith(N, V, L, roughness);
                float G_Vis = (G * VdotH) / (NdotH * NdotV);
                float Fc = pow(1.0 - VdotH, 5.0);
                A += (1.0 - Fc) * G_Vis;
                B += Fc * G_Vis;
            }
        }
        return vec2(A, B) / float(SAMPLE_COUNT);
    }

    void main() {
        vec2 res = IntegrateBRDF(v_UV.x, v_UV.y);
        o_Color = vec4(res, 0.0, 1.0);
    }
  )";

  std::shared_ptr<Shader> irradianceShader(new Shader(cubeVSrc, irradianceFSrc));

  std::shared_ptr<Shader> prefilterShader (new Shader(cubeVSrc, prefilterFSrc));

  std::shared_ptr<Shader> brdfLutShader   (new Shader(postVSrc, brdfLutFSrc));

  auto precomputeIBL = [&](uint32_t envCubemap) {
    if (!envCubemap) return;

    if (ibl.IrradianceMap) glDeleteTextures(1, &ibl.IrradianceMap);
    if (ibl.PrefilterMap)  glDeleteTextures(1, &ibl.PrefilterMap);
    if (ibl.BRDFLUT)       glDeleteTextures(1, &ibl.BRDFLUT);
    ibl = {};

    auto makeCubemap = [](int size, bool mipmaps) -> uint32_t {
      uint32_t id;
      glGenTextures(1, &id);
      glBindTexture(GL_TEXTURE_CUBE_MAP, id);
      for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
      }
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                      mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
      if (mipmaps) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
      return id;
    };

    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[6] = {
      glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
      glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
      glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
      glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
      glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
      glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
    };

    uint32_t captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    ibl.IrradianceMap = makeCubemap(64, false);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, captureRBO);
    irradianceShader->Bind();
    irradianceShader->UploadUniformMat4("u_Projection", captureProj);
    irradianceShader->UploadUniformInt("u_EnvMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glViewport(0, 0, 64, 64);
    for (int i = 0; i < 6; ++i) {
      irradianceShader->UploadUniformMat4("u_View", captureViews[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                             ibl.IrradianceMap, 0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      skyboxVAO->Bind();
      glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    ibl.PrefilterMap = makeCubemap(512, true);
    prefilterShader->Bind();
    prefilterShader->UploadUniformMat4("u_Projection", captureProj);
    prefilterShader->UploadUniformInt("u_EnvMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    const int maxMip = 6;
    for (int mip = 0; mip < maxMip; ++mip) {
      int mipSize = 512 >> mip;
      glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
      glViewport(0, 0, mipSize, mipSize);
      float roughness = (float)mip / (float)(maxMip - 1);
      prefilterShader->UploadUniformFloat("u_Roughness", roughness);
      for (int i = 0; i < 6; ++i) {
        prefilterShader->UploadUniformMat4("u_View", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                               ibl.PrefilterMap, mip);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        skyboxVAO->Bind();
        glDrawArrays(GL_TRIANGLES, 0, 36);
      }
    }

    glGenTextures(1, &ibl.BRDFLUT);
    glBindTexture(GL_TEXTURE_2D, ibl.BRDFLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ibl.BRDFLUT, 0);
    glViewport(0, 0, 512, 512);
    brdfLutShader->Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    ibl.SourceCubemap = envCubemap;
    ibl.Ready = true;
    std::cout << "[IBL] Precomputed env=" << envCubemap
              << " irr=" << ibl.IrradianceMap
              << " pre=" << ibl.PrefilterMap
              << " lut=" << ibl.BRDFLUT << std::endl;
  };

#ifndef GAME_MODE_RUNTIME
  ImGuiIO &io = ImGui::GetIO();
#endif

  while (m_Running) {
    if (std::string(SDL_GetWindowTitle(m_Window->GetNativeWindow())) ==
        "Engine") {
      if (m_AppState == AppState::Runtime) {
        static int correctionLog = 0;
        if (correctionLog == 0)
          std::cout
              << "[Watchdog] Forcing AppState::Editor based on Window Title."
              << std::endl;
        correctionLog++;
        m_AppState = AppState::Editor;
      }
    }

    bool sceneSwappedThisFrame = false;
    if (auto pending = SceneManager::ConsumePendingLoad()) {
      if (m_Project.Name.empty()) {
        std::cerr << "[SceneManager] got pending load but no project is open\n";
      } else {
        const SceneEntry* entry = m_Project.FindScene(*pending);
        if (!entry) {
          std::cerr << "[SceneManager] scene '" << *pending << "' not in project, ignoring\n";
        } else {
          SceneSerializer ser(m_Scene);
          auto full = (m_Project.ProjectRoot / entry->RelativePath).string();
          if (ser.Deserialize(full)) {
            m_CurrentScenePath = full;
            m_Scene->OnPhysicsStart();
            sceneSwappedThisFrame = true;
          }
        }
      }
    }

    if (!sceneSwappedThisFrame && !m_NextScenePath.empty()) {
      m_Scene->Clear();
      SceneSerializer serializer(m_Scene);
      if (serializer.Deserialize(m_NextScenePath)) {
        m_CurrentScenePath = m_NextScenePath;
        m_Scene->OnPhysicsStart();
      }
      m_NextScenePath.clear();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (m_AppState == AppState::Runtime) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE &&
            !event.key.repeat && !m_GameHandlesEscape) {
          SetMouseLocked(!m_MouseLocked);
        }
      }

      bool blockEditorInput = m_AppState == AppState::Runtime && m_MouseLocked;
      bool mouseEvent =
          event.type == SDL_MOUSEMOTION ||
          event.type == SDL_MOUSEBUTTONDOWN ||
          event.type == SDL_MOUSEBUTTONUP ||
          event.type == SDL_MOUSEWHEEL;
      bool textEvent = event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING;
      if (!blockEditorInput || (!mouseEvent && !textEvent)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
      }

#ifndef GAME_MODE_RUNTIME

      if (m_AppState == AppState::Runtime && event.type == SDL_KEYDOWN &&
          event.key.keysym.sym == SDLK_ESCAPE && !event.key.repeat && !m_GameHandlesEscape) {
        TogglePlayMode();
      }

      if (m_AppState != AppState::Runtime && event.type == SDL_KEYDOWN) {
        bool control = Input::IsKeyPressed(SDL_SCANCODE_LCTRL) ||
                       Input::IsKeyPressed(SDL_SCANCODE_RCTRL);
        bool shift = Input::IsKeyPressed(SDL_SCANCODE_LSHIFT) ||
                     Input::IsKeyPressed(SDL_SCANCODE_RSHIFT);

        if (true) {
          bool wantTextInput = io.WantTextInput;

          if (!wantTextInput && event.key.keysym.sym == SDLK_DELETE) {
            Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
            if (selected.GetID() != -1) {
              SaveHistoryState();
              m_Scene->DestroyEntity(selected);
              m_SceneHierarchyPanel.SetSelectedEntity({});
            }
          }

          if (control && !wantTextInput) {
            if (event.key.keysym.sym == SDLK_z) {
              if (shift)
                Redo();
              else
                Undo();
            } else if (event.key.keysym.sym == SDLK_y) {
              Redo();
            } else if (event.key.keysym.sym == SDLK_d) {
              DuplicateSelectedEntities();
            }
          }
        }
      }
#endif

      if (event.type == SDL_QUIT)
        m_Running = false;

      if (event.type == SDL_MOUSEWHEEL)
        Input::AddMouseScroll((float)event.wheel.y);

      if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID ==
                SDL_GetWindowID(m_Window->GetNativeWindow())) {
          m_Running = false;
        }
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          m_Window->OnResize(event.window.data1, event.window.data2);
        }
      }
    }

    Input::Update();

    // Tracking/actions must be sampled before gameplay, for this predicted frame.
    bool vrActive = m_VRSystem.IsAvailable() && m_AppState == AppState::Runtime;
    bool xrFrameValid = vrActive && m_VRSystem.BeginFrame();
    if (vrActive) {
      int width = 0, height = 0;
      SDL_GetWindowSize(m_Window->GetNativeWindow(), &width, &height);
      m_GameViewRect = {0, 0, (float)VRSystem::kUIWidth, (float)VRSystem::kUIHeight};
    }

    float time = (float)SDL_GetTicks() / 1000.0f;
    float ts = time - m_LastFrameTime;
    m_LastFrameTime = time;

#ifndef GAME_MODE_RUNTIME

    if (m_AutosaveEnabled && GetProject() && m_AppState == AppState::Editor && !m_CurrentScenePath.empty()) {
      m_AutosaveTimer += ts;
      if (m_AutosaveTimer >= m_AutosaveInterval) {
        m_AutosaveTimer = 0.0f;
        SceneSerializer(m_Scene).Serialize(m_CurrentScenePath);
        MarkSceneSynced();
        std::cout << "[Autosave] saved " << m_CurrentScenePath << "\n";
      }
    } else {
      m_AutosaveTimer = 0.0f;
    }
#endif

    if (m_AppState == AppState::Runtime) {
      m_Scene->OnUpdate(ts);
      { static VRRigSystem s_VRRigSystem; s_VRRigSystem.OnRuntimeUpdate(*m_Scene, ts); }
      Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
      if (primaryCamera && m_MouseLocked) {
        bool hasRunnableScript =
            primaryCamera->HasScript &&
            (primaryCamera->m_NativeScript.Instance ||
             primaryCamera->m_NativeScript.InstantiateScript);
        if (!hasRunnableScript) {
          glm::vec2 mouseDelta = Input::GetMouseDelta();
          constexpr float sensitivity = 0.002f;
          primaryCamera->Transform.Rotation.y -= mouseDelta.x * sensitivity;
          primaryCamera->Transform.Rotation.x -= mouseDelta.y * sensitivity;
          primaryCamera->Transform.Rotation.x =
              std::clamp(primaryCamera->Transform.Rotation.x,
                         glm::radians(-89.0f), glm::radians(89.0f));

          glm::vec3 direction(0.0f);
          if (Input::IsKeyPressed(SDL_SCANCODE_W)) direction.z -= 1.0f;
          if (Input::IsKeyPressed(SDL_SCANCODE_S)) direction.z += 1.0f;
          if (Input::IsKeyPressed(SDL_SCANCODE_A)) direction.x -= 1.0f;
          if (Input::IsKeyPressed(SDL_SCANCODE_D)) direction.x += 1.0f;

          if (glm::length(direction) > 0.0f) {
            direction = glm::normalize(direction);
            float speed = Input::IsKeyPressed(SDL_SCANCODE_LSHIFT) ? 18.0f : 10.0f;
            glm::quat yaw(glm::vec3(0.0f, primaryCamera->Transform.Rotation.y, 0.0f));
            primaryCamera->Transform.Translation += (yaw * direction) * speed * ts;
          }
        }
      }
      if (!primaryCamera) {
        m_Camera.OnUpdate(ts);
      }
    } else {
      m_Camera.OnUpdate(ts);
      m_Scene->OnUpdateEditor(ts);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if (vrActive && m_VRSystem.HasTracking()) {
      ImGui::GetIO().DisplaySize = ImVec2((float)VRSystem::kUIWidth, (float)VRSystem::kUIHeight);
      ImGui::GetIO().DisplayFramebufferScale = ImVec2(1,1);
    }
    if (m_AppState == AppState::Runtime && m_MouseLocked && !vrActive) {
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    } else {
      ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
      ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
    static bool vrMouseDown = false;
    static bool vrNavigation = false;
    if (vrActive) {
      auto& io = ImGui::GetIO();
      m_VRSystem.SetUIAspect(io.DisplaySize.x / std::max(io.DisplaySize.y, 1.0f));
      if (m_VRSystem.HasTracking() && m_VRSystem.IsFocused() &&
          (m_VRSystem.IsUIInteractive() || m_VRSystem.IsCinemaMode())) {
        glm::vec2 uv;
        bool hit = m_VRSystem.GetUIPointer(uv);
        const auto& hand = m_VRSystem.GetHandState(m_VRSystem.GetHandState(1).Active ? 1 : 0);
        const auto& left = m_VRSystem.GetHandState(0);
        if (glm::length(left.Thumbstick) > .6f) vrNavigation = true;
        if (hand.Trigger > .55f) vrNavigation = false;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, left.Thumbstick.y > .6f);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, left.Thumbstick.y < -.6f);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, left.Thumbstick.x < -.6f);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, left.Thumbstick.x > .6f);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, vrNavigation && hand.PrimaryBtn);
        io.AddMousePosEvent(hit && !vrNavigation ? uv.x * io.DisplaySize.x : -1000.0f, hit && !vrNavigation ? uv.y * io.DisplaySize.y : -1000.0f);
        vrMouseDown = !vrNavigation && hit && hand.Active && (hand.Trigger > 0.55f || hand.PrimaryBtn);
        io.AddMouseButtonEvent(0, vrMouseDown);
      } else if (vrMouseDown) {
        io.AddMouseButtonEvent(0, false);
        vrMouseDown = false;
      }
      if (!m_VRSystem.IsUIInteractive() || !m_VRSystem.IsFocused()) {
        for (auto key : {ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft,
                        ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadFaceDown}) io.AddKeyEvent(key, false);
        vrNavigation = false;
      }
      m_Window->SetVSync(false); // OpenXR paces the frame, independently of the monitor.
    }
    ImGui::NewFrame();
    if (m_AppState == AppState::Runtime) {
      if (xrFrameValid) m_VRSystem.RefreshTracking();
      m_Scene->OnLateUpdate(ts);
      if (vrActive) {
        m_VRSystem.BeginWorldUIFrame();
        if (xrFrameValid && !m_VRSystem.IsCinemaMode()) m_Scene->OnWorldUIRender();
      }
    }

#ifndef GAME_MODE_RUNTIME
    ImGuizmo::BeginFrame();

    if (Input::IsKeyPressed(SDL_SCANCODE_LCTRL) &&
        Input::IsKeyPressed(SDL_SCANCODE_S)) {
      if (!m_CurrentScenePath.empty()) {
        SceneSerializer(m_Scene).Serialize(m_CurrentScenePath);
        MarkSceneSynced();
      } else if (GetProject()) {
        std::string file = FileDialogs::SaveFile(".scene\0*.scene\0");
        if (!file.empty()) {
          std::filesystem::path p = file;
          if (p.extension() != ".scene") p += ".scene";
          SceneSerializer(m_Scene).Serialize(p.string());
          AddOrRegisterScene(p);
          m_CurrentScenePath = p.string();
          MarkSceneSynced();
        }
      }
    }

    if (Input::IsKeyPressed(SDL_SCANCODE_LCTRL) &&
        Input::IsKeyPressed(SDL_SCANCODE_O)) {
    }

    if (m_AppState == AppState::ProjectHub) {
      Renderer::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
      Renderer::Clear();
      RenderProjectHub();
    } else if (m_AppState == AppState::Editor) {

      if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
          if (ImGui::MenuItem("New Project...")) {
            std::string file = FileDialogs::SaveFile(".myproject\0*.myproject\0");
            if (!file.empty()) {
              std::filesystem::path p = file;
              if (p.extension() != ".myproject") p += ".myproject";
              std::string stem = p.stem().string();
              if (!NewProject(p.parent_path(), stem))
                std::cerr << m_LastProjectError << "\n";
            }
          }
          if (ImGui::MenuItem("Open Project...")) {
            std::string file = FileDialogs::OpenFile(".myproject\0*.myproject\0");
            if (!file.empty()) (void)OpenProject(file);
          }
          if (ImGui::MenuItem("Close Project")) {
            CloseProject();
          }
          ImGui::Separator();

          if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, GetProject() != nullptr)) {
            if (!m_CurrentScenePath.empty()) {
              SceneSerializer(m_Scene).Serialize(m_CurrentScenePath);
              MarkSceneSynced();
            } else {
              auto defaultDir = (m_Project.ProjectRoot / "Assets" / "Scenes").string();
              std::filesystem::create_directories(defaultDir);
              std::string file = FileDialogs::SaveFile(".scene\0*.scene\0", defaultDir.c_str());
              if (!file.empty()) {
                std::filesystem::path p = file;
                if (p.extension() != ".scene") p += ".scene";
                SceneSerializer(m_Scene).Serialize(p.string());
                AddOrRegisterScene(p);
                m_CurrentScenePath = p.string();
                MarkSceneSynced();
              }
            }
          }
          if (ImGui::MenuItem("Save Scene As...", nullptr, false, GetProject() != nullptr)) {
            auto defaultDir = (m_Project.ProjectRoot / "Assets" / "Scenes").string();
            std::filesystem::create_directories(defaultDir);
            std::string file = FileDialogs::SaveFile(".scene\0*.scene\0", defaultDir.c_str());
            if (!file.empty()) {
              std::filesystem::path p = file;
              if (p.extension() != ".scene") p += ".scene";
              SceneSerializer(m_Scene).Serialize(p.string());
              AddOrRegisterScene(p);
              m_CurrentScenePath = p.string();
              MarkSceneSynced();
            }
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Reload Scripts", "Ctrl+R", false, GetProject() != nullptr)) {
            ReloadScripts();
          }
          if (ImGui::MenuItem("Build Game...", "Ctrl+B", false, GetProject() != nullptr)) {
            m_BuildOutputBuf[0] = '\0';
            m_BuildResultMsg.clear();
            m_ShowBuildDialog = true;
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Exit"))
            m_Running = false;
          ImGui::EndMenu();
        }

        if (GetProject() != nullptr && ImGui::BeginMenu("Scene")) {
          if (ImGui::MenuItem("New Scene...")) {
            m_NewSceneNameBuf[0] = '\0';
            m_OpenNewScenePopup = true;
          }
          if (ImGui::BeginMenu("Open Scene")) {
            for (const auto& s : m_Project.Scenes) {
              if (ImGui::MenuItem(s.Name.c_str())) {
                auto full = (m_Project.ProjectRoot / s.RelativePath).string();
                m_Scene->Clear();
                if (SceneSerializer(m_Scene).Deserialize(full)) {
                  m_CurrentScenePath = full;
                  MarkSceneSynced();
                }
              }
            }
            ImGui::EndMenu();
          }
          if (ImGui::BeginMenu("Set Startup Scene")) {
            for (const auto& s : m_Project.Scenes) {
              bool isStartup = (s.Name == m_Project.StartupSceneName);
              if (ImGui::MenuItem(s.Name.c_str(), nullptr, isStartup)) {
                m_Project.StartupSceneName = s.Name;
                PersistProject();
              }
            }
            ImGui::EndMenu();
          }
          if (ImGui::BeginMenu("Remove Scene from Project")) {
            for (size_t i = 0; i < m_Project.Scenes.size(); ++i) {
              const auto& s = m_Project.Scenes[i];
              if (s.Name == m_Project.StartupSceneName) continue;
              if (ImGui::MenuItem(s.Name.c_str())) {
                m_Project.Scenes.erase(m_Project.Scenes.begin() + i);
                PersistProject();
                break;
              }
            }
            ImGui::EndMenu();
          }
          ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
          if (ImGui::MenuItem("Autosave (every 60s)", nullptr, m_AutosaveEnabled, GetProject() != nullptr)) {
            m_AutosaveEnabled = !m_AutosaveEnabled;
            m_AutosaveTimer = 0.0f;
          }
          ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
          if (ImGui::MenuItem("Show UI Overlay", "", &m_ShowUIOverlay)) {
          }
          if (ImGui::MenuItem("Scene Settings", nullptr, m_ShowSceneSettings)) {
            m_ShowSceneSettings = !m_ShowSceneSettings;
          }
          if (ImGui::MenuItem("Reset Layout")) {
            m_LayoutInitialized = false;
          }
          ImGui::EndMenu();
        }

        const float buttonW = 86.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - buttonW * 0.5f);
        if (IsPlaying()) {
          ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.22f, 0.22f, 1));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.30f, 0.30f, 1));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.62f, 0.15f, 0.15f, 1));
          if (ImGui::Button("Stop", ImVec2(buttonW, 0))) TogglePlayMode();
          ImGui::PopStyleColor(3);
        } else {
          bool canPlay = (GetProject() != nullptr);
          if (!canPlay) ImGui::BeginDisabled();
          ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.65f, 0.32f, 1));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.80f, 0.38f, 1));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.52f, 0.24f, 1));
          if (ImGui::Button("Play", ImVec2(buttonW, 0))) TogglePlayMode();
          ImGui::PopStyleColor(3);
          if (!canPlay) ImGui::EndDisabled();
        }

        ImGui::EndMainMenuBar();
      }

      if (m_ShowBuildDialog) {
        ImGui::OpenPopup("Build Game");
        m_ShowBuildDialog = false;
      }
      if (ImGui::BeginPopupModal("Build Game", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Output folder (will be created if missing):");
        ImGui::SetNextItemWidth(480);
        ImGui::InputText("##buildpath", m_BuildOutputBuf, sizeof(m_BuildOutputBuf));
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1),
                           "Example: C:/Users/me/Desktop/%s-Build",
                           m_Project.Name.c_str());
        if (!m_BuildResultMsg.empty()) {
          ImVec4 col = m_BuildResultOk ? ImVec4(0.4f, 0.85f, 0.4f, 1)
                                       : ImVec4(1.0f, 0.4f, 0.4f, 1);
          ImGui::TextColored(col, "%s", m_BuildResultMsg.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Build", ImVec2(120, 0))) {
          std::string out = m_BuildOutputBuf;
          if (out.empty()) {
            m_BuildResultOk = false;
            m_BuildResultMsg = "Output folder required.";
          } else {
            std::string err = BuildStandaloneGame(out);
            m_BuildResultOk = err.empty();
            m_BuildResultMsg = err.empty() ? "Built successfully: " + out : err;
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
          m_BuildResultMsg.clear();
        }
        ImGui::EndPopup();
      }

      if (m_ShowScriptBuildError) {
        ImGui::OpenPopup("Scripts did not compile");
        m_ShowScriptBuildError = false;
      }
      if (ImGui::BeginPopupModal("Scripts did not compile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "The game scripts failed to compile. The previous Game.dll is still loaded,");
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "so Play would run the OLD version of the game.");
        ImGui::InputTextMultiline("##scriptlog", m_ScriptBuildLog.data(), m_ScriptBuildLog.size() + 1,
                                  ImVec2(900, 320), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy log", ImVec2(120, 0)))
          ImGui::SetClipboardText(m_ScriptBuildLog.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Retry (Reload Scripts)", ImVec2(200, 0))) {
          ImGui::CloseCurrentPopup();
          ReloadScripts();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0)))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }

      if (m_OpenNewScenePopup) {
        ImGui::OpenPopup("New Scene");
        m_OpenNewScenePopup = false;
      }
      if (ImGui::BeginPopupModal("New Scene", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Scene name:");
        ImGui::SetNextItemWidth(260);
        bool submitted = ImGui::InputText("##scene_name", m_NewSceneNameBuf,
                                          sizeof(m_NewSceneNameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        std::string name = m_NewSceneNameBuf;

        while (!name.empty() && std::isspace((unsigned char)name.front())) name.erase(name.begin());
        while (!name.empty() && std::isspace((unsigned char)name.back()))  name.pop_back();

        bool dup = (m_Project.FindScene(name) != nullptr);
        bool valid = !name.empty() && !dup;
        if (dup)
          ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A scene with this name already exists.");
        if (!valid) ImGui::BeginDisabled();
        bool clicked = ImGui::Button("Create", ImVec2(120, 0));
        if (!valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
        }
        if (valid && (clicked || submitted)) {
          auto scenesDir = m_Project.ProjectRoot / "Assets" / "Scenes";
          std::filesystem::create_directories(scenesDir);
          auto p = scenesDir / (name + ".scene");
          m_Scene->Clear();
          SceneSerializer(m_Scene).Serialize(p.string());
          AddOrRegisterScene(p);
          m_CurrentScenePath = p.string();
          MarkSceneSynced();
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (m_ShowSceneSettings) {
        if (ImGui::Begin("Scene Settings", &m_ShowSceneSettings)) {
          if (ImGui::CollapsingHeader("Physics",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 gravity = m_Scene->GetGravity();
            if (ImGui::DragFloat3("Gravity", glm::value_ptr(gravity), 0.1f)) {
              m_Scene->SetGravity(gravity);
            }
          }

          if (ImGui::CollapsingHeader("Environment",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 skyColor = m_Scene->GetSkyColor();
            if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(skyColor))) {
              m_Scene->SetSkyColor(skyColor);
            }

            glm::vec3 ambientColor = m_Scene->GetAmbientColor();
            if (ImGui::ColorEdit3("Ambient Color",
                                  glm::value_ptr(ambientColor))) {
              m_Scene->SetAmbientColor(ambientColor);
            }

            float ambientIntensity = m_Scene->GetAmbientIntensity();
            if (ImGui::DragFloat("Ambient Intensity", &ambientIntensity, 0.01f,
                                 0.0f, 10.0f)) {
              m_Scene->SetAmbientIntensity(ambientIntensity);
            }

            ImGui::Separator();
            ImGui::Text("Sky Texture (PNG)");
            std::string skyTexLabel =
                m_Scene->GetSkyTexturePath().empty()
                    ? "Drag/Select PNG"
                    : std::filesystem::path(m_Scene->GetSkyTexturePath())
                          .filename()
                          .string();

            if (ImGui::Button(skyTexLabel.c_str(), ImVec2(-1, 0))) {
              std::string path = FileDialogs::OpenFile(
                  "Texture (*.jpg;*.png;*.tga)\0*.jpg;*.png;*.tga\0All Files "
                  "(*.*)\0*.*\0");
              if (!path.empty()) {
                m_Scene->SetSkyTexturePath(path);
              }
            }

            if (ImGui::BeginDragDropTarget()) {
              if (const ImGuiPayload *payload =
                      ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char *path = (const char *)payload->Data;
                std::filesystem::path p(path);
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".tga") {
                  m_Scene->SetSkyTexturePath(p.string());
                }
              }
              ImGui::EndDragDropTarget();
            }

            if (!m_Scene->GetSkyTexturePath().empty()) {
              if (ImGui::Button("Clear Sky Texture")) {
                m_Scene->SetSkyTexturePath("");
              }
            }
          }
        }

        ImGui::End();
      }
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("MainDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace_v2");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (!m_LayoutInitialized) {
      m_LayoutInitialized = true;

      ImGui::DockBuilderRemoveNode(dockspace_id);
      ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspace_id,
                                    ImGui::GetMainViewport()->Size);

      ImGuiID dock_main_id = dockspace_id;
      ImGuiID dock_right = ImGui::DockBuilderSplitNode(
          dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
      ImGuiID dock_left = ImGui::DockBuilderSplitNode(
          dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
      ImGuiID dock_down = ImGui::DockBuilderSplitNode(
          dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

      ImGui::DockBuilderDockWindow("Viewport",        dock_main_id);
      ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
      ImGui::DockBuilderDockWindow("Inspector",       dock_right);
      ImGui::DockBuilderDockWindow("Rendering",       dock_right);
      ImGui::DockBuilderDockWindow("Content Browser", dock_down);

      ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::End();
#endif

    {
      auto writePost = [&](const RenderSettings &from) {
        g_Exposure = from.Exposure;
        g_AutoExposure = from.AutoExposure;
        g_BloomStrength = from.BloomStrength;
        g_BloomThreshold = from.BloomThreshold;
        g_Vignette = from.Vignette;
        g_FilmGrain = from.FilmGrain;
        g_ChromaticAberration = from.ChromaticAberration;
        g_FogDensity = from.FogDensity;
        g_FogColor = from.FogColor;
        g_FogStart = from.FogStart;
        g_Contrast = from.Contrast;
        g_Saturation = from.Saturation;
        g_Temperature = from.Temperature;
        g_SSAOEnabled = from.SSAO;
        g_SSREnabled = from.SSR;
        g_SkyTop = from.SkyTop;
        g_SkyHorizon = from.SkyHorizon;
        g_GroundCol = from.SkyGround;
      };
      auto readPost = [&](RenderSettings &to) {
        to.Exposure = g_Exposure;
        to.AutoExposure = g_AutoExposure;
        to.BloomStrength = g_BloomStrength;
        to.BloomThreshold = g_BloomThreshold;
        to.Vignette = g_Vignette;
        to.FilmGrain = g_FilmGrain;
        to.ChromaticAberration = g_ChromaticAberration;
        to.FogDensity = g_FogDensity;
        to.FogColor = g_FogColor;
        to.FogStart = g_FogStart;
        to.Contrast = g_Contrast;
        to.Saturation = g_Saturation;
        to.Temperature = g_Temperature;
        to.SSAO = g_SSAOEnabled;
        to.SSR = g_SSREnabled;
        to.SkyTop = g_SkyTop;
        to.SkyHorizon = g_SkyHorizon;
        to.SkyGround = g_GroundCol;
      };

      // A script's look is applied on top of the editor's values, which come
      // back when Play stops.
      static RenderSettings s_EditorPost;
      static bool s_HasEditorPost = false;
      RenderSettings &rs = RenderSettings::Get();
      if (rs.ApplyPost) {
        rs.ApplyPost = false;
        if (!s_HasEditorPost) {
          readPost(s_EditorPost);
          s_HasEditorPost = true;
        }
        writePost(rs);
      }
      if (rs.RestorePost) {
        rs.RestorePost = false;
        if (s_HasEditorPost) {
          writePost(s_EditorPost);
          s_HasEditorPost = false;
        }
      }
    }

    Entity* renderCamera = m_Scene->GetPrimaryCameraEntity();
    glm::mat4 csmLightSpace[3] = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
    glm::mat4 lightSpaceMatrix(1.0f);
    bool shadowsActive = false;
    float shadowBiasScale = 1.0f;
    int shadowPCFRadius = 2;
    {
      glm::vec3 sunDir{0.3f, -1.0f, -0.3f};
      float shadowHalfSize = 25.0f;
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasLight && e.Light.Type == LightComponent::LightType::Directional &&
            e.Light.CastShadows) {
          glm::quat q(glm::quat_cast(m_Scene->GetWorldTransform(e)));
          sunDir = q * glm::vec3(0, 0, -1);
          shadowHalfSize = std::max(1.0f, e.Light.ShadowDistance);
          shadowBiasScale = std::max(0.01f, e.Light.ShadowBias);
          shadowPCFRadius = std::clamp(e.Light.ShadowPCFRadius, 0, 4);
          shadowsActive = true;
          break;
        }
      }
      if (shadowsActive) {

        glm::vec3 focus(0.0f);
        glm::vec3 focusForward(0.0f);
        if (m_AppState == AppState::Runtime) {
          if (auto *pc = renderCamera) {
            glm::mat4 camWorld = m_Scene->GetWorldTransform(*pc);
            focus = glm::vec3(camWorld[3]);
            // Push each cascade ahead of the camera so it covers what is on
            // screen instead of being centred on the eye.
            focusForward = -glm::normalize(glm::vec3(camWorld[2]));
          }
        } else {
          focus = m_Camera.GetPosition();
        }

        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_FRONT);
        depthShader->Bind();
        for (int c = 0; c < 3; ++c) {
          float halfSize = csmHalfSize[c];

          float texelSize = (2.0f * halfSize) / 2048.0f;
          glm::vec3 snapped = focus + focusForward * (halfSize * 0.75f);
          snapped.x = std::floor(snapped.x / texelSize) * texelSize;
          snapped.z = std::floor(snapped.z / texelSize) * texelSize;

          glm::vec3 sunPos = snapped - glm::normalize(sunDir) * (halfSize * 2.0f);
          glm::mat4 view = glm::lookAt(sunPos, snapped, glm::vec3(0, 1, 0));
          glm::mat4 proj = glm::ortho(-halfSize, halfSize,
                                      -halfSize, halfSize,
                                      0.1f, halfSize * 4.0f);
          csmLightSpace[c] = proj * view;

          shadowFBs[c]->Bind();
          glClear(GL_DEPTH_BUFFER_BIT);
          depthShader->UploadUniformMat4("u_LightSpaceMatrix", csmLightSpace[c]);
          for (const auto &e : m_Scene->GetEntities()) {
            if (!e.HasMeshRenderer || !e.MeshRenderer.Mesh) continue;
            if (!e.MeshRenderer.CastsShadow) continue;
            glm::mat4 model = m_Scene->GetWorldTransform(e);
            if (!BoundsVisible(csmLightSpace[c] * model, e.MeshRenderer.Mesh->GetAABBMin(), e.MeshRenderer.Mesh->GetAABBMax())) continue;
            depthShader->UploadUniformMat4("u_Transform", model);
            e.MeshRenderer.Mesh->GetVertexArray()->Bind();
            glDrawElements(GL_TRIANGLES,
                           e.MeshRenderer.Mesh->GetVertexArray()
                               ->GetIndexBuffer()->GetCount(),
                           GL_UNSIGNED_INT, nullptr);
          }
          shadowFBs[c]->Unbind();
        }
        glCullFace(GL_BACK);

        lightSpaceMatrix = csmLightSpace[0];
      }
    }

    bool vrFrameValid = xrFrameValid && !m_VRSystem.IsCinemaMode();
    int vrEyeCount = vrFrameValid ? 2 : 1;
    const auto& stereoLook = RenderSettings::Get().VR;
    const bool cleanStereo = vrFrameValid && stereoLook.Enabled;

    glm::mat4 viewProjection(1.0f);
    glm::mat4 viewMatrix(1.0f);
    glm::mat4 projMatrix(1.0f);

    for (int vrEye = 0; vrEye < vrEyeCount; vrEye++) {

    if (vrActive && vrFrameValid) {
      framebuffer->SetSamples(m_VRSystem.GetQuality() == 0 ? 1 : m_VRSystem.GetQuality() == 1 ? 2 : 4);
      float scale = m_VRSystem.GetRenderScale();
      resizeRenderTargets((uint32_t)(m_VRSystem.GetEyeWidth()*scale), (uint32_t)(m_VRSystem.GetEyeHeight()*scale));
      framebuffer->Bind();
    } else {
      framebuffer->SetSamples(4);
#ifndef GAME_MODE_RUNTIME
      framebuffer->Bind();
#else
      // The standalone runtime draws into the same HDR target as the editor so
      // the post-process stack applies; it is presented after the post pass.
      auto [dw, dh] = m_Window->GetDrawableSize();
      if (dw > 0 && dh > 0)
        resizeRenderTargets((uint32_t)dw, (uint32_t)dh);
      framebuffer->Bind();
      int winW = 0, winH = 0;
      SDL_GetWindowSize(m_Window->GetNativeWindow(), &winW, &winH);
      if (!vrActive) m_GameViewRect = {0.0f, 0.0f, (float)winW, (float)winH};
#endif
    }
    if (m_AppState == AppState::Editor) {
      framebuffer->Bind();
    } else if (m_AppState == AppState::Runtime && !(vrActive && vrFrameValid)) {
      glViewport(0, 0, fbSpec.Width, fbSpec.Height);
      m_Camera.SetViewportSize((float)fbSpec.Width, (float)fbSpec.Height);
    }

    RenderState::SetBlend(false);
    glm::vec4 skyClear = {m_Scene->GetSkyColor(), 1.0f};
    const float sceneClear[4] = {skyClear.r, skyClear.g, skyClear.b, skyClear.a};
    const float gbufferClear[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, sceneClear);
    glClearBufferfv(GL_COLOR, 1, gbufferClear);
    glClear(GL_DEPTH_BUFFER_BIT);

    auto skyTexture = m_Scene->GetSkyTexture();
    if (skyTexture) {
      static int backdropLog = 0;
      if (backdropLog++ % 600 == 0) {
        std::cout << "[Renderer] Rendering backdrop with texture: "
                  << m_Scene->GetSkyTexturePath()
                  << " (ID: " << skyTexture->GetID() << ")" << std::endl;
      }
      glDisable(GL_DEPTH_TEST);
      shader->Bind();
      skyTexture->Bind(0);
      shader->UploadUniformInt("u_Texture", 0);
      shader->UploadUniformInt("u_HasTexture", 1);
      shader->UploadUniformInt("u_IsUnlit", 1);
      shader->UploadUniformFloat4("u_Color", {1, 1, 1, 1});
      shader->UploadUniformFloat("u_TilingFactor", 1.0f);
      shader->UploadUniformInt("u_HasDiffuseMap", 0);
      shader->UploadUniformInt("u_HasNormalMap", 0);

      shader->UploadUniformMat4("u_ViewProjection", glm::mat4(1.0f));
      shader->UploadUniformMat4("u_Transform", glm::mat4(1.0f));

      Renderer::Submit(shader, m_BackdropVAO, glm::mat4(1.0f));

      shader->UploadUniformInt("u_IsUnlit", 0);
      glEnable(GL_DEPTH_TEST);
    } else if (!m_Scene->GetSkyTexturePath().empty()) {
      static int backdropFailLog = 0;
      if (backdropFailLog++ % 600 == 0) {
        std::cout
            << "[Renderer] Sky Texture Path set but GetSkyTexture() is NULL: "
            << m_Scene->GetSkyTexturePath() << std::endl;
      }
    }

    if (m_AppState == AppState::Runtime) {
      if (vrActive && vrFrameValid) {

        viewMatrix     = m_VRSystem.GetEyeViewMatrix(vrEye);
        projMatrix     = m_VRSystem.GetEyeProjectionMatrix(vrEye, 0.05f, 1000.0f);
        viewProjection = projMatrix * viewMatrix;
      } else {
        float aspectRatio = (float)fbSpec.Width / (float)fbSpec.Height;

        Entity *primaryCamera = renderCamera;
        if (primaryCamera) {
          glm::mat4 view       = glm::inverse(m_Scene->GetWorldTransform(*primaryCamera));
          glm::mat4 projection = primaryCamera->Camera.GetProjectionMatrix(aspectRatio);
          viewProjection = projection * view;
          viewMatrix     = view;
          projMatrix     = projection;
        } else {
          viewProjection = m_Camera.GetViewProjection();
          viewMatrix     = m_Camera.GetViewMatrix();
          projMatrix     = m_Camera.GetProjection();
        }
      }
    }

    else {
      viewProjection = m_Camera.GetViewProjection();
      viewMatrix = m_Camera.GetViewMatrix();
      projMatrix = m_Camera.GetProjection();
    }

    {
      uint32_t envCube = 0;
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasSkybox && e.Skybox.IsLoaded && e.Skybox.RendererID) {
          envCube = e.Skybox.RendererID;
          break;
        }
      }
      if (envCube && envCube != ibl.SourceCubemap) {
        precomputeIBL(envCube);

        glViewport(0, 0, fbSpec.Width, fbSpec.Height);
      }
      if (!envCube && ibl.Ready) {

        if (ibl.IrradianceMap) glDeleteTextures(1, &ibl.IrradianceMap);
        if (ibl.PrefilterMap)  glDeleteTextures(1, &ibl.PrefilterMap);
        if (ibl.BRDFLUT)       glDeleteTextures(1, &ibl.BRDFLUT);
        ibl = {};
      }
    }

    bool planarActive = false;
    glm::vec4 planarPlane(0.0f, 1.0f, 0.0f, 0.0f);
    for (const auto &e : m_Scene->GetEntities()) {
      if ((e.HasMeshRenderer && e.MeshRenderer.IsReflective) || e.HasWater) {
        const glm::mat4 reflectorWorld = m_Scene->GetWorldTransform(e);
        if (e.HasMeshRenderer && e.MeshRenderer.Mesh &&
            !BoundsVisible(viewProjection * reflectorWorld, e.MeshRenderer.Mesh->GetAABBMin(), e.MeshRenderer.Mesh->GetAABBMax())) continue;
        planarActive = true;
        const glm::mat4 world = m_Scene->GetWorldTransform(e);
        // Reflect across the entity's local XZ plane, including parent transforms.
        const glm::vec3 normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(world))) * glm::vec3(0, 1, 0));
        planarPlane = glm::vec4(normal, -glm::dot(normal, glm::vec3(world[3])));
        const glm::vec4 eye = glm::inverse(viewMatrix)[3];
        if (glm::dot(planarPlane, eye) < 0.0f)
          planarPlane = -planarPlane;
        break;
      }
    }
    glm::mat4 planarVP(1.0f);
    if (planarActive) {
      { // A reflection texture and its matrix must belong to this eye.
        glm::mat4 mirrorMat = PlanarReflectionMatrix(planarPlane);
        glm::mat4 mirrorView = viewMatrix * mirrorMat;
        planarVP = projMatrix * mirrorView;

        planarFB->Bind();
        glClearColor(m_Scene->GetSkyColor().r, m_Scene->GetSkyColor().g,
                     m_Scene->GetSkyColor().b, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glFrontFace(GL_CW);

      planarShader->Bind();
      planarShader->UploadUniformMat4("u_ViewProjection", planarVP);
      planarShader->UploadUniformFloat4("u_ClipPlane", planarPlane);
      planarShader->UploadUniformInt("u_DiffuseMap", 0);
      planarShader->UploadUniformInt("u_LinearMaterialColors", cleanStereo && stereoLook.LinearMaterialColors ? 1 : 0);

      glm::vec3 sunDirL{0.3f, -1.0f, -0.3f};
      glm::vec3 sunCol{1, 0.95f, 0.85f};
      float sunI = 1.0f;
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasLight && e.Light.Type == LightComponent::LightType::Directional) {
          glm::quat q(glm::quat_cast(m_Scene->GetWorldTransform(e)));
          sunDirL = q * glm::vec3(0, 0, -1);
          sunCol = e.Light.Color;
          sunI = e.Light.Intensity;
          break;
        }
      }
      planarShader->UploadUniformFloat3("u_SunDir",       sunDirL);
      planarShader->UploadUniformFloat3("u_SunColor",     sunCol);
      planarShader->UploadUniformFloat("u_SunIntensity",  sunI);
      planarShader->UploadUniformFloat3("u_AmbientColor", m_Scene->GetAmbientColor());
      planarShader->UploadUniformFloat("u_AmbientIntensity", m_Scene->GetAmbientIntensity());
      int reflectedLights = 0;
      for (const auto &light : m_Scene->GetEntities()) {
        if (!light.HasLight || light.Light.Type != LightComponent::LightType::Point) continue;
        const std::string index = std::to_string(reflectedLights);
        planarShader->UploadUniformFloat4("u_PointPositionRadius[" + index + "]",
            glm::vec4(glm::vec3(m_Scene->GetWorldTransform(light)[3]), light.Light.Radius));
        planarShader->UploadUniformFloat3("u_PointColor[" + index + "]", light.Light.Color * light.Light.Intensity);
        if (++reflectedLights == 8) break;
      }
      planarShader->UploadUniformInt("u_PointCount", reflectedLights);

      for (const auto &entity : m_Scene->GetEntities()) {
        if (!entity.HasMeshRenderer || !entity.MeshRenderer.Mesh) continue;
        if (entity.IsUIElement()) continue;

        if (entity.MeshRenderer.IsReflective) continue;

        if (m_AppState == AppState::Runtime) {
          Entity *pc = renderCamera;
          if (pc && entity.GetID() == pc->GetID()) continue;
        }
        glm::mat4 model = m_Scene->GetWorldTransform(entity);
        if (!BoundsVisible(planarVP * model, entity.MeshRenderer.Mesh->GetAABBMin(), entity.MeshRenderer.Mesh->GetAABBMax())) continue;
        planarShader->UploadUniformMat4("u_Transform", model);
        planarShader->UploadUniformFloat4("u_Albedo", entity.MeshRenderer.Color);
        // ShadowOnly parts (the local head and hat) are visible in reflections.
        planarShader->UploadUniformInt("u_HasDiffuseMap", entity.MeshRenderer.DiffuseMap ? 1 : 0);
        if (entity.MeshRenderer.DiffuseMap)
          entity.MeshRenderer.DiffuseMap->Bind(0);
        entity.MeshRenderer.Mesh->GetVertexArray()->Bind();
        glDrawElements(GL_TRIANGLES,
                       entity.MeshRenderer.Mesh->GetVertexArray()
                           ->GetIndexBuffer()->GetCount(),
                       GL_UNSIGNED_INT, nullptr);
      }
      glFrontFace(GL_CCW);

      {
        glDepthFunc(GL_LEQUAL);
        GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        skyboxShader->Bind();
        glm::mat4 mirroredViewNoTrans = glm::mat4(glm::mat3(viewMatrix * mirrorMat));
        skyboxShader->UploadUniformMat4("view", mirroredViewNoTrans);
        skyboxShader->UploadUniformMat4("projection", projMatrix);
        skyboxShader->UploadUniformInt("u_UseProcedural", g_UseProceduralSky ? 1 : 0);
        glm::vec3 sunD{0.3f, -1.0f, -0.3f};
        for (const auto &e : m_Scene->GetEntities()) {
          if (e.HasLight && e.Light.Type == LightComponent::LightType::Directional) {
            glm::quat q(glm::quat_cast(m_Scene->GetWorldTransform(e)));
            sunD = glm::normalize(q * glm::vec3(0, 0, -1));
            break;
          }
        }
        skyboxShader->UploadUniformFloat3("u_SunDir",      sunD);
        skyboxShader->UploadUniformFloat3("u_SkyTop",      g_SkyTop);
        skyboxShader->UploadUniformFloat3("u_SkyHorizon",  g_SkyHorizon);
        skyboxShader->UploadUniformFloat3("u_GroundColor", g_GroundCol);
        skyboxShader->UploadUniformFloat("u_SunDiscSize",  g_SunDiscSize);
        skyboxShader->UploadUniformFloat("u_SunIntensity", g_SunDiscIntensity);
        skyboxVAO->Bind();
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthFunc(GL_LESS);
        if (cullWas) glEnable(GL_CULL_FACE);
      }
      planarFB->Unbind();
      }
      framebuffer->Bind();
    }

    shader->Bind();
    shader->UploadUniformMat4("u_ViewProjection", viewProjection);
    shader->UploadUniformMat4("u_LightSpaceMatrix", lightSpaceMatrix);
    shader->UploadUniformInt("u_ShadowsEnabled", shadowsActive ? 1 : 0);
    shader->UploadUniformFloat("u_ShadowBiasScale", shadowBiasScale);
    shader->UploadUniformInt("u_ShadowPCFRadius", shadowPCFRadius);
    shader->UploadUniformMat3("u_ViewRotation", glm::mat3(viewMatrix));
    shader->UploadUniformInt("u_LinearMaterialColors", cleanStereo && stereoLook.LinearMaterialColors ? 1 : 0);
    shader->UploadUniformFloat("u_MinRoughness", cleanStereo ? glm::clamp(stereoLook.MinRoughness, .04f, 1.f) : .04f);
    shader->UploadUniformFloat2("u_AmbientHemisphere", cleanStereo ? glm::vec2(stereoLook.AmbientSky, stereoLook.AmbientGround) : glm::vec2(1.6f,.35f));

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D,
                  planarActive ? planarFB->GetColorAttachmentRendererID() : 0);
    shader->UploadUniformInt("u_PlanarReflection", 7);
    shader->UploadUniformMat4("u_ViewProjectionPlanar", planarVP);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowFBs[0]->GetDepthAttachmentRendererID());
    shader->UploadUniformInt("u_ShadowMap0", 3);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, shadowFBs[1]->GetDepthAttachmentRendererID());
    shader->UploadUniformInt("u_ShadowMap1", 10);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, shadowFBs[2]->GetDepthAttachmentRendererID());
    shader->UploadUniformInt("u_ShadowMap2", 11);
    shader->UploadUniformMat4("u_LightSpace0", csmLightSpace[0]);
    shader->UploadUniformMat4("u_LightSpace1", csmLightSpace[1]);
    shader->UploadUniformMat4("u_LightSpace2", csmLightSpace[2]);
    shader->UploadUniformFloat("u_CsmSplit0", csmSplitDist[0]);
    shader->UploadUniformFloat("u_CsmSplit1", csmSplitDist[1]);
    shader->UploadUniformFloat("u_CsmSplit2", csmSplitDist[2]);

    if (ibl.Ready) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.IrradianceMap);
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ibl.PrefilterMap);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D, ibl.BRDFLUT);
      shader->UploadUniformInt("u_HasIBL", 1);
    } else {
      shader->UploadUniformInt("u_HasIBL", 0);
    }

    if (m_AppState == AppState::Runtime) {
      Entity *primaryCamera = renderCamera;
      if (primaryCamera && primaryCamera->HasCamera) {
        if (primaryCamera->Camera.AntiAliasing)
          glEnable(GL_MULTISAMPLE);
        else
          glDisable(GL_MULTISAMPLE);
      }
    } else {
      glEnable(GL_MULTISAMPLE);
    }

    if (m_AppState == AppState::Runtime) {
      Entity *primaryCamera = renderCamera;
    }

    glm::vec3 cameraPos = m_Camera.GetPosition();
    if (vrActive && vrFrameValid) {
      cameraPos = glm::vec3(glm::inverse(viewMatrix)[3]);
    } else if (m_AppState == AppState::Runtime) {
      Entity *primaryCamera = renderCamera;
      if (primaryCamera)
        cameraPos = glm::vec3(m_Scene->GetWorldTransform(*primaryCamera)[3]);
    }
    shader->UploadUniformFloat3("u_ViewPos", cameraPos);

    glm::vec3 sunDirection(-0.5f, -1.0f, -0.5f);
    glm::vec3 sunColor(1.0f);
    float sunIntensity = 1.0f;
    bool hasSun = false;

    struct PointLight {
      glm::vec3 Pos;
      glm::vec3 Color;
      float Intensity;
      float Radius;
    };
    std::vector<PointLight> pointLights;

    struct SpotLight {
      glm::vec3 Pos; glm::vec3 Dir; glm::vec3 Color;
      float Intensity; float Radius; float CosInner; float CosOuter;
    };
    struct AreaLight {
      glm::vec3 Pos; glm::vec3 Fwd; glm::vec3 Right; glm::vec3 Up;
      glm::vec3 Color; float Intensity; float Radius; glm::vec2 Size;
    };
    std::vector<SpotLight> spotLights;
    std::vector<AreaLight> areaLights;

    for (const auto &entity : m_Scene->GetEntities()) {
      if (entity.HasLight) {
        auto world = m_Scene->GetWorldTransform(entity);
        glm::quat q = glm::quat_cast(world);
        glm::vec3 fwd = glm::normalize(q * glm::vec3(0, 0, -1));
        glm::vec3 right = glm::normalize(q * glm::vec3(1, 0, 0));
        glm::vec3 up    = glm::normalize(q * glm::vec3(0, 1, 0));
        glm::vec3 pos   = glm::vec3(world[3]);

        switch (entity.Light.Type) {
        case LightComponent::LightType::Directional:
          sunDirection = fwd;
          sunColor = entity.Light.Color;
          sunIntensity = entity.Light.Intensity;
          hasSun = true;
          break;
        case LightComponent::LightType::Point:
          pointLights.push_back({pos, entity.Light.Color,
                                 entity.Light.Intensity, entity.Light.Radius});
          break;
        case LightComponent::LightType::Spot: {
          SpotLight s;
          s.Pos = pos; s.Dir = fwd; s.Color = entity.Light.Color;
          s.Intensity = entity.Light.Intensity; s.Radius = entity.Light.Radius;
          s.CosInner = std::cos(glm::radians(entity.Light.SpotInner));
          s.CosOuter = std::cos(glm::radians(entity.Light.SpotOuter));
          spotLights.push_back(s);
          break;
        }
        case LightComponent::LightType::Area: {
          AreaLight a;
          a.Pos = pos; a.Fwd = fwd; a.Right = right; a.Up = up;
          a.Color = entity.Light.Color; a.Intensity = entity.Light.Intensity;
          a.Radius = entity.Light.Radius; a.Size = entity.Light.AreaSize;
          areaLights.push_back(a);
          break;
        }
        }
      }
    }

    shader->UploadUniformFloat3("u_SunDirection", sunDirection);
    shader->UploadUniformFloat3("u_SunColor", sunColor);
    shader->UploadUniformFloat("u_SunIntensity", hasSun ? sunIntensity : 0.0f);
    shader->UploadUniformFloat3("u_AmbientColor", m_Scene->GetAmbientColor());
    shader->UploadUniformFloat("u_AmbientIntensity",
                               m_Scene->GetAmbientIntensity());

    // The shader has eight slots. Sending the full scene count read beyond its
    // arrays; distant garage lights also displaced the kitchen's own lights.
    // Rank around the centre of the head so both eyes select the same lights.
    glm::vec3 lightFocus = vrFrameValid ? glm::vec3(m_VRSystem.GetWorldHeadPose()[3]) : cameraPos;
    auto relevance = [&](const auto& light) {
      float distance = glm::distance(light.Pos, lightFocus);
      float radius = std::max(light.Radius, .01f);
      return std::max(light.Intensity, 0.0f) * radius * radius / (radius * radius + distance * distance);
    };
    std::stable_sort(pointLights.begin(), pointLights.end(), [&](const auto& a, const auto& b) { return relevance(a) > relevance(b); });
    std::stable_sort(spotLights.begin(), spotLights.end(), [&](const auto& a, const auto& b) { return relevance(a) > relevance(b); });
    std::stable_sort(areaLights.begin(), areaLights.end(), [&](const auto& a, const auto& b) { return relevance(a) > relevance(b); });
    shader->UploadUniformInt("u_PointLightCount", (int)std::min((size_t)8,pointLights.size()));
    for (int i = 0; i < (int)pointLights.size() && i < 8; i++) {
      std::string iStr = std::to_string(i);
      shader->UploadUniformFloat3("u_PointLightPositions[" + iStr + "]", pointLights[i].Pos);
      shader->UploadUniformFloat3("u_PointLightColors[" + iStr + "]", pointLights[i].Color);
      shader->UploadUniformFloat("u_PointLightIntensities[" + iStr + "]", pointLights[i].Intensity);
      shader->UploadUniformFloat("u_PointLightRadii[" + iStr + "]", pointLights[i].Radius);
    }

    shader->UploadUniformInt("u_SpotLightCount", (int)std::min((size_t)4, spotLights.size()));
    for (int i = 0; i < (int)spotLights.size() && i < 4; ++i) {
      std::string s = std::to_string(i);
      shader->UploadUniformFloat3("u_SpotPositions[" + s + "]",  spotLights[i].Pos);
      shader->UploadUniformFloat3("u_SpotDirections[" + s + "]", spotLights[i].Dir);
      shader->UploadUniformFloat3("u_SpotColors[" + s + "]",     spotLights[i].Color);
      shader->UploadUniformFloat("u_SpotIntensities[" + s + "]", spotLights[i].Intensity);
      shader->UploadUniformFloat("u_SpotRadii[" + s + "]",       spotLights[i].Radius);
      shader->UploadUniformFloat("u_SpotCosInner[" + s + "]",    spotLights[i].CosInner);
      shader->UploadUniformFloat("u_SpotCosOuter[" + s + "]",    spotLights[i].CosOuter);
    }

    shader->UploadUniformInt("u_AreaLightCount", (int)std::min((size_t)4, areaLights.size()));
    for (int i = 0; i < (int)areaLights.size() && i < 4; ++i) {
      std::string s = std::to_string(i);
      shader->UploadUniformFloat3("u_AreaPositions[" + s + "]",   areaLights[i].Pos);
      shader->UploadUniformFloat3("u_AreaForwards[" + s + "]",    areaLights[i].Fwd);
      shader->UploadUniformFloat3("u_AreaRights[" + s + "]",      areaLights[i].Right);
      shader->UploadUniformFloat3("u_AreaUps[" + s + "]",         areaLights[i].Up);
      shader->UploadUniformFloat3("u_AreaColors[" + s + "]",      areaLights[i].Color);
      shader->UploadUniformFloat("u_AreaIntensities[" + s + "]",  areaLights[i].Intensity);
      shader->UploadUniformFloat("u_AreaRadii[" + s + "]",        areaLights[i].Radius);
      shader->UploadUniformFloat2("u_AreaSizes[" + s + "]",       areaLights[i].Size);
    }

    for (const auto &entity : m_Scene->GetEntities()) {
      if (entity.IsUIElement()) {
        continue;
      }

      if (!entity.HasSpriteRenderer) {
        continue;
      }

      if (m_AppState == AppState::Runtime) {
        Entity *primaryCamera = renderCamera;
        if (primaryCamera && entity.GetID() == primaryCamera->GetID())
          continue;
      }

      glm::mat4 transform = m_Scene->GetWorldTransform(entity);

      if (entity.SpriteRenderer.Texture) {
        entity.SpriteRenderer.Texture->Bind(0);
        shader->UploadUniformInt("u_Texture", 0);
        shader->UploadUniformInt("u_HasTexture", 1);
      } else {
        shader->UploadUniformInt("u_HasTexture", 0);
      }

      shader->UploadUniformInt("u_HasDiffuseMap", 0);
      shader->UploadUniformInt("u_HasNormalMap", 0);

      shader->UploadUniformFloat("u_TilingFactor",
                                 entity.SpriteRenderer.TilingFactor);
      shader->UploadUniformFloat4("u_Color", entity.SpriteRenderer.Color);
      shader->UploadUniformInt("u_ReceiveShadows", 0);
      shader->UploadUniformFloat("u_MaterialSSRIntensity", 0.0f);
      Renderer::Submit(shader, m_VertexArray, transform);
    }

    // Three passes: the opaque scene; see-through meshes (Color alpha below
    // 1, like window glass), blended over it without writing depth or the
    // G-buffer; then overlay meshes squeezed into the front of the depth range
    // so nothing in the world can cover them.
    for (int scenePass = 0; scenePass < 3; ++scenePass) {
    bool translucentPass = scenePass == 1, overlayPass = scenePass == 2;
    if (translucentPass) {
      RenderState::SetBlend(true);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
      glDepthMask(GL_FALSE);
      glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    } else if (overlayPass) {
      glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_TRUE);
      RenderState::SetDefaultBlendFunc();
      RenderState::SetBlend(false);
      glDepthRange(0.0, 0.1);
    }
    shader->UploadUniformInt("u_Translucent", translucentPass ? 1 : 0);
    for (auto &entity : m_Scene->GetEntities()) {
      if (!entity.HasMeshRenderer || !entity.MeshRenderer.Mesh)
        continue;

      if (entity.MeshRenderer.Overlay != overlayPass)
        continue;
      if (!overlayPass && (entity.MeshRenderer.Color.a < 0.999f) != translucentPass)
        continue;

      if (entity.MeshRenderer.ShadowOnly)
        continue;

      if (entity.IsUIElement())
        continue;

      if (m_AppState == AppState::Runtime) {
        Entity *primaryCamera = renderCamera;
        if (primaryCamera && entity.GetID() == primaryCamera->GetID())
          continue;
      }

      glm::mat4 transform = m_Scene->GetWorldTransform(entity);

      if ((vrActive || (renderCamera && renderCamera->Camera.FrustumCulling)) &&
          !BoundsVisible(viewProjection * transform, entity.MeshRenderer.Mesh->GetAABBMin(), entity.MeshRenderer.Mesh->GetAABBMax())) continue;

      std::shared_ptr<Mesh> meshToRender = entity.MeshRenderer.Mesh;
      if (!entity.MeshRenderer.LODs.empty()) {
        float dist =
            glm::distance(cameraPos, glm::vec3(transform[3]));
        for (const auto &lod : entity.MeshRenderer.LODs) {
          if (dist < lod.Distance) {
            meshToRender = lod.Mesh;
            break;
          }
        }
      }

      shader->UploadUniformInt("u_HasTexture", 0);
      // Sprites drawn earlier leave their own tiling behind; meshes use their UVs as they are.
      shader->UploadUniformFloat("u_TilingFactor", 1.0f);

      if (entity.MeshRenderer.DiffuseMap) {
        entity.MeshRenderer.DiffuseMap->Bind(1);
        shader->UploadUniformInt("u_DiffuseMap", 1);
        shader->UploadUniformInt("u_HasDiffuseMap", 1);
      } else {
        shader->UploadUniformInt("u_HasDiffuseMap", 0);
      }

      if (entity.MeshRenderer.NormalMap) {
        entity.MeshRenderer.NormalMap->Bind(2);
        shader->UploadUniformInt("u_NormalMap", 2);
        shader->UploadUniformInt("u_HasNormalMap", 1);
      } else {
        shader->UploadUniformInt("u_HasNormalMap", 0);
      }

      shader->UploadUniformFloat4("u_Color", entity.MeshRenderer.Color);
      shader->UploadUniformFloat("u_Metallic",  entity.MeshRenderer.Metallic);
      shader->UploadUniformFloat("u_Roughness", entity.MeshRenderer.Roughness);
      shader->UploadUniformFloat("u_AO",        entity.MeshRenderer.AO);
      shader->UploadUniformInt("u_ReceiveShadows",
                               entity.MeshRenderer.ReceivesShadow ? 1 : 0);
      shader->UploadUniformFloat("u_MaterialSSRIntensity",
                                 entity.MeshRenderer.ReceivesSSR
                                     ? entity.MeshRenderer.SSRIntensity
                                     : 0.0f);
      shader->UploadUniformInt("u_HasPlanarReflection",
                               (planarActive && entity.MeshRenderer.IsReflective) ? 1 : 0);
      Renderer::SubmitMesh(meshToRender, shader, transform);
    }
    }
    glDepthRange(0.0, 1.0);

    if (m_AppState == AppState::Editor) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      glLineWidth(2.0f);
      shader->UploadUniformFloat4("u_Color", {1.0f, 0.5f, 0.0f, 1.0f});
      shader->UploadUniformInt("u_HasTexture", 0);
      shader->UploadUniformInt("u_HasDiffuseMap", 0);
      shader->UploadUniformInt("u_HasNormalMap", 0);

      for (int selectedID : m_SceneHierarchyPanel.GetSelectedEntities()) {
        for (const auto &entity : m_Scene->GetEntities()) {
          if (entity.GetID() == selectedID) {
            if (entity.IsUIElement())
              continue;

            glm::mat4 transform = m_Scene->GetWorldTransform(entity);

            if (entity.HasMeshRenderer && entity.MeshRenderer.Mesh) {
              Renderer::SubmitMesh(entity.MeshRenderer.Mesh, shader, transform);
            } else if (entity.HasSpriteRenderer) {
              Renderer::Submit(shader, m_VertexArray, transform);
            }
            break;
          }
        }
      }
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    {
      bool anyWater = false;
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasWater && e.HasMeshRenderer && e.MeshRenderer.Mesh) { anyWater = true; break; }
      }
      if (anyWater) {

        framebuffer->BlitColorTo(*resolveFB, 0);
        framebuffer->BlitDepthTo(*resolveFB);

        framebuffer->Bind();

        waterShader->Bind();
        waterShader->UploadUniformMat4("u_ViewProjection", viewProjection);
        waterShader->UploadUniformFloat3("u_ViewPos", cameraPos);
        waterShader->UploadUniformFloat("u_Time", (float)SDL_GetTicks() * 0.001f);
        waterShader->UploadUniformFloat3("u_SunDir", sunDirection);
        waterShader->UploadUniformFloat3("u_SunColor", sunColor);
        waterShader->UploadUniformFloat("u_SunIntensity", hasSun ? sunIntensity : 0.0f);
        waterShader->UploadUniformFloat("u_Near", 0.1f);
        waterShader->UploadUniformFloat("u_Far", 1000.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID(0));
        waterShader->UploadUniformInt("u_SceneColor", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, resolveFB->GetDepthAttachmentRendererID());
        waterShader->UploadUniformInt("u_SceneDepth", 1);
        glActiveTexture(GL_TEXTURE2);
        if (planarActive)
          glBindTexture(GL_TEXTURE_2D, planarFB->GetColorAttachmentRendererID());
        else
          glBindTexture(GL_TEXTURE_2D, 0);
        waterShader->UploadUniformInt("u_PlanarReflection", 2);
        waterShader->UploadUniformInt("u_HasPlanarReflection", planarActive ? 1 : 0);

        glEnable(GL_DEPTH_TEST);
        GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        for (const auto &e : m_Scene->GetEntities()) {
          if (!e.HasWater || !e.HasMeshRenderer || !e.MeshRenderer.Mesh) continue;
          glm::mat4 model = m_Scene->GetWorldTransform(e);
          waterShader->UploadUniformMat4("u_Transform", model);
          waterShader->UploadUniformFloat3("u_ShallowColor", e.Water.ShallowColor);
          waterShader->UploadUniformFloat3("u_DeepColor",    e.Water.DeepColor);
          waterShader->UploadUniformFloat3("u_FoamColor",    e.Water.FoamColor);
          waterShader->UploadUniformFloat("u_Amplitude",     e.Water.WaveAmplitude);
          waterShader->UploadUniformFloat("u_Steepness",     e.Water.WaveSteepness);
          waterShader->UploadUniformFloat("u_Speed",         e.Water.WaveSpeed);
          waterShader->UploadUniformFloat("u_Scale",         e.Water.WaveScale);
          waterShader->UploadUniformFloat("u_FresnelPower",  e.Water.FresnelPower);
          waterShader->UploadUniformFloat("u_Refraction",    e.Water.Refraction);
          waterShader->UploadUniformFloat("u_DepthFade",     e.Water.DepthFade);
          waterShader->UploadUniformFloat("u_FoamThreshold", e.Water.FoamThreshold);
          e.MeshRenderer.Mesh->GetVertexArray()->Bind();
          glDrawElements(GL_TRIANGLES,
                         e.MeshRenderer.Mesh->GetVertexArray()
                             ->GetIndexBuffer()->GetCount(),
                         GL_UNSIGNED_INT, nullptr);
        }
        if (wasCullEnabled) glEnable(GL_CULL_FACE);
      }
    }

    {
      uint32_t skyCube = 0;
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasSkybox && e.Skybox.IsLoaded && e.Skybox.RendererID) {
          skyCube = e.Skybox.RendererID;
          break;
        }
      }
      if (skyCube || g_UseProceduralSky) {

        glm::mat4 viewNoTrans(1.0f), proj(1.0f);
        if (vrActive && vrFrameValid) {

          viewNoTrans = glm::mat4(glm::mat3(viewMatrix));
          proj        = projMatrix;
        } else if (m_AppState == AppState::Runtime) {
          Entity *pc = renderCamera;
          if (pc) {
            glm::quat q(glm::quat_cast(m_Scene->GetWorldTransform(*pc)));
            glm::mat4 rot = glm::mat4_cast(q);
            viewNoTrans = glm::mat4(glm::mat3(glm::inverse(rot)));
            proj = glm::perspective(glm::radians(pc->Camera.PerspectiveFOV),
                                    (float)fbSpec.Width / (float)fbSpec.Height,
                                    pc->Camera.PerspectiveNear,
                                    1000.0f);
          }
        } else {
          viewNoTrans = glm::mat4(glm::mat3(m_Camera.GetViewMatrix()));
          proj = m_Camera.GetProjection();
        }
        glDepthFunc(GL_LEQUAL);
        GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        skyboxShader->Bind();
        skyboxShader->UploadUniformMat4("view", viewNoTrans);
        skyboxShader->UploadUniformMat4("projection", proj);
        glActiveTexture(GL_TEXTURE0);
        if (skyCube) glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube);
        skyboxShader->UploadUniformInt("skybox", 0);

        skyboxShader->UploadUniformInt("u_UseProcedural", g_UseProceduralSky ? 1 : 0);

        glm::vec3 sunDir{0.3f, -1.0f, -0.3f};
        for (const auto &e : m_Scene->GetEntities()) {
          if (e.HasLight && e.Light.Type == LightComponent::LightType::Directional) {
            glm::quat q(glm::quat_cast(m_Scene->GetWorldTransform(e)));
            sunDir = glm::normalize(q * glm::vec3(0, 0, -1));
            break;
          }
        }
        skyboxShader->UploadUniformFloat3("u_SunDir",      sunDir);
        skyboxShader->UploadUniformFloat3("u_SkyTop",      g_SkyTop);
        skyboxShader->UploadUniformFloat3("u_SkyHorizon",  g_SkyHorizon);
        skyboxShader->UploadUniformFloat3("u_GroundColor", g_GroundCol);
        skyboxShader->UploadUniformFloat("u_SunDiscSize",  g_SunDiscSize);
        skyboxShader->UploadUniformFloat("u_SunIntensity", g_SunDiscIntensity);
        skyboxVAO->Bind();
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthFunc(GL_LESS);
        if (wasCullEnabled) glEnable(GL_CULL_FACE);
      }
    }

    {
      static thread_local std::vector<float> s_ParticleUpload;
      bool anyParticles = false;
      for (auto &e : m_Scene->GetEntities()) {
        if (e.HasParticleSystem) { anyParticles = true; break; }
      }
      if (anyParticles) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        bool wasCullEnabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);

        particleShader->Bind();
        particleShader->UploadUniformMat4("u_View", viewMatrix);
        particleShader->UploadUniformMat4("u_Projection", projMatrix);
        particleShader->UploadUniformInt("u_HasTexture", 0);
        glBindVertexArray(particleVAO);

        glm::vec3 camPos = glm::vec3(glm::inverse(viewMatrix)[3]);

        for (auto &e : m_Scene->GetEntities()) {
          if (!e.HasParticleSystem) continue;
          auto &ps = e.ParticleSystem;

          if (!ps.Initialized || (int)ps.Particles.size() != ps.MaxParticles) {
            ps.Particles.assign(ps.MaxParticles, ParticleSystemComponent::Particle{});
            ps.Initialized = true;
          }
          glm::vec3 emitPos = glm::vec3(m_Scene->GetWorldTransform(e)[3]);

          if (vrEye == 0) { // Simulate once, then show identical particles to both eyes.
          ps.EmitAccumulator += ts * ps.EmitRate;
          int toEmit = (int)ps.EmitAccumulator;
          ps.EmitAccumulator -= (float)toEmit;
          if (!ps.Loop && toEmit > 0) {

          }
          for (auto &p : ps.Particles) {

            if (p.Age < p.MaxAge) {
              p.Age += ts;
              p.Vel += ps.Gravity * ts;
              p.Pos += p.Vel * ts;
            } else if (toEmit > 0 && (ps.Loop || p.MaxAge == 0.0f)) {

              auto rnd = []() { return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f; };
              p.Pos = emitPos;
              p.Vel = ps.StartVelocity + ps.VelocityVariance *
                      glm::vec3(rnd(), rnd(), rnd());
              p.Age = 0.0f;
              p.MaxAge = ps.Lifetime;
              p.Size = ps.StartSize;
              p.Color = ps.StartColor;
              --toEmit;
            }

            if (p.MaxAge > 0.0f) {
              float t = glm::clamp(p.Age / p.MaxAge, 0.0f, 1.0f);
              p.Size  = glm::mix(ps.StartSize, ps.EndSize, t);
              p.Color = glm::mix(ps.StartColor, ps.EndColor, t);
            }
          }

          }

          struct Live { glm::vec3 pos; float size; glm::vec4 color; float depth; };
          static thread_local std::vector<Live> live;
          live.clear();
          live.reserve(ps.Particles.size());
          for (auto &p : ps.Particles) {
            if (p.Age >= p.MaxAge || p.MaxAge == 0.0f) continue;
            Live l;
            l.pos = p.Pos; l.size = p.Size; l.color = p.Color;
            l.depth = glm::dot(p.Pos - camPos, p.Pos - camPos);
            live.push_back(l);
          }
          std::sort(live.begin(), live.end(),
                    [](const Live &a, const Live &b) { return a.depth > b.depth; });
          if (live.empty()) continue;

          s_ParticleUpload.clear();
          s_ParticleUpload.reserve(live.size() * 8);
          for (auto &l : live) {
            s_ParticleUpload.push_back(l.pos.x);
            s_ParticleUpload.push_back(l.pos.y);
            s_ParticleUpload.push_back(l.pos.z);
            s_ParticleUpload.push_back(l.size);
            s_ParticleUpload.push_back(l.color.r);
            s_ParticleUpload.push_back(l.color.g);
            s_ParticleUpload.push_back(l.color.b);
            s_ParticleUpload.push_back(l.color.a);
          }

          glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
          GLsizeiptr bytes = (GLsizeiptr)(s_ParticleUpload.size() * sizeof(float));
          GLsizeiptr cap   = 64 * 1024 * 32;
          if (bytes > cap) bytes = cap;
          glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, s_ParticleUpload.data());

          if (ps.AdditiveBlend) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
          else                  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

          glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)live.size());
        }

        glBindVertexArray(0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        if (wasCullEnabled) glEnable(GL_CULL_FACE);
      }
    }

    if (m_AppState != AppState::Runtime) {
      if (m_Grid)
        m_Grid->Draw(m_Camera);
    }

    if (m_AppState != AppState::Runtime) {
      for (const auto &entity : m_Scene->GetEntities()) {
        if (entity.HasCamera) {
          glm::mat4 worldTransform = m_Scene->GetWorldTransform(entity);
          glm::quat q(glm::quat_cast(worldTransform));
          glm::vec3 forward = q * glm::vec3(0, 0, -1);
          glm::vec3 right = q * glm::vec3(1, 0, 0);
          glm::vec3 up = q * glm::vec3(0, 1, 0);

          glm::vec3 p0 = glm::vec3(worldTransform[3]);
          float dist = 2.0f;
          glm::vec3 p1 = p0 + forward * dist;

          float aspectRatio = 1.778f;
          float hSize, wSize;

          if (entity.Camera.Type ==
              CameraComponent::ProjectionType::Perspective) {
            hSize =
                dist * tan(glm::radians(entity.Camera.PerspectiveFOV) * 0.5f);
            wSize = hSize * aspectRatio;
          } else {
            hSize = entity.Camera.OrthographicSize * 0.5f;
            wSize = hSize * aspectRatio;
          }

          Renderer::DrawLine(p0, p1, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());

          glm::vec3 v0 = p1 + (-right * wSize - up * hSize);
          glm::vec3 v1 = p1 + (right * wSize - up * hSize);
          glm::vec3 v2 = p1 + (right * wSize + up * hSize);
          glm::vec3 v3 = p1 + (-right * wSize + up * hSize);

          Renderer::DrawLine(v0, v1, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(v1, v2, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(v2, v3, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(v3, v0, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());

          Renderer::DrawLine(p0, v0, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(p0, v1, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(p0, v2, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
          Renderer::DrawLine(p0, v3, {1, 1, 0, 1},
                             m_Camera.GetViewProjection());
        }
      }

      for (const auto &entity : m_Scene->GetEntities()) {
        if (entity.IsUIElement())
          continue;

        if (!m_SceneHierarchyPanel.IsSelected(entity.GetID()))
          continue;

        if (entity.HasBoxCollider) {
          glm::vec3 size = entity.BoxCollider.Size;

          glm::mat4 colliderTransform =
              m_Scene->GetWorldTransform(entity) *
              glm::translate(glm::mat4(1.0f), entity.BoxCollider.Offset) *
              glm::scale(glm::mat4(1.0f), size);

          Renderer::DrawWireBox(colliderTransform, {0.0f, 1.0f, 0.0f, 1.0f},
                                m_Camera.GetViewProjection());
        }
      }
    }

    if (m_AppState == AppState::Editor) {
      m_Scene->OnUIRender((float)fbSpec.Width, (float)fbSpec.Height,
                          m_ViewportOffset);
    } else {
      auto [dw, dh] = m_Window->GetDrawableSize();
      static int s_FrameCount = 0;
      if (s_FrameCount++ % 60 == 0) {
        int uiCount = 0;
        for (auto &e : m_Scene->GetEntities()) {
          if (e.IsUIElement())
            uiCount++;
        }
        std::cout << "[Runtime UI Debug] WindowSize: " << dw << "x" << dh
                  << " | Total Entities: " << m_Scene->GetEntities().size()
                  << " | UI Entities: " << uiCount << std::endl;
      }
#ifdef GAME_MODE_RUNTIME
      // Without VR the UI is drawn after the post pass, straight to the window.
      // The VR UI is submitted once, as its own compositor layer below.
#else
      m_Scene->OnUIRender((float)fbSpec.Width, (float)fbSpec.Height,
                          m_ViewportOffset);
#endif
    }

    if (m_AppState == AppState::Editor) {
      framebuffer->Unbind();
    }

    // Every eye uses the same HDR, shadows, reflections and tone mapping as desktop.
    RenderState::SetBlend(false);

    framebuffer->BlitColorTo(*resolveFB, 0);
    framebuffer->BlitColorTo(*resolveFB, 1);
    framebuffer->BlitDepthTo(*resolveFB);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(fsQuadVAO);

    if (g_SSREnabled && !vrFrameValid) {
      ssrFB->Bind();
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ssrShader->Bind();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID(0));
      ssrShader->UploadUniformInt("u_Scene", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetDepthAttachmentRendererID());
      ssrShader->UploadUniformInt("u_Depth", 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID(1));
      ssrShader->UploadUniformInt("u_GBuffer", 2);
      ssrShader->UploadUniformMat4("u_Projection", projMatrix);
      ssrShader->UploadUniformMat4("u_InvProjection", glm::inverse(projMatrix));
      ssrShader->UploadUniformFloat("u_Strength", g_SSRStrength);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    bool useSSAO = (cleanStereo ? stereoLook.ContactAO : g_SSAOEnabled) && (!vrActive || m_VRSystem.GetQuality() > 0);
    if (useSSAO) {
      ssaoFB->Bind();
      glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ssaoShader->Bind();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetDepthAttachmentRendererID());
      ssaoShader->UploadUniformInt("u_Depth", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID(1));
      ssaoShader->UploadUniformInt("u_Normals", 1);
      ssaoShader->UploadUniformMat3("u_ViewRotation", glm::mat3(viewMatrix));
      ssaoShader->UploadUniformMat4("u_Projection", projMatrix);
      ssaoShader->UploadUniformMat4("u_InvProjection", glm::inverse(projMatrix));
      ssaoShader->UploadUniformFloat("u_Radius", cleanStereo ? stereoLook.AORadius : g_SSAORadius);
      ssaoShader->UploadUniformFloat("u_Bias", cleanStereo ? stereoLook.AOBias : g_SSAOBias);
      ssaoShader->UploadUniformFloat("u_Strength", cleanStereo ? stereoLook.AOStrength : g_SSAOStrength);
      ssaoShader->UploadUniformFloat2("u_NoiseScale",
                                      {(float)ssaoFB->GetSpecification().Width / 4.0f,
                                       (float)ssaoFB->GetSpecification().Height / 4.0f});
      glDrawArrays(GL_TRIANGLES, 0, 3);

      ssaoBlurFB->Bind();
      glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ssaoBlurShader->Bind();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, ssaoFB->GetColorAttachmentRendererID());
      ssaoBlurShader->UploadUniformInt("u_SSAO", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, resolveFB->GetDepthAttachmentRendererID());
      ssaoBlurShader->UploadUniformInt("u_Depth", 1);
      ssaoBlurShader->UploadUniformMat4("u_InvProjection", glm::inverse(projMatrix));
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    bloomA->Bind();
    brightShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID());
    brightShader->UploadUniformInt("u_Scene", 0);
    brightShader->UploadUniformFloat("u_Threshold", g_BloomThreshold);
    glBindVertexArray(fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    blurShader->Bind();
    bool horizontal = true;
    Framebuffer* src = bloomA.get();
    Framebuffer* dst = bloomB.get();
    int blurIters = vrFrameValid ? 2 : g_BlurIterations;
    for (int i = 0; i < blurIters * 2; ++i) {
      dst->Bind();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, src->GetColorAttachmentRendererID());
      blurShader->UploadUniformInt("u_Tex", 0);
      glm::vec2 texel = {1.0f / (float)dst->GetSpecification().Width,
                         1.0f / (float)dst->GetSpecification().Height};
      blurShader->UploadUniformFloat2("u_TexelSize", texel);
      blurShader->UploadUniformFloat2("u_Direction",
                                      horizontal ? glm::vec2{1, 0} : glm::vec2{0, 1});
      glDrawArrays(GL_TRIANGLES, 0, 3);
      std::swap(src, dst);
      horizontal = !horizontal;
    }

    Framebuffer* bloomResult = src;

    ldrFB->Bind();
    postShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID());
    postShader->UploadUniformInt("u_Scene", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomResult->GetColorAttachmentRendererID());
    postShader->UploadUniformInt("u_Bloom", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,
                  useSSAO ? ssaoBlurFB->GetColorAttachmentRendererID() : 0);
    postShader->UploadUniformInt("u_SSAO", 2);
    postShader->UploadUniformInt("u_UseSSAO", useSSAO ? 1 : 0);
    postShader->UploadUniformInt("u_CleanStereo", cleanStereo ? 1 : 0);
    postShader->UploadUniformInt("u_SpatialAA", vrActive && m_VRSystem.GetQuality() == 0 ? 1 : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D,
                  g_SSREnabled ? ssrFB->GetColorAttachmentRendererID(0) : 0);
    postShader->UploadUniformInt("u_SSR", 3);
    postShader->UploadUniformInt("u_UseSSR", (g_SSREnabled && !vrFrameValid) ? 1 : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, resolveFB->GetDepthAttachmentRendererID());
    postShader->UploadUniformInt("u_Depth", 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID(1));
    postShader->UploadUniformInt("u_GBuffer", 5);
    postShader->UploadUniformInt("u_DebugView", g_DebugView);
    postShader->UploadUniformFloat("u_BloomStrength", cleanStereo ? stereoLook.BloomStrength : g_BloomStrength);

    if (g_AutoExposure && vrEye == 0) {

      glBindTexture(GL_TEXTURE_2D, resolveFB->GetColorAttachmentRendererID());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glGenerateMipmap(GL_TEXTURE_2D);
      float avgPixel[4] = {0, 0, 0, 0};

      int maxLevel = 0;
      {
        int w = (int)resolveFB->GetSpecification().Width;
        int h = (int)resolveFB->GetSpecification().Height;
        while ((w >> maxLevel) > 1 || (h >> maxLevel) > 1) maxLevel++;
      }
      glGetTexImage(GL_TEXTURE_2D, maxLevel, GL_RGBA, GL_FLOAT, avgPixel);
      float lum = 0.2126f * avgPixel[0] + 0.7152f * avgPixel[1] + 0.0722f * avgPixel[2];
      lum = glm::clamp(lum, 0.01f, 5.0f);
      g_CurrentAvgLum = glm::mix(g_CurrentAvgLum, lum, glm::clamp(g_AutoExposureSpeed * 0.016f, 0.0f, 1.0f));
      float targetExposure = 0.18f / glm::max(g_CurrentAvgLum, 0.001f);
      targetExposure = glm::clamp(targetExposure, g_AutoExposureMin, g_AutoExposureMax);
      g_Exposure = glm::mix(g_Exposure, targetExposure, 0.05f);
    }
    postShader->UploadUniformFloat("u_Exposure", cleanStereo ? stereoLook.Exposure : g_Exposure);
    postShader->UploadUniformFloat("u_Vignette", vrFrameValid ? 0.0f : g_Vignette);

    postShader->UploadUniformFloat("u_ChromaticAberration", vrFrameValid ? 0.0f : g_ChromaticAberration);
    postShader->UploadUniformFloat("u_FilmGrain", vrFrameValid ? 0.0f : g_FilmGrain);
    postShader->UploadUniformFloat("u_PixelHeight", vrFrameValid ? 0.0f : RenderSettings::Get().PixelHeight);
    postShader->UploadUniformFloat("u_ColorLevels", vrFrameValid ? 0.0f : RenderSettings::Get().ColorLevels);
    postShader->UploadUniformFloat("u_DitherStrength", vrFrameValid ? 0.0f : RenderSettings::Get().Dither);
    postShader->UploadUniformFloat("u_OutlineStrength", cleanStereo ? stereoLook.Outline : RenderSettings::Get().Outline);
    postShader->UploadUniformInt("u_FogAffectsSky", RenderSettings::Get().FogAffectsSky ? 1 : 0);
    static float s_PostTime = 0.0f;
    s_PostTime += 0.016f;
    postShader->UploadUniformFloat("u_Time", s_PostTime);
    postShader->UploadUniformFloat("u_FogDensity", g_FogDensity);
    postShader->UploadUniformFloat3("u_FogColor", g_FogColor);
    postShader->UploadUniformFloat("u_FogStart", g_FogStart);
    postShader->UploadUniformInt("u_GodRaysEnabled", g_GodRaysEnabled ? 1 : 0);
    postShader->UploadUniformFloat("u_GodRaysStrength", g_GodRaysStrength);
    postShader->UploadUniformFloat("u_LensFlareStrength", g_LensFlareStrength);
    postShader->UploadUniformFloat("u_DOFFocusDepth", g_DOFFocusDepth);
    postShader->UploadUniformFloat("u_DOFRange", g_DOFRange);
    postShader->UploadUniformFloat("u_DOFStrength", vrFrameValid ? 0.0f : g_DOFStrength);
    postShader->UploadUniformFloat("u_MotionBlur", vrFrameValid ? 0.0f : g_MotionBlur);
    postShader->UploadUniformFloat("u_TAAStrength", vrFrameValid ? 0.0f : g_TAAStrength);
    postShader->UploadUniformFloat("u_Contrast", cleanStereo ? stereoLook.Contrast : g_Contrast);
    postShader->UploadUniformFloat("u_Saturation", cleanStereo ? stereoLook.Saturation : g_Saturation);
    postShader->UploadUniformFloat("u_Temperature", cleanStereo ? stereoLook.Temperature : g_Temperature);
    postShader->UploadUniformFloat("u_Tint", g_Tint);
    postShader->UploadUniformFloat3("u_Lift",  g_Lift);
    postShader->UploadUniformFloat3("u_Gamma", g_Gamma);
    postShader->UploadUniformFloat3("u_Gain",  g_Gain);
    postShader->UploadUniformFloat("u_Near", vrFrameValid ? 0.05f : 0.1f);
    postShader->UploadUniformFloat("u_Far", 1000.0f);
    postShader->UploadUniformMat4("u_InvViewProjection", glm::inverse(viewProjection));
    postShader->UploadUniformMat4("u_PrevViewProjection", m_PrevViewProjection);

    glm::vec2 sunUV(-1.0f);
    {
      glm::vec3 sunDir{0, -1, 0};
      for (const auto &e : m_Scene->GetEntities()) {
        if (e.HasLight && e.Light.Type == LightComponent::LightType::Directional) {
          glm::quat q(e.Transform.Rotation);
          sunDir = glm::normalize(q * glm::vec3(0, 0, -1));
          break;
        }
      }

      glm::vec3 sunPos = -sunDir * 500.0f;
      glm::vec4 clip = projMatrix * viewMatrix * glm::vec4(sunPos, 1.0f);
      if (clip.w > 0.0f) {
        sunUV = glm::vec2((clip.x / clip.w) * 0.5f + 0.5f,
                          (clip.y / clip.w) * 0.5f + 0.5f);
      }
    }
    postShader->UploadUniformFloat2("u_SunScreenPos", sunUV);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, prevFrameFB->GetColorAttachmentRendererID());
    postShader->UploadUniformInt("u_PrevFrame", 6);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    ldrFB->Unbind();

    if (!vrFrameValid) {
      ldrFB->BlitColorTo(*prevFrameFB);
      m_PrevViewProjection = viewProjection;
    }

    if (vrFrameValid) {
      resolveFB->BlitDepthTo(*ldrFB);
      ldrFB->Bind();
      glDisable(GL_FRAMEBUFFER_SRGB);
      m_VRSystem.RenderWorldUI(viewProjection);
      ldrFB->Unbind();
    }
    if (vrFrameValid && m_VRSystem.BindEyeFramebuffer(vrEye)) {
      glDisable(GL_FRAMEBUFFER_SRGB); // The post pass has already encoded display colors.
      ldrFB->BlitColorToBoundFramebuffer(m_VRSystem.GetEyeWidth(), m_VRSystem.GetEyeHeight());
      m_VRSystem.UnbindEyeFramebuffer(vrEye);
    }

#ifdef GAME_MODE_RUNTIME
    if (!vrFrameValid) {
      auto [dw, dh] = m_Window->GetDrawableSize();
      ldrFB->BlitColorToScreen((uint32_t)dw, (uint32_t)dh);
      glViewport(0, 0, dw, dh);
      m_Scene->OnUIRender((float)dw, (float)dh, {0.0f, 0.0f});
    }
#endif

    }

#ifndef GAME_MODE_RUNTIME
    m_SceneHierarchyPanel.OnImGuiRender();
    m_ContentBrowserPanel.OnImGuiRender();

    ImGui::Begin("Rendering");

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.85f, 1));
    ImGui::Text("%.2f ms/frame  |  %.0f FPS", 1000.0f / io.Framerate, io.Framerate);
    ImGui::PopStyleColor();
    static bool s_Vsync = true;
    if (ImGui::Checkbox("V-Sync", &s_Vsync))
      SDL_GL_SetSwapInterval(s_Vsync ? 1 : 0);
    ImGui::Spacing();

    if (ImGui::Button("Restart Physics") && m_AppState == AppState::Runtime) {
      m_Scene->OnPhysicsStart();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Selected"))
      DuplicateSelectedEntities();

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Post-Process", ImGuiTreeNodeFlags_DefaultOpen)) {
      const char *debugViews[] = {"Final", "Depth", "GBuffer Normal",
                                  "GBuffer Reflectivity", "SSR", "SSAO"};
      ImGui::Combo("Debug View", &g_DebugView, debugViews,
                   IM_ARRAYSIZE(debugViews));
      ImGui::SliderFloat("Exposure", &g_Exposure, 0.1f, 4.0f);
      ImGui::SliderFloat("Vignette", &g_Vignette, 0.0f, 2.0f);
    }

    if (ImGui::CollapsingHeader("Bloom")) {
      ImGui::SliderFloat("Strength##Bloom",   &g_BloomStrength, 0.0f, 1.0f);
      ImGui::SliderFloat("Threshold##Bloom",  &g_BloomThreshold, 0.0f, 3.0f);
      ImGui::SliderInt("Iterations##Bloom",   &g_BlurIterations, 1, 10);
    }

    if (ImGui::CollapsingHeader("Ambient Occlusion (SSAO)")) {
      ImGui::Checkbox("Enabled##SSAO",  &g_SSAOEnabled);
      ImGui::SliderFloat("Radius##SSAO",   &g_SSAORadius, 0.1f, 2.0f);
      ImGui::SliderFloat("Bias##SSAO",     &g_SSAOBias, 0.0f, 0.1f, "%.4f");
      ImGui::SliderFloat("Strength##SSAO", &g_SSAOStrength, 0.0f, 4.0f);
    }

    if (ImGui::CollapsingHeader("Reflections (SSR)")) {
      ImGui::Checkbox("Enabled##SSR",  &g_SSREnabled);
      ImGui::SliderFloat("Strength##SSR", &g_SSRStrength, 0.0f, 2.0f);
      ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.75f, 1),
                         "Planar reflection: per-entity in Inspector.");
    }

    if (ImGui::CollapsingHeader("Sky")) {
      ImGui::Checkbox("Procedural Sky", &g_UseProceduralSky);
      ImGui::ColorEdit3("Sky Zenith",   &g_SkyTop.x);
      ImGui::ColorEdit3("Sky Horizon",  &g_SkyHorizon.x);
      ImGui::ColorEdit3("Ground Color", &g_GroundCol.x);
      ImGui::SliderFloat("Sun Disc Size",      &g_SunDiscSize, 0.0001f, 0.01f, "%.4f");
      ImGui::SliderFloat("Sun Disc Intensity", &g_SunDiscIntensity, 0.0f, 5.0f);
    }
    if (ImGui::CollapsingHeader("Auto Exposure")) {
      ImGui::Checkbox("Enabled##AutoExp", &g_AutoExposure);
      ImGui::SliderFloat("Speed##AutoExp", &g_AutoExposureSpeed, 0.1f, 5.0f);
      ImGui::SliderFloat("Min##AutoExp",   &g_AutoExposureMin, 0.05f, 1.0f);
      ImGui::SliderFloat("Max##AutoExp",   &g_AutoExposureMax, 0.5f, 5.0f);
      ImGui::Text("Current avg lum: %.3f", g_CurrentAvgLum);
    }
    if (ImGui::CollapsingHeader("Color Grading")) {
      ImGui::SliderFloat("Contrast",    &g_Contrast, 0.5f, 2.0f);
      ImGui::SliderFloat("Saturation",  &g_Saturation, 0.0f, 2.0f);
      ImGui::SliderFloat("Temperature", &g_Temperature, -1.0f, 1.0f);
      ImGui::SliderFloat("Tint",        &g_Tint, -1.0f, 1.0f);
      ImGui::ColorEdit3("Lift  (shadows)", &g_Lift.x);
      ImGui::ColorEdit3("Gamma (mids)",    &g_Gamma.x);
      ImGui::ColorEdit3("Gain  (highs)",   &g_Gain.x);
    }
    if (ImGui::CollapsingHeader("Film Look")) {
      ImGui::SliderFloat("Chromatic Aberration", &g_ChromaticAberration, 0.0f, 2.0f);
      ImGui::SliderFloat("Film Grain",           &g_FilmGrain, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Volumetric Fog")) {
      ImGui::SliderFloat("Density##Fog", &g_FogDensity, 0.0f, 0.2f, "%.4f");
      ImGui::SliderFloat("Start##Fog",   &g_FogStart, 0.0f, 100.0f);
      ImGui::ColorEdit3("Color##Fog",    &g_FogColor.x);
    }
    if (ImGui::CollapsingHeader("God Rays & Lens Flare")) {
      ImGui::Checkbox("God Rays",            &g_GodRaysEnabled);
      ImGui::SliderFloat("God Rays Strength", &g_GodRaysStrength, 0.0f, 2.0f);
      ImGui::SliderFloat("Lens Flare",        &g_LensFlareStrength, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Depth of Field")) {
      ImGui::SliderFloat("DOF Strength",    &g_DOFStrength, 0.0f, 1.0f);
      ImGui::SliderFloat("Focus Depth",     &g_DOFFocusDepth, 0.0f, 1.0f);
      ImGui::SliderFloat("Sharp Range",     &g_DOFRange, 0.0f, 0.5f);
    }
    if (ImGui::CollapsingHeader("Temporal (Motion Blur / TAA)")) {
      ImGui::SliderFloat("Motion Blur",  &g_MotionBlur, 0.0f, 0.5f);
      ImGui::SliderFloat("TAA Strength", &g_TAAStrength, 0.0f, 0.5f);
      ImGui::TextColored(ImVec4(0.7f, 0.65f, 0.45f, 1),
                         "Approximation — true MB/TAA need motion vectors.\n"
                         "Pixels with big color change are rejected to kill ghosting.");
    }

    if (ImGui::CollapsingHeader("Retro Look")) {
      RenderSettings &rs = RenderSettings::Get();
      ImGui::SliderFloat("Pixel Height", &rs.PixelHeight, 0.0f, 720.0f, "%.0f");
      ImGui::SliderFloat("Color Levels", &rs.ColorLevels, 0.0f, 32.0f, "%.0f");
      ImGui::SliderFloat("Dither", &rs.Dither, 0.0f, 1.0f);
      ImGui::SliderFloat("Outline", &rs.Outline, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Assets")) {
      static AssetRegistry s_AssetRegistry;
      static std::filesystem::path s_LastAssetRoot;
      std::filesystem::path assetRoot =
          m_Project.ProjectRoot.empty() ? std::filesystem::path("assets")
                                        : m_Project.ProjectRoot / "Assets";
      if (s_LastAssetRoot != assetRoot) {
        s_AssetRegistry.Scan(assetRoot);
        s_LastAssetRoot = assetRoot;
      }
      ImGui::Text("Root: %s", assetRoot.string().c_str());
      if (ImGui::Button("Refresh Assets"))
        s_AssetRegistry.Scan(assetRoot);
      ImGui::Text("Total: %d", (int)s_AssetRegistry.GetAssets().size());
      ImGui::Text("Scenes: %d", (int)s_AssetRegistry.CountByType(AssetType::Scene));
      ImGui::Text("Models: %d", (int)s_AssetRegistry.CountByType(AssetType::Model));
      ImGui::Text("Textures: %d", (int)s_AssetRegistry.CountByType(AssetType::Texture));
      ImGui::Text("Scripts: %d", (int)s_AssetRegistry.CountByType(AssetType::Script));
      ImGui::Text("Audio: %d", (int)s_AssetRegistry.CountByType(AssetType::Audio));
    }

    if (ImGui::CollapsingHeader("VR (OpenXR)")) {
      bool vrOn = m_VRSystem.IsActive();
      ImGui::TextColored(vrOn ? ImVec4(0.3f, 0.9f, 0.3f, 1) : ImVec4(0.7f, 0.5f, 0.5f, 1),
                         vrOn ? "Headset ACTIVE" : "No headset detected");
      if (vrOn) {
        ImGui::Text("Resolution: %u x %u (per eye)", m_VRSystem.GetEyeWidth(), m_VRSystem.GetEyeHeight());
        glm::vec3 hmdPos = m_VRSystem.GetHMDPosition();
        ImGui::Text("HMD: %.2f  %.2f  %.2f", hmdPos.x, hmdPos.y, hmdPos.z);
        for (int h = 0; h < 2; h++) {
          const auto &hs = m_VRSystem.GetHandState(h);
          ImGui::Text("%s: %s  trig=%.2f  grip=%.2f  stick=(%.2f,%.2f)",
                      h == 0 ? "L" : "R",
                      hs.Active ? "active" : "off   ",
                      hs.Trigger, hs.Squeeze,
                      hs.Thumbstick.x, hs.Thumbstick.y);
        }
      }

      if (m_Scene) {
        Entity *rigEntity = nullptr;
        for (auto &e : m_Scene->GetEntities())
          if (e.HasVRRig) { rigEntity = &e; break; }

        if (rigEntity) {
          ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1), "Rig entity: %s", rigEntity->Name.c_str());
          ImGui::Checkbox("Preview Mode (no headset)", &rigEntity->VRRig.PreviewMode);
          if (ImGui::IsItemEdited()) m_Scene->UpdateEntity(*rigEntity);
        } else {
          ImGui::TextDisabled("No entity with VR Rig component found.");
          ImGui::TextDisabled("Add 'VR Rig' via Inspector > Add Component.");
        }
      }
    }

    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
        (viewportSize.x != fbSpec.Width || viewportSize.y != fbSpec.Height)) {
      resizeRenderTargets((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);

      m_Camera.SetViewportSize(fbSpec.Width, fbSpec.Height);
    }

    uint32_t textureID = ldrFB->GetColorAttachmentRendererID();
    ImVec2 vPos = ImGui::GetCursorScreenPos();
    m_ViewportOffset = {vPos.x, vPos.y};
    m_GameViewRect = {vPos.x, vPos.y, (float)fbSpec.Width, (float)fbSpec.Height};

    ImGui::Image((void *)(intptr_t)textureID,
                 ImVec2((float)fbSpec.Width, (float)fbSpec.Height),
                 ImVec2(0, 1), ImVec2(1, 0));
    bool viewportHovered = ImGui::IsItemHovered();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        viewportHovered && !ImGuizmo::IsOver()) {
      m_MarqueeActive = true;
      m_MarqueeStart = {ImGui::GetMousePos().x, ImGui::GetMousePos().y};
    }

    if (m_MarqueeActive) {
      ImVec2 mousePos = ImGui::GetMousePos();
      ImGui::GetWindowDrawList()->AddRect(
          ImVec2(m_MarqueeStart.x, m_MarqueeStart.y), mousePos,
          IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_MarqueeActive = false;
        glm::vec2 marqueeEnd = {mousePos.x, mousePos.y};
        float dist = glm::distance(m_MarqueeStart, marqueeEnd);

        if (dist < 5.0f) {
          glm::vec2 relativePos = {
              m_MarqueeStart.x - m_ViewportOffset.x,
              m_MarqueeStart.y - m_ViewportOffset.y};

          if (relativePos.x < 0.0f || relativePos.y < 0.0f ||
              relativePos.x > (float)fbSpec.Width ||
              relativePos.y > (float)fbSpec.Height) {
            if (!ImGui::GetIO().KeyCtrl)
              m_SceneHierarchyPanel.ClearSelection();
          } else {
            float x = (2.0f * relativePos.x) / (float)fbSpec.Width - 1.0f;
            float y = 1.0f - (2.0f * relativePos.y) / (float)fbSpec.Height;

            glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);
            glm::vec4 ray_eye =
                glm::inverse(m_Camera.GetProjection()) * ray_clip;
            ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
            glm::vec3 ray_world =
                glm::vec3(glm::inverse(m_Camera.GetViewMatrix()) * ray_eye);
            ray_world = glm::normalize(ray_world);

            Ray ray = {m_Camera.GetPosition(), ray_world};
            int hitID = -1;
            float minT = std::numeric_limits<float>::max();

            for (auto &entity : m_Scene->GetEntities()) {
              float t;
              if (RayIntersectsEntity(*m_Scene, ray, entity, t)) {
                if (t < minT) {
                  minT = t;
                  hitID = entity.GetID();
                }
              }
            }

            if (hitID != -1) {
              bool controlDown = ImGui::GetIO().KeyCtrl;
              if (controlDown) {
                if (m_SceneHierarchyPanel.IsSelected(hitID))
                  m_SceneHierarchyPanel.RemoveSelectedEntity(hitID);
                else
                  m_SceneHierarchyPanel.AddSelectedEntity(hitID);
              } else {
                for (auto &entity : m_Scene->GetEntities()) {
                  if (entity.GetID() == hitID) {
                    m_SceneHierarchyPanel.SetSelectedEntity(entity);
                    break;
                  }
                }
              }
            } else {
              if (!ImGui::GetIO().KeyCtrl)
                m_SceneHierarchyPanel.ClearSelection();
            }
          }
        } else {
          if (!ImGui::GetIO().KeyCtrl)
            m_SceneHierarchyPanel.ClearSelection();

          glm::vec2 min = {std::min(m_MarqueeStart.x, marqueeEnd.x),
                           std::min(m_MarqueeStart.y, marqueeEnd.y)};
          glm::vec2 max = {std::max(m_MarqueeStart.x, marqueeEnd.x),
                           std::max(m_MarqueeStart.y, marqueeEnd.y)};

          glm::mat4 vp = m_Camera.GetViewProjection();

          for (auto &entity : m_Scene->GetEntities()) {
            if (!entity.HasSpriteRenderer)
              continue;

            glm::vec4 worldPos =
                glm::vec4(glm::vec3(m_Scene->GetWorldTransform(entity)[3]),
                          1.0f);
            glm::vec4 clipPos = vp * worldPos;

            if (clipPos.w > 0.0f) {
              glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
              glm::vec2 screenPos;
              screenPos.x =
                  (ndc.x + 1.0f) * 0.5f * fbSpec.Width + m_ViewportOffset.x;
              screenPos.y =
                  (1.0f - ndc.y) * 0.5f * fbSpec.Height + m_ViewportOffset.y;

              if (screenPos.x >= min.x && screenPos.x <= max.x &&
                  screenPos.y >= min.y && screenPos.y <= max.y) {
                m_SceneHierarchyPanel.AddSelectedEntity(entity.GetID());
              }
            }
          }
        }
      }
    }

    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    if (m_AppState == AppState::Editor && selectedEntity.GetID() != -1) {
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();

      float windowWidth = (float)ImGui::GetWindowWidth();
      float windowHeight = (float)ImGui::GetWindowHeight();
      ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                        windowWidth, windowHeight);

      const glm::mat4 &cameraProjection = m_Camera.GetProjection();
      glm::mat4 cameraView = m_Camera.GetViewMatrix();

      glm::mat4 transform = m_Scene->GetWorldTransform(selectedEntity);

      bool snap = Input::IsKeyPressed(SDL_SCANCODE_LCTRL);
      float snapValue = 0.5f;
      if (ImGuizmo::OPERATION::ROTATE)
        snapValue = 45.0f;

      float snapValues[3] = {snapValue, snapValue, snapValue};

      static ImGuizmo::OPERATION m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;

      if (Input::IsKeyPressed(SDL_SCANCODE_W))
        m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
      if (Input::IsKeyPressed(SDL_SCANCODE_E))
        m_GizmoType = ImGuizmo::OPERATION::ROTATE;
      if (Input::IsKeyPressed(SDL_SCANCODE_R))
        m_GizmoType = ImGuizmo::OPERATION::SCALE;

      if (selectedEntity.IsUIElement()) {
        if (selectedEntity.HasRectTransform) {
          float width = (float)fbSpec.Width;
          float height = (float)fbSpec.Height;

          glm::vec2 parentPos = {0, 0};
          glm::vec2 parentSize = {width, height};

          auto &rt = selectedEntity.RectTransform;
          glm::vec2 parentMin = parentPos;
          glm::vec2 anchorMinPoint = parentMin + rt.AnchorsMin * parentSize;
          glm::vec2 anchorMaxPoint = parentMin + rt.AnchorsMax * parentSize;
          glm::vec2 anchorCenter = (anchorMinPoint + anchorMaxPoint) * 0.5f;
          glm::vec2 baseSize = anchorMaxPoint - anchorMinPoint;
          glm::vec2 size = baseSize + rt.SizeDelta;
          glm::vec2 pivotOffset = (rt.Pivot - glm::vec2(0.5f)) * size;
          glm::vec2 posWithoutPivot = anchorCenter + rt.Position;
          glm::vec2 finalPos = posWithoutPivot - size * 0.5f - pivotOffset;

          ImVec2 vMin = ImVec2(m_ViewportOffset.x + finalPos.x,
                               m_ViewportOffset.y + finalPos.y);
          ImVec2 vMax = ImVec2(vMin.x + size.x, vMin.y + size.y);

          ImGui::GetWindowDrawList()->AddRect(
              vMin, vMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);

          float handleSize = 8.0f;
          float halfHandle = handleSize * 0.5f;

          ImRect handles[4];
          handles[0] = ImRect(vMin.x - halfHandle, vMax.y - halfHandle,
                              vMin.x + halfHandle, vMax.y + halfHandle);
          ImGui::SetCursorScreenPos(vMin);
          ImGui::InvisibleButton("##move_ui", vMax - vMin);
          if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            rt.Position.x += delta.x;
            rt.Position.y -= delta.y;
          }

          ImGui::SetCursorScreenPos(
              ImVec2(vMax.x - halfHandle, vMax.y - halfHandle));
          ImGui::Button("##resize_br", ImVec2(handleSize, handleSize));
          if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            rt.SizeDelta.x += delta.x;
            rt.SizeDelta.y += delta.y;
          }

          ImGui::GetWindowDrawList()->AddRect(
              vMin, vMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
        }
      } else {
        ImGuizmo::Manipulate(glm::value_ptr(cameraView),
                             glm::value_ptr(cameraProjection), m_GizmoType,
                             ImGuizmo::LOCAL, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);
      }

      static bool s_GizmoWasUsing = false;
      if (ImGuizmo::IsUsing()) {
        if (!s_GizmoWasUsing) {
          SaveHistoryState();
          m_InitialSelectionTransforms.clear();
          for (int id : m_SceneHierarchyPanel.GetSelectedEntities()) {
            for (auto &entity : m_Scene->GetEntities()) {
              if (entity.GetID() == id) {
                m_InitialSelectionTransforms[id] =
                    m_Scene->GetWorldTransform(entity);
                break;
              }
            }
          }
        }
        s_GizmoWasUsing = true;

        glm::mat4 initialPrimary =
            m_InitialSelectionTransforms[selectedEntity.GetID()];
        glm::mat4 delta = transform * glm::inverse(initialPrimary);

        for (int id : m_SceneHierarchyPanel.GetSelectedEntities()) {
          if (m_InitialSelectionTransforms.find(id) ==
              m_InitialSelectionTransforms.end())
            continue;

          glm::mat4 newTransform = delta * m_InitialSelectionTransforms[id];

          for (auto &e : m_Scene->GetEntities()) {
            if (e.GetID() == id) {
              glm::mat4 localTransform = newTransform;
              if (e.HasRelationship && e.Relationship.Parent != 0) {
                localTransform =
                    glm::inverse(m_Scene->GetWorldTransform(
                        e.Relationship.Parent)) *
                    newTransform;
              }
              m_Scene->SetLocalTransformFromMatrix(e, localTransform);
              break;
            }
          }
        }
      } else {
        s_GizmoWasUsing = false;
      }
    }

    ImGui::End();
    ImGui::PopStyleVar();
#endif
    if (m_AppState == AppState::Runtime)
      m_Scene->OnImGuiRender();

    if (vrActive && m_VRSystem.HasTracking() && m_VRSystem.IsFocused() &&
        (m_VRSystem.IsUIInteractive() || m_VRSystem.IsCinemaMode())) {
      glm::vec2 uv;
      if (m_VRSystem.GetUIPointer(uv)) {
        auto& io = ImGui::GetIO();
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float px = uv.x * io.DisplaySize.x;
        float py = uv.y * io.DisplaySize.y;
        const auto& hand = m_VRSystem.GetHandState(m_VRSystem.GetHandState(1).Active ? 1 : 0);
        bool down = hand.Active && (hand.Trigger > 0.55f || hand.PrimaryBtn);
        fg->AddCircleFilled(ImVec2(px,py),12,IM_COL32(5,16,26,230));
        fg->AddCircleFilled(ImVec2(px,py),down?5.0f:3.5f,IM_COL32(245,250,255,255),16);
        fg->AddCircle(ImVec2(px,py),down?12.0f:9.5f,IM_COL32(255,214,92,255),24,2.0f);
      }
    }

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!xrFrameValid) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (xrFrameValid && m_VRSystem.BindUIFramebuffer()) {
      auto [dw, dh] = m_Window->GetDrawableSize();
      if (m_VRSystem.IsCinemaMode()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, dw, dh, 0, 0, VRSystem::kUIWidth, VRSystem::kUIHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      }
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      m_VRSystem.UnbindUIFramebuffer();
      glViewport(0, 0, dw, dh);
    }
    if (vrActive) m_VRSystem.EndFrame();

    if (!m_ScreenshotPath.empty()) {
      auto [dw, dh] = m_Window->GetDrawableSize();
      std::vector<uint8_t> pixels((size_t)dw * dh * 3);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glReadBuffer(GL_BACK);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      if (!WritePNG(m_ScreenshotPath, pixels, dw, dh))
        std::cerr << "[App] could not write screenshot " << m_ScreenshotPath << "\n";
      m_ScreenshotPath.clear();
    }

    m_Window->OnUpdate();
  }
}

void Application::SaveHistoryState() {
  SceneSerializer serializer(m_Scene);
  m_UndoStack.push_back(serializer.SerializeToString());
  if (m_UndoStack.size() > m_MaxHistorySize) {
    m_UndoStack.pop_front();
  }
  m_RedoStack.clear();
}

void Application::Undo() {
  if (m_UndoStack.empty())
    return;

  std::string currentState = SceneSerializer(m_Scene).SerializeToString();
  m_RedoStack.push_back(currentState);
  if (m_RedoStack.size() > m_MaxHistorySize) {
    m_RedoStack.pop_front();
  }

  std::string lastState = m_UndoStack.back();
  m_UndoStack.pop_back();

  SceneSerializer serializer(m_Scene);
  serializer.DeserializeFromString(lastState);

  m_Scene->OnPhysicsStart();
}

void Application::Redo() {
  if (m_RedoStack.empty())
    return;

  std::string currentState = SceneSerializer(m_Scene).SerializeToString();
  m_UndoStack.push_back(currentState);
  if (m_UndoStack.size() > m_MaxHistorySize) {
    m_UndoStack.pop_front();
  }

  std::string nextState = m_RedoStack.back();
  m_RedoStack.pop_back();

  SceneSerializer serializer(m_Scene);
  serializer.DeserializeFromString(nextState);

  m_Scene->OnPhysicsStart();
}

void Application::DuplicateSelectedEntities() {
  const auto selected = m_SceneHierarchyPanel.GetSelectedEntities();
  if (selected.empty())
    return;

  SaveHistoryState();
  m_SceneHierarchyPanel.ClearSelection();

  for (int id : selected) {
    const Entity *source = nullptr;
    for (const auto &entity : m_Scene->GetEntities()) {
      if (entity.GetID() == id) {
        source = &entity;
        break;
      }
    }
    if (!source)
      continue;

    Entity copy = m_Scene->CreateEntity(source->Name + " Copy");
    copy.Transform = source->Transform;
    copy.Transform.Translation += glm::vec3(0.25f, 0.0f, 0.25f);
    copy.SpriteRenderer = source->SpriteRenderer;
    copy.RigidBody = source->RigidBody;
    copy.BoxCollider = source->BoxCollider;
    copy.Camera = source->Camera;
    copy.Light = source->Light;
    copy.Skybox = source->Skybox;
    copy.MeshRenderer = source->MeshRenderer;
    copy.Canvas = source->Canvas;
    copy.RectTransform = source->RectTransform;
    copy.Image = source->Image;
    copy.Button = source->Button;
    copy.Text = source->Text;
    copy.AudioSource = source->AudioSource;

    copy.HasSkybox = source->HasSkybox;
    copy.HasMeshRenderer = source->HasMeshRenderer;
    copy.HasRigidBody = source->HasRigidBody;
    copy.HasBoxCollider = source->HasBoxCollider;
    copy.HasCamera = source->HasCamera;
    copy.HasSpriteRenderer = source->HasSpriteRenderer;
    copy.HasLight = source->HasLight;
    copy.HasAudioSource = source->HasAudioSource;
    copy.HasCanvas = source->HasCanvas;
    copy.HasRectTransform = source->HasRectTransform;
    copy.HasImage = source->HasImage;
    copy.HasButton = source->HasButton;
    copy.HasText = source->HasText;
    copy.HasRelationship = false;
    copy.Relationship = RelationshipComponent();

    m_Scene->UpdateEntity(copy);
    m_SceneHierarchyPanel.AddSelectedEntity(copy.GetID());
  }
}

void Application::LoadRecentScenes() {
  m_RecentScenes.clear();
  std::ifstream in("imgui.recent");
  if (in.is_open()) {
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty())
        continue;
      try {
        std::filesystem::path p(line);
        if (std::filesystem::exists(p)) {
          m_RecentScenes.push_back(line);
        }
      } catch (const std::exception &e) {
        std::cerr << "Error checking recent scene path: " << line << " ("
                  << e.what() << ")" << std::endl;
      } catch (...) {
        std::cerr << "Unknown error checking recent scene path: " << line
                  << std::endl;
      }
    }
    in.close();
  }
}

void Application::SaveRecentScenes() {
  std::ofstream out("imgui.recent");
  if (out.is_open()) {
    for (const auto &path : m_RecentScenes) {
      out << path << std::endl;
    }
    out.close();
  }
}

void Application::AddToRecentScenes(const std::string &path) {
  auto it = std::find(m_RecentScenes.begin(), m_RecentScenes.end(), path);
  if (it != m_RecentScenes.end()) {
    m_RecentScenes.erase(it);
  }
  m_RecentScenes.insert(m_RecentScenes.begin(), path);
  if (m_RecentScenes.size() > 10) {
    m_RecentScenes.resize(10);
  }
  SaveRecentScenes();
}

void Application::RenderProjectHub() {
  auto &io = ImGui::GetIO();
  const ImVec2 screen = io.DisplaySize;

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##ProjectHub", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::SetWindowPos(ImVec2(0, 0));
  ImGui::SetWindowSize(screen);

  const float sidebarW = screen.x * 0.32f;
  auto *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(sidebarW, screen.y),
                    IM_COL32(20, 24, 34, 255));
  dl->AddRectFilled(ImVec2(sidebarW - 3, 0), ImVec2(sidebarW, screen.y),
                    IM_COL32(80, 130, 240, 255));

  float scaleBak = io.FontGlobalScale;
  ImGui::SetCursorPos(ImVec2(48, 80));
  io.FontGlobalScale = 2.6f;
  ImGui::TextColored(ImVec4(1, 1, 1, 1), "MyEngine");
  io.FontGlobalScale = 1.2f;
  ImGui::SetCursorPosX(48);
  ImGui::TextColored(ImVec4(0.55f, 0.6f, 0.7f, 1), "v0.1");
  io.FontGlobalScale = scaleBak;

  ImGui::SetCursorPos(ImVec2(48, screen.y - 60));
  ImGui::TextColored(ImVec4(0.45f, 0.5f, 0.6f, 1),
                     "%d recent project(s)", (int)m_RecentScenes.size());

  const float contentX = sidebarW + 48;
  const float contentW = screen.x - contentX - 48;

  ImGui::SetCursorPos(ImVec2(contentX, 80));
  io.FontGlobalScale = 1.8f;
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  ImGui::TextUnformatted("Projects");
  ImGui::PopStyleColor();
  io.FontGlobalScale = scaleBak;

  ImGui::SetCursorPos(ImVec2(contentX, 130));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.45f, 0.85f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 1.00f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.70f, 1));
  if (ImGui::Button("+ New Project", ImVec2(180, 44))) {
    std::string file = FileDialogs::SaveFile(".myproject\0*.myproject\0");
    if (!file.empty()) {
      std::filesystem::path p = file;
      if (p.extension() != ".myproject") p += ".myproject";
      std::string stem = p.stem().string();
      if (!NewProject(p.parent_path(), stem))
        std::cerr << "[Hub] New failed: " << m_LastProjectError << "\n";
    }
  }
  ImGui::PopStyleColor(3);
  ImGui::SameLine(0, 12);
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.20f, 0.24f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.27f, 0.32f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.16f, 0.20f, 1));
  if (ImGui::Button("Open Existing...", ImVec2(180, 44))) {
    std::string file = FileDialogs::OpenFile(".myproject\0*.myproject\0");
    if (!file.empty()) {
      if (!OpenProject(file))
        std::cerr << "[Hub] Open failed: " << m_LastProjectError << "\n";
    }
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);

  if (!m_LastProjectError.empty()) {
    ImGui::SetCursorPos(ImVec2(contentX, 190));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1));
    ImGui::Text("Error: %s", m_LastProjectError.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::SetCursorPos(ImVec2(contentX, 220));
  ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "RECENT");

  ImGui::SetCursorPos(ImVec2(contentX, 245));
  ImGui::BeginChild("##RecentList", ImVec2(contentW, screen.y - 280), false,
                    ImGuiWindowFlags_NoBackground);
  if (m_RecentScenes.empty()) {
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
                       "No recent projects yet. Create or open one above.");
  }
  int idx = 0;
  std::string toOpen;
  int removeIdx = -1;
  for (const auto &r : m_RecentScenes) {
    ImGui::PushID(idx);
    std::filesystem::path rp(r);
    std::string name = rp.stem().string();
    std::string dir  = rp.parent_path().string();

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float cardH = 64.0f;
    const float cardW = contentW - 20;
    bool hovered = ImGui::IsMouseHoveringRect(
        cursor, ImVec2(cursor.x + cardW, cursor.y + cardH));
    ImU32 bg = hovered ? IM_COL32(36, 42, 58, 255) : IM_COL32(22, 26, 38, 255);
    auto *cdl = ImGui::GetWindowDrawList();
    cdl->AddRectFilled(cursor, ImVec2(cursor.x + cardW, cursor.y + cardH),
                       bg, 6.0f);
    cdl->AddRectFilled(cursor, ImVec2(cursor.x + 4, cursor.y + cardH),
                       hovered ? IM_COL32(120, 180, 255, 255)
                               : IM_COL32(60, 90, 160, 255));

    ImGui::SetCursorScreenPos(ImVec2(cursor.x + 18, cursor.y + 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted(name.empty() ? rp.string().c_str() : name.c_str());
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(cursor.x + 18, cursor.y + 32));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.60f, 0.70f, 1));
    ImGui::TextUnformatted(dir.c_str());
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(cursor);
    if (ImGui::InvisibleButton("##card", ImVec2(cardW, cardH)))
      toOpen = r;
    if (ImGui::BeginPopupContextItem("##ctx")) {
      if (ImGui::MenuItem("Remove from list")) removeIdx = idx;
      ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PopID();
    ++idx;
  }
  ImGui::EndChild();

  if (!toOpen.empty()) {
    if (!OpenProject(toOpen))
      std::cerr << "[Hub] Recent open failed: " << m_LastProjectError << "\n";
  }
  if (removeIdx >= 0 && removeIdx < (int)m_RecentScenes.size()) {
    m_RecentScenes.erase(m_RecentScenes.begin() + removeIdx);
    SaveRecentScenes();
  }

  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

bool Application::OpenProject(const std::filesystem::path& file) {
  m_LastProjectError.clear();
  auto r = ProjectLoader::LoadFromFile(file, m_Project);
  if (!r.Ok) { m_LastProjectError = r.Error; return false; }

  if (ScriptBuilder::IsStale(m_Project)) {
    auto br = ScriptBuilder::Build(m_Project);
    if (!br.Ok) {
      std::cerr << "[OpenProject] initial build failed:\n" << br.Log << "\n";
      ReportScriptBuildFailure(br.Log);
    }
  }

  {
    auto engineDll = std::filesystem::path(BuildInfo::EngineLibDll);
    if (std::filesystem::exists(engineDll)) {
      auto destDll = m_Project.ProjectRoot / engineDll.filename();
      bool needCopy = !std::filesystem::exists(destDll) ||
          std::filesystem::last_write_time(engineDll) > std::filesystem::last_write_time(destDll);
      if (needCopy) {
        std::error_code ec;
        std::filesystem::copy_file(engineDll, destDll,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) std::cerr << "[OpenProject] failed to copy EngineLib.dll: " << ec.message() << "\n";
      }
    }
  }

  auto modPath = m_Project.ProjectRoot / m_Project.GameModulePath;
  if (std::filesystem::exists(modPath)) {
    auto mr = m_GameModule.Load(modPath);
    if (!mr.Ok) std::cerr << "[OpenProject] DLL load warning: " << mr.Error << "\n";
  }

  SceneManager::SetProject(&m_Project);

  {
    std::error_code ec;
    std::filesystem::current_path(m_Project.ProjectRoot, ec);
    if (ec) std::cerr << "[OpenProject] chdir failed: " << ec.message() << "\n";
  }

  const SceneEntry* entry = m_Project.FindScene(m_Project.StartupSceneName);
  if (!entry) { m_LastProjectError = "startup scene not found in project"; return false; }
  SceneSerializer ser(m_Scene);
  auto full = (m_Project.ProjectRoot / entry->RelativePath).string();
  if (!ser.Deserialize(full)) { m_LastProjectError = "failed to load startup scene"; return false; }
  m_CurrentScenePath = full;
  MarkSceneSynced();

  m_ProjectFilePath = file;
  m_AppState = AppState::Editor;
  SDL_SetWindowTitle(m_Window->GetNativeWindow(), (m_Project.Name + " — Engine").c_str());
  AddToRecentScenes(file.string());
  return true;
}

void Application::CloseProject() {
  m_GameHandlesEscape = false;
  m_Scene->Clear();
  SceneManager::SetProject(nullptr);
  m_GameModule.Unload();
  m_Project = {};
  m_ProjectFilePath.clear();
  m_AppState = AppState::ProjectHub;
  SDL_SetWindowTitle(m_Window->GetNativeWindow(), "Engine");
}

bool Application::NewProject(const std::filesystem::path& dir, const std::string& name) {
  m_LastProjectError.clear();
  namespace fs = std::filesystem;
  try {
    fs::create_directories(dir / "Assets/Scenes");
    fs::create_directories(dir / "Assets/Scripts");
    fs::create_directories(dir / "Assets/Textures");
    fs::create_directories(dir / "Assets/Models");
    fs::create_directories(dir / "Assets/Audio");
    fs::create_directories(dir / "Build");
    fs::create_directories(dir / ".cache/build");
  } catch (const std::exception& e) {
    m_LastProjectError = e.what(); return false;
  }

  auto scenePath = dir / "Assets/Scenes/MainMenu.scene";
  std::ofstream(scenePath) << "Scene: MainMenu\nEntities: []\n";

  Project p;
  p.Name = name;
  p.StartupSceneName = "MainMenu";
  p.Scenes = {{"MainMenu", "Assets/Scenes/MainMenu.scene"}};
  p.GameModulePath = "Game.dll";
  p.ProjectRoot = dir;

  auto projFile = dir / (name + ".myproject");
  auto r = ProjectSerializer::Serialize(p, projFile);
  if (!r.Ok) { m_LastProjectError = r.Error; return false; }

  return OpenProject(projFile);
}

void Application::TogglePlayMode() {
  if (!GetProject()) return;
  m_GameHandlesEscape = false;
  if (m_AppState == AppState::Runtime) {

    AudioEngine::StopAllVoices();
    m_Scene->OnPhysicsStop();
    m_Scene->Clear();
    SceneSerializer(m_Scene).DeserializeFromString(m_PrePlaySceneSnapshot);
    m_AppState = AppState::Editor;

    // Drop whatever look the game's scripts set while playing.
    RenderSettings &look = RenderSettings::Get();
    look.PixelHeight = look.ColorLevels = look.Dither = look.Outline = 0.0f;
    look.FogAffectsSky = true;
    look.VR.Enabled = false;
    look.RestorePost = true;

    SetMouseLocked(false);
    std::cout << "[Play] stopped\n";
  } else {
    // Something changed the project outside the editor (git pull, a
    // generator): play what is on disk, not what was loaded before. A script
    // build that fails keeps the editor out of Play, with the log on screen.
    ReloadSceneIfChangedOnDisk();
    if (ScriptBuilder::IsStale(m_Project) && !ReloadScripts())
      return;

    m_PrePlaySceneSnapshot = SceneSerializer(m_Scene).SerializeToString();
    m_AppState = AppState::Runtime;

    SetMouseLocked(true);
    m_Scene->OnPhysicsStart();
    std::cout << "[Play] started\n";
  }
}

void Application::SetMouseLocked(bool locked) {
  m_MouseLocked = locked;
  SDL_Window *window = m_Window ? m_Window->GetNativeWindow() : nullptr;

  if (locked && window) {
    SDL_RaiseWindow(window);
    SDL_SetWindowInputFocus(window);
    SDL_SetWindowGrab(window, SDL_TRUE);
    SDL_CaptureMouse(SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_WarpMouseInWindow(window, w / 2, h / 2);
    SDL_ShowCursor(SDL_DISABLE);
  } else {
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_CaptureMouse(SDL_FALSE);
    if (window)
      SDL_SetWindowGrab(window, SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
  }

  int x = 0;
  int y = 0;
  SDL_GetRelativeMouseState(&x, &y);
}

void Application::PersistProject() {
  if (m_ProjectFilePath.empty()) return;
  auto r = ProjectSerializer::Serialize(m_Project, m_ProjectFilePath);
  if (!r.Ok) std::cerr << "[Project] save failed: " << r.Error << "\n";
}

void Application::AddOrRegisterScene(const std::filesystem::path& scenePath) {
  namespace fs = std::filesystem;
  std::string relStr;
  try {
    auto rel = fs::relative(scenePath, m_Project.ProjectRoot);
    relStr = rel.generic_string();
  } catch (...) {
    relStr = scenePath.generic_string();
  }
  std::string name = scenePath.stem().string();
  for (auto& s : m_Project.Scenes) {
    if (s.Name == name) { s.RelativePath = relStr; PersistProject(); return; }
  }
  m_Project.Scenes.push_back({name, relStr});
  PersistProject();
}

void Application::SaveGameSet(const std::string &k, const std::string &v) { m_SaveGameKV[k] = v; }
std::string Application::SaveGameGet(const std::string &k, const std::string &fb) const {
  auto it = m_SaveGameKV.find(k); return it == m_SaveGameKV.end() ? fb : it->second;
}
void Application::SaveGameWrite(const std::filesystem::path &file) {
  YAML::Emitter out; out << YAML::BeginMap;
  out << YAML::Key << "Save" << YAML::Value << YAML::BeginMap;
  for (auto &kv : m_SaveGameKV) out << YAML::Key << kv.first << YAML::Value << kv.second;
  out << YAML::EndMap << YAML::EndMap;
  std::ofstream f(file); if (f) f << out.c_str();
}
void Application::SaveGameRead(const std::filesystem::path &file) {
  m_SaveGameKV.clear();
  if (!std::filesystem::exists(file)) return;
  try {
    YAML::Node n = YAML::LoadFile(file.string());
    auto save = n["Save"];
    if (!save) return;
    for (auto it = save.begin(); it != save.end(); ++it)
      m_SaveGameKV[it->first.as<std::string>()] = it->second.as<std::string>();
  } catch (...) {}
}
std::string Application::SaveCurrentSceneSnapshot() {
  return SceneSerializer(m_Scene).SerializeToString();
}
bool Application::RestoreSceneSnapshot(const std::string &yaml) {
  m_Scene->Clear();
  return SceneSerializer(m_Scene).DeserializeFromString(yaml);
}

bool Application::SavePrefab(int entityID, const std::filesystem::path &outFile) {
  for (const auto &e : m_Scene->GetEntities()) {
    if (e.GetID() == entityID) {

      auto tempScene = std::make_shared<Scene>();

      auto fullYaml = SceneSerializer(m_Scene).SerializeToString();

      YAML::Emitter out;
      out << YAML::BeginMap << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
      out << YAML::Key << "Name" << YAML::Value << e.Name;
      out << YAML::EndMap << YAML::EndMap;

      std::ofstream f(outFile);
      if (!f) return false;

      f << "Scene: Prefab\nEntities:\n";

      f << "  - Entity: " << (uint64_t)e.GetUUID() << "\n";
      f << "    Name: " << e.Name << "\n";
      f << "    TransformComponent:\n";
      f << "      Translation: [" << e.Transform.Translation.x << ", "
        << e.Transform.Translation.y << ", " << e.Transform.Translation.z << "]\n";
      f << "      Rotation: [" << e.Transform.Rotation.x << ", "
        << e.Transform.Rotation.y << ", " << e.Transform.Rotation.z << "]\n";
      f << "      Scale: [" << e.Transform.Scale.x << ", "
        << e.Transform.Scale.y << ", " << e.Transform.Scale.z << "]\n";
      if (e.HasMeshRenderer) {
        f << "    MeshRendererComponent:\n";
        f << "      FilePath: " << e.MeshRenderer.FilePath << "\n";
        f << "      Color: [" << e.MeshRenderer.Color.r << ", "
          << e.MeshRenderer.Color.g << ", " << e.MeshRenderer.Color.b << ", "
          << e.MeshRenderer.Color.a << "]\n";
        f << "      DiffusePath: " << e.MeshRenderer.DiffusePath << "\n";
        f << "      NormalPath: " << e.MeshRenderer.NormalPath << "\n";
        f << "      Metallic: " << e.MeshRenderer.Metallic << "\n";
        f << "      Roughness: " << e.MeshRenderer.Roughness << "\n";
        f << "      AO: " << e.MeshRenderer.AO << "\n";
      }
      return true;
    }
  }
  return false;
}

int Application::InstantiatePrefab(const std::filesystem::path &file) {
  if (!std::filesystem::exists(file)) return -1;

  auto temp = std::make_shared<Scene>();
  if (!SceneSerializer(temp).Deserialize(file.string())) return -1;
  int firstID = -1;
  for (auto &e : temp->GetEntities()) {
    Entity stub = m_Scene->CreateEntity(e.Name);
    int newID = stub.GetID();
    Entity *dst = m_Scene->GetEntityByID(newID);
    if (!dst) continue;
    dst->Transform     = e.Transform;
    dst->HasMeshRenderer = e.HasMeshRenderer;
    dst->MeshRenderer  = e.MeshRenderer;
    dst->HasRigidBody  = e.HasRigidBody;
    dst->RigidBody     = e.RigidBody;
    dst->HasBoxCollider = e.HasBoxCollider;
    dst->BoxCollider   = e.BoxCollider;
    dst->HasSphereCollider = e.HasSphereCollider;
    dst->SphereCollider = e.SphereCollider;
    dst->HasCapsuleCollider = e.HasCapsuleCollider;
    dst->CapsuleCollider = e.CapsuleCollider;
    dst->HasMeshCollider = e.HasMeshCollider;
    dst->MeshCollider  = e.MeshCollider;
    dst->HasLight      = e.HasLight;
    dst->Light         = e.Light;
    dst->HasCamera     = e.HasCamera;
    dst->Camera        = e.Camera;
    dst->HasAudioSource = e.HasAudioSource;
    dst->AudioSource   = e.AudioSource;
    dst->HasWater      = e.HasWater;
    dst->Water         = e.Water;
    if (firstID < 0) firstID = newID;
  }
  return firstID;
}

std::string Application::BuildStandaloneGame(const std::filesystem::path& outDir) {
  namespace fs = std::filesystem;
  if (!GetProject()) return "No project open.";

  std::error_code ec;
  fs::create_directories(outDir, ec);
  if (ec) return "Cannot create output folder: " + ec.message();

  auto editorDll = fs::path(BuildInfo::EngineLibDll);
  auto editorDir = editorDll.parent_path();

  // The runtime flavour of the engine DLL has the same file name as the editor
  // one (so one Game.dll works with both) and lives in its own folder.
  auto runtimeDir      = editorDir / "runtime";
  auto gameRuntimeExe  = runtimeDir / "GameRuntime.exe";
  auto engineRuntimeDll= runtimeDir / editorDll.filename();
  auto sdl2Dll         = editorDir / "SDL2.dll";

  if (!fs::exists(gameRuntimeExe))
    return "runtime/GameRuntime.exe not found next to editor. Rebuild the engine.";
  if (!fs::exists(engineRuntimeDll))
    return "runtime/EngineLib.dll not found next to editor. Rebuild the engine.";

  auto copyOverwrite = [&](const fs::path& from, const fs::path& to) -> std::string {
    std::error_code e;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, e);
    if (e) return "Copy " + from.filename().string() + " failed: " + e.message();
    return {};
  };

  if (auto err = copyOverwrite(gameRuntimeExe,  outDir / "GameRuntime.exe"); !err.empty()) return err;
  if (auto err = copyOverwrite(engineRuntimeDll, outDir / engineRuntimeDll.filename()); !err.empty()) return err;
  if (fs::exists(sdl2Dll)) copyOverwrite(sdl2Dll, outDir / "SDL2.dll");

  if (m_ProjectFilePath.empty())
    return "Project path unknown; save the project first.";
  if (auto err = copyOverwrite(m_ProjectFilePath,
                                outDir / m_ProjectFilePath.filename()); !err.empty()) return err;

  if (ScriptBuilder::IsStale(m_Project) && !ReloadScripts())
    return "Scripts failed to compile (see the popup); the build would ship the old Game.dll.";
  auto gameDll = m_Project.ProjectRoot / m_Project.GameModulePath;
  if (fs::exists(gameDll))
    copyOverwrite(gameDll, outDir / m_Project.GameModulePath);
  else
    return "Game.dll missing (not built). Press Reload Scripts first.";

  auto assetsSrc = m_Project.ProjectRoot / "Assets";
  auto assetsDst = outDir / "Assets";
  if (fs::exists(assetsSrc)) {
    fs::remove_all(assetsDst, ec);
    fs::copy(assetsSrc, assetsDst, fs::copy_options::recursive, ec);
    if (ec) return "Assets copy failed: " + ec.message();
  }

  auto engineAssetsSrc = editorDir / "assets";
  if (fs::exists(engineAssetsSrc)) {
    auto engineAssetsDst = outDir / "assets";
    fs::copy(engineAssetsSrc, engineAssetsDst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  }

  std::ofstream readme((outDir / "README.txt").string());
  if (readme) {
    readme << "Game: " << m_Project.Name << "\n"
           << "Run: GameRuntime.exe\n"
           << "The .myproject + Game.dll + Assets must stay in this folder.\n";
  }
  return {};
}

bool Application::ReloadScripts() {
  if (!GetProject()) return false;

  auto snapshot = SceneSerializer(m_Scene).SerializeToString();

  m_GameModule.Unload();

  auto br = ScriptBuilder::Build(m_Project);
  if (!br.Ok) {
    std::cerr << "[ReloadScripts] BUILD FAILED:\n" << br.Log << "\n";
    ReportScriptBuildFailure(br.Log);

    auto modPath = m_Project.ProjectRoot / m_Project.GameModulePath;
    if (std::filesystem::exists(modPath)) m_GameModule.Load(modPath);
    return false;
  }

  auto modPath = m_Project.ProjectRoot / m_Project.GameModulePath;
  auto mr = m_GameModule.Load(modPath);
  if (!mr.Ok) {
    std::cerr << "[ReloadScripts] DLL load failed: " << mr.Error << "\n";
    ReportScriptBuildFailure("Game.dll did not load: " + mr.Error);
    return false;
  }

  m_Scene->Clear();
  SceneSerializer(m_Scene).DeserializeFromString(snapshot);
  std::cout << "[ReloadScripts] OK\n";
  return true;
}

void Application::ReportScriptBuildFailure(const std::string &log) {
  // The compiler output is long; the lines with "error" say what broke, and
  // the tail says where it stopped.
  std::istringstream in(log);
  std::string line, errors, tail;
  std::deque<std::string> last;
  while (std::getline(in, line)) {
    if (line.find("error") != std::string::npos && errors.size() < 6000)
      errors += line + "\n";
    last.push_back(line);
    if (last.size() > 25) last.pop_front();
  }
  for (const auto &l : last) tail += l + "\n";
  m_ScriptBuildLog = errors.empty() ? tail : errors;
  m_ShowScriptBuildError = true;
}

void Application::MarkSceneSynced() {
  std::error_code ec;
  m_SceneFileTime = std::filesystem::last_write_time(m_CurrentScenePath, ec);
  m_SceneHashAtSync = std::hash<std::string>{}(SceneSerializer(m_Scene).SerializeToString());
}

void Application::ReloadSceneIfChangedOnDisk() {
  if (m_CurrentScenePath.empty() || m_SceneHashAtSync == 0) return;
  std::error_code ec;
  auto onDisk = std::filesystem::last_write_time(m_CurrentScenePath, ec);
  if (ec || onDisk == m_SceneFileTime) return;
  // Edited here since it was loaded: those edits win (Save Scene would
  // overwrite the file anyway).
  if (std::hash<std::string>{}(SceneSerializer(m_Scene).SerializeToString()) != m_SceneHashAtSync) return;
  m_Scene->Clear();
  if (SceneSerializer(m_Scene).Deserialize(m_CurrentScenePath))
    std::cout << "[Play] scene changed on disk, reloaded " << m_CurrentScenePath << "\n";
    MarkSceneSynced();
}

}
