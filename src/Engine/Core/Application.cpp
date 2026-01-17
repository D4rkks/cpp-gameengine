#define IMGUI_DEFINE_MATH_OPERATORS
#include "Application.h"
#include "../Scripts/CameraController.h"
#include "../Scripts/PlayerController.h"

#include <GL/glew.h>
#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <memory>

#include "../Audio/AudioEngine.h"
#include "../Renderer/Buffer.h"
#include "../Renderer/Framebuffer.h"
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

#include "../Scene/SceneSerializer.h"
#include "Input.h"
#include <algorithm>
#include <fstream>

namespace Engine {

struct Ray {
  glm::vec3 Origin;
  glm::vec3 Direction;
};

static bool RayIntersectsAABB(const Ray &ray, const glm::vec3 &min,
                              const glm::vec3 &max, float &t) {
  float tx1 = (min.x - ray.Origin.x) / ray.Direction.x;
  float tx2 = (max.x - ray.Origin.x) / ray.Direction.x;

  float tmin = std::min(tx1, tx2);
  float tmax = std::max(tx1, tx2);

  float ty1 = (min.y - ray.Origin.y) / ray.Direction.y;
  float ty2 = (max.y - ray.Origin.y) / ray.Direction.y;

  tmin = std::max(tmin, std::min(ty1, ty2));
  tmax = std::min(tmax, std::max(ty1, ty2));

  float tz1 = (min.z - ray.Origin.z) / ray.Direction.z;
  float tz2 = (max.z - ray.Origin.z) / ray.Direction.z;

  tmin = std::max(tmin, std::min(tz1, tz2));
  tmax = std::min(tmax, std::max(tz1, tz2));

  t = tmin;
  return tmax >= tmin && tmax >= 0.0f;
}

static bool RayIntersectsEntity(const Ray &ray, Entity &entity, float &t) {
  glm::mat4 model =
      glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
      glm::toMat4(glm::quat(entity.Transform.Rotation)) *
      glm::scale(glm::mat4(1.0f), entity.Transform.Scale);

  glm::mat4 invModel = glm::inverse(model);

  glm::vec4 localOrigin = invModel * glm::vec4(ray.Origin, 1.0f);
  glm::vec4 localDir = invModel * glm::vec4(ray.Direction, 0.0f);

  Ray localRay = {glm::vec3(localOrigin), glm::normalize(glm::vec3(localDir))};
  return RayIntersectsAABB(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), t);
}

Application *Application::s_Instance = nullptr;

Application::Application() {
  s_Instance = this;
#ifdef GAME_MODE_RUNTIME
  m_Window = new Window(WindowProps("Game (Runtime)", 1280, 720));
  m_AppState = AppState::Runtime;
#else
  m_Window = new Window(WindowProps("Engine - UI RELOADED", 1280, 720));
  m_AppState = AppState::Editor;
#endif
  std::cout << "[App] Starting..." << std::endl;
  Renderer::Init();
  Renderer2D::Init();
  AudioEngine::Init();
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
  SceneSerializer serializer(m_Scene);
  std::vector<std::string> paths = {"Game.scene", "../Game.scene",
                                    "assets/scenes/Game.scene",
                                    "../assets/scenes/Game.scene"};

  bool loaded = false;
  for (const auto &path : paths) {
    if (std::filesystem::exists(path)) {
      std::cout << "[Runtime] Found specific scene: " << path << std::endl;
      if (serializer.Deserialize(path)) {
        m_CurrentScenePath = path;
        m_Scene->OnPhysicsStart();
        loaded = true;
        std::cout << "[Runtime] Loaded scene: " << path << std::endl;
        std::cout << "[Runtime] Entity count: " << m_Scene->GetEntities().size()
                  << std::endl;
        break;
      }
    }
  }

  if (!loaded) {
    std::cout << "[Runtime] Game.scene not found. Scanning directory..."
              << std::endl;
    for (const auto &entry : std::filesystem::directory_iterator(".")) {
      if (entry.path().extension() == ".scene") {
        if (entry.path().filename() == "Default.scene")
          continue;

        std::string p = entry.path().string();
        std::cout << "[Runtime] Found fallback scene: " << p << std::endl;
        if (serializer.Deserialize(p)) {
          m_CurrentScenePath = p;
          m_Scene->OnPhysicsStart();
          loaded = true;
          break;
        }
      }
    }

    if (!loaded && std::filesystem::exists("..")) {
      std::cout << "[Runtime] Nothing in bin/. Scanning parent directory "
                   "(project root)..."
                << std::endl;
      for (const auto &entry : std::filesystem::directory_iterator("..")) {
        if (entry.is_directory())
          continue;
        if (entry.path().extension() == ".scene") {
          if (entry.path().filename() == "Default.scene")
            continue;

          std::string p = entry.path().string();
          std::cout << "[Runtime] Found fallback scene in parent: " << p
                    << std::endl;
          if (serializer.Deserialize(p)) {
            m_CurrentScenePath = p;
            m_Scene->OnPhysicsStart();
            loaded = true;
            break;
          }
        }
      }
    }

    if (!loaded && std::filesystem::exists("../assets/scenes")) {
      std::cout << "[Runtime] Nothing in parent. Scanning ../assets/scenes..."
                << std::endl;
      for (const auto &entry :
           std::filesystem::directory_iterator("../assets/scenes")) {
        if (entry.path().extension() == ".scene") {
          if (entry.path().filename() == "Default.scene")
            continue;

          std::string p = entry.path().string();
          std::cout << "[Runtime] Found fallback scene in ../assets: " << p
                    << std::endl;
          if (serializer.Deserialize(p)) {
            m_CurrentScenePath = p;
            m_Scene->OnPhysicsStart();
            loaded = true;
            break;
          }
        }
      }
    }

    if (!loaded && std::filesystem::exists("assets/scenes")) {
      std::cout << "[Runtime] Nothing in root. Scanning assets/scenes..."
                << std::endl;
      for (const auto &entry :
           std::filesystem::directory_iterator("assets/scenes")) {
        if (entry.path().extension() == ".scene") {
          std::string p = entry.path().string();
          std::cout << "[Runtime] Found fallback scene in assets: " << p
                    << std::endl;
          if (serializer.Deserialize(p)) {
            m_CurrentScenePath = p;
            m_Scene->OnPhysicsStart();
            loaded = true;
            break;
          }
        }
      }
    }
  }

  if (!loaded) {
    std::cerr << "CRITICAL: Could not find ANY .scene file!" << std::endl;
    std::cerr << "Current Directory: " << std::filesystem::current_path()
              << std::endl;
#ifndef GAME_MODE_RUNTIME
    if (m_AppState != AppState::Editor) {
      std::cout << "[Correction] AppState was " << (int)m_AppState
                << ", forcing to Editor." << std::endl;
      m_AppState = AppState::Editor;
    } else {
      std::cout << "[Constructor] AppState is correctly Editor." << std::endl;
    }
#else
    std::cout << "[Constructor] AppState is Runtime." << std::endl;
#endif
  }
#endif
}

Application::~Application() {
  AudioEngine::Shutdown();
  Renderer2D::Shutdown();
  delete m_Window;
}

void Application::LoadScene(const std::string &path) { m_NextScenePath = path; }

void Application::Run() {
  float vertices[] = {
      // Front (Normal: 0, 0, 1)
      -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, -0.5f, 0.5f, 0.0f,
      0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
      -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,

      // Back (Normal: 0, 0, -1)
      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.5f, -0.5f, -0.5f,
      0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f,
      1.0f, -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,

      // Top (Normal: 0, 1, 0)
      -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f,
      1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
      -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

      // Bottom (Normal: 0, -1, 0)
      -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.5f,
      0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
      1.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,

      // Right (Normal: 1, 0, 0)
      0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
      0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

      // Left (Normal: -1, 0, 0)
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
      0,  1,  2,  2,  3,  0,  // Front
      4,  5,  6,  6,  7,  4,  // Back
      8,  9,  10, 10, 11, 8,  // Top
      12, 13, 14, 14, 15, 12, // Bottom
      16, 17, 18, 18, 19, 16, // Right
      20, 21, 22, 22, 23, 20  // Left
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
    
    out vec3 v_Normal;
    out vec3 v_FragPos;
    out vec2 v_TexCoord;

    void main() {
        v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
        v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal; 
        v_TexCoord = a_TexCoord;
        gl_Position = u_ViewProjection * vec4(v_FragPos, 1.0);
    }
  )";

  std::string fragmentSrc = R"(
    #version 330 core
    layout(location = 0) out vec4 color;
    
    in vec3 v_Normal;
    in vec3 v_FragPos;
    in vec2 v_TexCoord;
    
    uniform vec3 u_SunDirection;
    uniform vec3 u_SunColor;
    uniform float u_SunIntensity;

    uniform vec3 u_AmbientColor;
    uniform float u_AmbientIntensity;

    #define MAX_POINT_LIGHTS 8
    uniform int u_PointLightCount;
    uniform vec3 u_PointLightPositions[MAX_POINT_LIGHTS];
    uniform vec3 u_PointLightColors[MAX_POINT_LIGHTS];
    uniform float u_PointLightIntensities[MAX_POINT_LIGHTS];
    uniform float u_PointLightRadii[MAX_POINT_LIGHTS];

    uniform vec3 u_ViewPos;
    uniform vec4 u_Color;
    uniform sampler2D u_Texture;
    uniform int u_HasTexture;
    uniform float u_TilingFactor;

    uniform sampler2D u_DiffuseMap;
    uniform int u_HasDiffuseMap;
    uniform sampler2D u_NormalMap;
    uniform int u_HasNormalMap;
    uniform int u_IsUnlit;

    void main() {
        if (u_IsUnlit > 0) {
            vec4 texColor = vec4(1.0);
            if (u_HasTexture > 0) texColor = texture(u_Texture, v_TexCoord * u_TilingFactor);
            if (u_HasDiffuseMap > 0) texColor *= texture(u_DiffuseMap, v_TexCoord * u_TilingFactor);
            color = u_Color * texColor;
            return;
        }

        vec3 ambient = u_AmbientColor * u_AmbientIntensity;
        vec3 norm = normalize(v_Normal);
        if (u_HasNormalMap > 0) {
            vec3 n = texture(u_NormalMap, v_TexCoord * u_TilingFactor).rgb;
            n = normalize(n * 2.0 - 1.0);
            norm = normalize(norm + n * 0.5); 
        }

        vec3 lightDir = normalize(-u_SunDirection);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = u_SunColor * diff * u_SunIntensity;
        
        float specularStrength = 0.5;
        vec3 viewDir = normalize(u_ViewPos - v_FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);  
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
        vec3 specular = u_SunColor * (spec * specularStrength);  

        vec3 pointLightsResult = vec3(0.0);
        for(int i = 0; i < u_PointLightCount; i++) {
             float distance = length(u_PointLightPositions[i] - v_FragPos);
             if (distance < u_PointLightRadii[i]) {
                 vec3 lightDirP = normalize(u_PointLightPositions[i] - v_FragPos);
                 float diffP = max(dot(norm, lightDirP), 0.0);
                 float att = 1.0 - (distance / u_PointLightRadii[i]);
                 att = att * att;
                 pointLightsResult += u_PointLightColors[i] * diffP * u_PointLightIntensities[i] * att;
             }
        }

        vec4 texColor = vec4(1.0);
        if (u_HasTexture > 0) texColor = texture(u_Texture, v_TexCoord * u_TilingFactor);
        if (u_HasDiffuseMap > 0) texColor *= texture(u_DiffuseMap, v_TexCoord * u_TilingFactor);

        vec3 diffuseColor = u_Color.rgb * texColor.rgb;
        vec3 result = (ambient + diffuse + specular + pointLightsResult) * diffuseColor;
        result = pow(result, vec3(1.0 / 2.2));
        color = vec4(result, u_Color.a * texColor.a);
    }
  )";

  std::shared_ptr<Shader> shader;
  shader.reset(new Shader(vertexSrc, fragmentSrc));

  FramebufferSpecification fbSpec;
  fbSpec.Width = 1280;
  fbSpec.Height = 720;
  std::shared_ptr<Framebuffer> framebuffer;
  framebuffer.reset(Framebuffer::Create(fbSpec));

#include "../Scene/SceneSerializer.h"

#ifdef GAME_MODE_RUNTIME
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
        void main() {    
            FragColor = texture(skybox, TexCoords);
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

    if (!m_NextScenePath.empty()) {
      m_Scene->Clear();
      SceneSerializer serializer(m_Scene);
      if (serializer.Deserialize(m_NextScenePath)) {
        m_CurrentScenePath = m_NextScenePath;
        m_Scene->OnPhysicsStart();
      }
      m_NextScenePath.clear();
    }

    if (m_AppState == AppState::Runtime) {
      SDL_SetRelativeMouseMode(m_MouseLocked ? SDL_TRUE : SDL_FALSE);
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (m_AppState == AppState::Runtime) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
          m_MouseLocked = !m_MouseLocked;
          SDL_SetRelativeMouseMode(m_MouseLocked ? SDL_TRUE : SDL_FALSE);
        }
      }

      ImGui_ImplSDL2_ProcessEvent(&event);

#ifndef GAME_MODE_RUNTIME
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
            }
          }
        }
      }
#endif

      if (event.type == SDL_QUIT)
        m_Running = false;

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

    float time = (float)SDL_GetTicks() / 1000.0f;
    float ts = time - m_LastFrameTime;
    m_LastFrameTime = time;

    if (m_AppState == AppState::Runtime) {
      m_Scene->OnUpdate(ts);
      if (!m_Scene->GetPrimaryCameraEntity()) {
        m_Camera.OnUpdate(ts);
      }
    } else {
      m_Camera.OnUpdate(ts);
      m_Scene->OnUpdateEditor(ts);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

#ifndef GAME_MODE_RUNTIME
    ImGuizmo::BeginFrame();

    if (Input::IsKeyPressed(SDL_SCANCODE_LCTRL) &&
        Input::IsKeyPressed(SDL_SCANCODE_S)) {
      if (!m_CurrentScenePath.empty()) {
        SceneSerializer serializer(m_Scene);
        serializer.Serialize(m_CurrentScenePath);
      } else {
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
          if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
            SaveHistoryState();
            m_Scene->Clear();
            m_CurrentScenePath = "";
            m_Scene->OnPhysicsStop();
          }
          if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            std::string filepath =
                FileDialogs::OpenFile("Scene (*.scene)\0*.scene\0");
            if (!filepath.empty()) {
              SceneSerializer serializer(m_Scene);
              if (serializer.Deserialize(filepath)) {
                m_CurrentScenePath = filepath;
                AddToRecentScenes(filepath);
                m_Scene->OnPhysicsStart();
              }
            }
          }
          if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
            if (!m_CurrentScenePath.empty()) {
              SceneSerializer serializer(m_Scene);
              serializer.Serialize(m_CurrentScenePath);
              AddToRecentScenes(m_CurrentScenePath);
            } else {
              std::string filepath =
                  FileDialogs::SaveFile("Scene (*.scene)\0*.scene\0");
              if (!filepath.empty()) {
                SceneSerializer serializer(m_Scene);
                serializer.Serialize(filepath);
                m_CurrentScenePath = filepath;
                AddToRecentScenes(filepath);
              }
            }
          }
          if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
            std::string filepath =
                FileDialogs::SaveFile("Scene (*.scene)\0*.scene\0");
            if (!filepath.empty()) {
              SceneSerializer serializer(m_Scene);
              serializer.Serialize(filepath);
              m_CurrentScenePath = filepath;
              AddToRecentScenes(filepath);
            }
          }
          if (ImGui::MenuItem("Back to Hub")) {
            m_AppState = AppState::ProjectHub;
          }
          if (ImGui::MenuItem("Exit"))
            m_Running = false;
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
        ImGui::EndMainMenuBar();
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

      ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
      ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
      ImGui::DockBuilderDockWindow("Inspector", dock_right);
      ImGui::DockBuilderDockWindow("Debug Control", dock_right);
      ImGui::DockBuilderDockWindow("Content Browser", dock_down);

      ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::End();
#endif

#ifndef GAME_MODE_RUNTIME
    framebuffer->Bind();
#else
    auto [dw, dh] = m_Window->GetDrawableSize();
    glViewport(0, 0, dw, dh);
    m_Camera.SetViewportSize((float)dw, (float)dh);
#endif
    if (m_AppState == AppState::Editor) {
      framebuffer->Bind();
    } else if (m_AppState == AppState::Runtime) {
      auto [dw, dh] = m_Window->GetDrawableSize();
      glViewport(0, 0, dw, dh);
      m_Camera.SetViewportSize((float)dw, (float)dh);
    }

    Renderer::SetClearColor({m_Scene->GetSkyColor(), 1.0f});
    Renderer::Clear();

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

    glm::mat4 viewProjection;

    if (m_AppState == AppState::Runtime) {
      auto [runtime_dw, runtime_dh] = m_Window->GetDrawableSize();

      float aspectRatio = (float)runtime_dw / (float)runtime_dh;

      Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
      if (primaryCamera) {
        static int logCounter = 0;
        if (logCounter++ % 120 == 0) {
          std::cout << "[Runtime] Using Primary Camera Entity ID: "
                    << primaryCamera->GetID() << std::endl;
          std::cout << "[Runtime] Aspect Ratio: " << aspectRatio << std::endl;
        }

        glm::mat4 view = glm::inverse(
            glm::translate(glm::mat4(1.0f),
                           primaryCamera->Transform.Translation) *
            glm::toMat4(glm::quat(primaryCamera->Transform.Rotation)));
        glm::mat4 projection =
            primaryCamera->Camera.GetProjectionMatrix(aspectRatio);
        viewProjection = projection * view;
      } else {
        static int logCounter = 0;
        if (logCounter++ % 120 == 0) {
          std::cout
              << "[Runtime] NO PRIMARY CAMERA FOUND! Fallback to Editor Camera."
              << std::endl;
        }
        viewProjection = m_Camera.GetViewProjection();
      }
    }

    else {
      viewProjection = m_Camera.GetViewProjection();
    }

    shader->Bind();
    shader->UploadUniformMat4("u_ViewProjection", viewProjection);

    if (m_AppState == AppState::Runtime) {
      Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
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
      Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
    }

    glm::vec3 cameraPos = m_Camera.GetPosition();
    if (m_AppState == AppState::Runtime) {
      Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
      if (primaryCamera)
        cameraPos = primaryCamera->Transform.Translation;
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

    for (const auto &entity : m_Scene->GetEntities()) {
      if (entity.HasLight) {
        if (entity.Light.Type == LightComponent::LightType::Directional) {
          glm::quat q(entity.Transform.Rotation);
          sunDirection = q * glm::vec3(0, 0, -1);
          sunColor = entity.Light.Color;
          sunIntensity = entity.Light.Intensity;
          hasSun = true;
        } else {
          pointLights.push_back({entity.Transform.Translation,
                                 entity.Light.Color, entity.Light.Intensity,
                                 entity.Light.Radius});
        }
      }
    }

    shader->UploadUniformFloat3("u_SunDirection", sunDirection);
    shader->UploadUniformFloat3("u_SunColor", sunColor);
    shader->UploadUniformFloat("u_SunIntensity", hasSun ? sunIntensity : 0.0f);
    shader->UploadUniformFloat3("u_AmbientColor", m_Scene->GetAmbientColor());
    shader->UploadUniformFloat("u_AmbientIntensity",
                               m_Scene->GetAmbientIntensity());

    shader->UploadUniformInt("u_PointLightCount", (int)pointLights.size());
    for (int i = 0; i < pointLights.size() && i < 8; i++) {
      std::string prefix = "u_PointLight";
      std::string iStr = std::to_string(i);
      shader->UploadUniformFloat3(prefix + "Positions[" + iStr + "]",
                                  pointLights[i].Pos);
      shader->UploadUniformFloat3(prefix + "Colors[" + iStr + "]",
                                  pointLights[i].Color);
      shader->UploadUniformFloat(prefix + "Intensities[" + iStr + "]",
                                 pointLights[i].Intensity);
      shader->UploadUniformFloat(prefix + "Radii[" + iStr + "]",
                                 pointLights[i].Radius);
    }

    for (const auto &entity : m_Scene->GetEntities()) {
      if (entity.IsUIElement()) {
        continue;
      }

      if (!entity.HasSpriteRenderer) {
        continue;
      }

      if (m_AppState == AppState::Runtime) {
        Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
        if (primaryCamera && entity.GetID() == primaryCamera->GetID())
          continue;
      }

      glm::mat4 transform =
          glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
          glm::toMat4(glm::quat(entity.Transform.Rotation)) *
          glm::scale(glm::mat4(1.0f), entity.Transform.Scale);

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
      Renderer::Submit(shader, m_VertexArray, transform);
    }

    for (auto &entity : m_Scene->GetEntities()) {
      if (!entity.HasMeshRenderer || !entity.MeshRenderer.Mesh)
        continue;

      if (entity.IsUIElement())
        continue;

      if (m_AppState == AppState::Runtime) {
        Entity *primaryCamera = m_Scene->GetPrimaryCameraEntity();
        if (primaryCamera && entity.GetID() == primaryCamera->GetID())
          continue;
      }

      glm::mat4 transform =
          glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
          glm::toMat4(glm::quat(entity.Transform.Rotation)) *
          glm::scale(glm::mat4(1.0f), entity.Transform.Scale);

      bool shouldCull = false;
      Entity *primaryCamEntity = m_Scene->GetPrimaryCameraEntity();
      if (primaryCamEntity && primaryCamEntity->Camera.FrustumCulling) {
        glm::vec4 min = viewProjection * transform *
                        glm::vec4(entity.MeshRenderer.Mesh->GetAABBMin(), 1.0f);
        glm::vec4 max = viewProjection * transform *
                        glm::vec4(entity.MeshRenderer.Mesh->GetAABBMax(), 1.0f);

        bool outside = (max.x < -max.w && min.x < -min.w) ||
                       (min.x > min.w && max.x > max.w) ||
                       (max.y < -max.w && min.y < -min.w) ||
                       (min.y > min.w && max.y > max.w) ||
                       (max.z < -max.w && min.z < -min.w) ||
                       (min.z > min.w && max.z > max.w);

        if (outside)
          shouldCull = true;
      }

      if (shouldCull)
        continue;

      std::shared_ptr<Mesh> meshToRender = entity.MeshRenderer.Mesh;
      if (!entity.MeshRenderer.LODs.empty()) {
        float dist = glm::distance(cameraPos, entity.Transform.Translation);
        for (const auto &lod : entity.MeshRenderer.LODs) {
          if (dist < lod.Distance) {
            meshToRender = lod.Mesh;
            break;
          }
        }
      }

      shader->UploadUniformInt("u_HasTexture", 0);

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
      Renderer::SubmitMesh(meshToRender, shader, transform);
    }

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

          glm::mat4 transform =
              glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
              glm::toMat4(glm::quat(entity.Transform.Rotation)) *
              glm::scale(glm::mat4(1.0f), entity.Transform.Scale);

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

    if (m_AppState != AppState::Runtime) {
      if (m_Grid)
        m_Grid->Draw(m_Camera);
    }

    if (m_AppState != AppState::Runtime) {
      for (const auto &entity : m_Scene->GetEntities()) {
        if (entity.HasCamera) {
          glm::quat q(entity.Transform.Rotation);
          glm::vec3 forward = q * glm::vec3(0, 0, -1);
          glm::vec3 right = q * glm::vec3(1, 0, 0);
          glm::vec3 up = q * glm::vec3(0, 1, 0);

          glm::vec3 p0 = entity.Transform.Translation;
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

        if (entity.HasBoxCollider) {
          glm::vec3 size = entity.BoxCollider.Size;

          glm::mat4 colliderTransform =
              glm::translate(glm::mat4(1.0f), entity.Transform.Translation) *
              glm::toMat4(glm::quat(entity.Transform.Rotation)) *
              glm::translate(glm::mat4(1.0f), entity.BoxCollider.Offset) *
              glm::scale(glm::mat4(1.0f), entity.Transform.Scale * size);

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
      m_Scene->OnUIRender((float)dw, (float)dh, {0.0f, 0.0f});
    }

    if (m_AppState == AppState::Editor) {
      framebuffer->Unbind();
    }

#ifndef GAME_MODE_RUNTIME
    m_SceneHierarchyPanel.OnImGuiRender();
    m_ContentBrowserPanel.OnImGuiRender();

    ImGui::Begin("Debug Control");
    ImGui::Text("Application Average %.3f ms/frame (%.1f FPS)",
                1000.0f / io.Framerate, io.Framerate);

    if (ImGui::Button("Restart Physics") && m_AppState == AppState::Runtime) {
      m_Scene->OnPhysicsStart();
    }

    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    if (selectedEntity.GetID() != -1) {
      ImGui::Separator();
      ImGui::Text("Selected Entity ID: %d", selectedEntity.GetID());

      bool changed = false;

      changed |= ImGui::DragFloat3(
          "Position", glm::value_ptr(selectedEntity.Transform.Translation),
          0.1f);

      glm::vec3 rotationDeg = glm::degrees(selectedEntity.Transform.Rotation);
      if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotationDeg), 0.1f)) {
        selectedEntity.Transform.Rotation = glm::radians(rotationDeg);
        changed = true;
      }

      changed |= ImGui::DragFloat3(
          "Scale", glm::value_ptr(selectedEntity.Transform.Scale), 0.1f);
      changed |= ImGui::ColorEdit3(
          "Color", glm::value_ptr(selectedEntity.SpriteRenderer.Color));

      ImGui::Text("Vertices: 36 (Cube Primitive)");

      if (changed) {
        m_Scene->UpdateEntity(selectedEntity);
      }
    }

    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
        (viewportSize.x != fbSpec.Width || viewportSize.y != fbSpec.Height)) {
      fbSpec.Width = (uint32_t)viewportSize.x;
      fbSpec.Height = (uint32_t)viewportSize.y;
      framebuffer->Resize(fbSpec.Width, fbSpec.Height);

      m_Camera.SetViewportSize(fbSpec.Width, fbSpec.Height);
    }

    uint32_t textureID = framebuffer->GetColorAttachmentRendererID();
    ImVec2 vPos = ImGui::GetCursorScreenPos();
    m_ViewportOffset = {vPos.x, vPos.y};

    ImGui::Image((void *)(intptr_t)textureID,
                 ImVec2((float)fbSpec.Width, (float)fbSpec.Height),
                 ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::IsWindowHovered() && !ImGuizmo::IsOver()) {
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
          ImVec2 windowPos = ImGui::GetWindowPos();
          glm::vec2 relativePos = {m_MarqueeStart.x - windowPos.x,
                                   m_MarqueeStart.y - windowPos.y};

          float x = (2.0f * relativePos.x) / viewportSize.x - 1.0f;
          float y = 1.0f - (2.0f * relativePos.y) / viewportSize.y;

          glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);
          glm::vec4 ray_eye = glm::inverse(m_Camera.GetProjection()) * ray_clip;
          ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
          glm::vec3 ray_world =
              glm::vec3(glm::inverse(m_Camera.GetViewMatrix()) * ray_eye);
          ray_world = glm::normalize(ray_world);

          Ray ray = {m_Camera.GetPosition(), ray_world};
          int hitID = -1;
          float minT = std::numeric_limits<float>::max();

          for (auto &entity : m_Scene->GetEntities()) {
            float t;
            if (RayIntersectsEntity(ray, entity, t)) {
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
                m_SceneHierarchyPanel.AddSelectedEntity(hitID);
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
        } else {
          if (!ImGui::GetIO().KeyCtrl)
            m_SceneHierarchyPanel.ClearSelection();

          glm::vec2 min = {std::min(m_MarqueeStart.x, marqueeEnd.x),
                           std::min(m_MarqueeStart.y, marqueeEnd.y)};
          glm::vec2 max = {std::max(m_MarqueeStart.x, marqueeEnd.x),
                           std::max(m_MarqueeStart.y, marqueeEnd.y)};

          ImVec2 windowPos = ImGui::GetWindowPos();
          glm::mat4 vp = m_Camera.GetViewProjection();

          for (auto &entity : m_Scene->GetEntities()) {
            if (!entity.HasSpriteRenderer)
              continue;

            glm::vec4 worldPos = glm::vec4(entity.Transform.Translation, 1.0f);
            glm::vec4 clipPos = vp * worldPos;

            if (clipPos.w > 0.0f) {
              glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
              glm::vec2 screenPos;
              screenPos.x =
                  (ndc.x + 1.0f) * 0.5f * viewportSize.x + windowPos.x;
              screenPos.y =
                  (1.0f - ndc.y) * 0.5f * viewportSize.y + windowPos.y;

              if (screenPos.x >= min.x && screenPos.x <= max.x &&
                  screenPos.y >= min.y && screenPos.y <= max.y) {
                m_SceneHierarchyPanel.AddSelectedEntity(entity.GetID());
              }
            }
          }
        }
      }
    }

    selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    if (selectedEntity.GetID() != -1) {
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();

      float windowWidth = (float)ImGui::GetWindowWidth();
      float windowHeight = (float)ImGui::GetWindowHeight();
      ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                        windowWidth, windowHeight);

      const glm::mat4 &cameraProjection = m_Camera.GetProjection();
      glm::mat4 cameraView = m_Camera.GetViewMatrix();

      glm::mat4 transform =
          glm::translate(glm::mat4(1.0f),
                         selectedEntity.Transform.Translation) *
          glm::toMat4(glm::quat(selectedEntity.Transform.Rotation)) *
          glm::scale(glm::mat4(1.0f), selectedEntity.Transform.Scale);

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
                glm::mat4 entityTransform =
                    glm::translate(glm::mat4(1.0f),
                                   entity.Transform.Translation) *
                    glm::toMat4(glm::quat(entity.Transform.Rotation)) *
                    glm::scale(glm::mat4(1.0f), entity.Transform.Scale);
                m_InitialSelectionTransforms[id] = entityTransform;
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
              glm::vec3 translation, rotation, scale;
              float matrixTranslation[3], matrixRotation[3], matrixScale[3];
              ImGuizmo::DecomposeMatrixToComponents(
                  glm::value_ptr(newTransform), matrixTranslation,
                  matrixRotation, matrixScale);

              e.Transform.Translation = glm::make_vec3(matrixTranslation);
              e.Transform.Rotation =
                  glm::radians(glm::make_vec3(matrixRotation));
              e.Transform.Scale = glm::make_vec3(matrixScale);
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
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
  static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

  ImGui::Begin("Project Hub", nullptr, window_flags);
  ImGui::PopStyleVar(5);

  ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 300,
                             viewport->WorkSize.y * 0.5f - 250));
  ImGui::BeginChild("HubContent", ImVec2(600, 500), true,
                    ImGuiWindowFlags_None);

  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
  ImGui::SetWindowFontScale(1.5f);
  ImGui::Text("C++ ENGINE");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopFont();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::Text("Start");
  if (ImGui::Button("New Project / Scene", ImVec2(580, 50))) {
    m_Scene->Clear();
    m_CurrentScenePath = "";
    Entity cam = m_Scene->CreateEntity("Camera");
    cam.Transform.Translation.z = 5.0f;
    m_Scene->UpdateEntity(cam);
    m_AppState = AppState::Editor;
    m_Scene->OnPhysicsStop();
  }

  if (ImGui::Button("Open Scene...", ImVec2(580, 50))) {
    std::string filepath = FileDialogs::OpenFile("Scene (*.scene)\0*.scene\0");
    if (!filepath.empty()) {
      SceneSerializer serializer(m_Scene);
      if (serializer.Deserialize(filepath)) {
        m_CurrentScenePath = filepath;
        AddToRecentScenes(filepath);
        m_AppState = AppState::Editor;
        m_Scene->OnPhysicsStart();
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("Recent Scenes");

  ImGui::BeginChild("RecentList", ImVec2(580, 200), true);
  for (const auto &path : m_RecentScenes) {
    if (ImGui::Button(path.c_str(), ImVec2(560, 30))) {
      SceneSerializer serializer(m_Scene);
      if (serializer.Deserialize(path)) {
        m_CurrentScenePath = path;
        m_AppState = AppState::Editor;
        AddToRecentScenes(path);
        m_Scene->OnPhysicsStart();
      }
    }
  }
  ImGui::EndChild();

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::End();
}
} // namespace Engine
