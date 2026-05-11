#include "Window.h"

#include <GL/glew.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace Engine {

Window::Window(const WindowProps &props) { Init(props); }

Window::~Window() { Shutdown(); }

void Window::Init(const WindowProps &props) {
  m_Data.Title = props.Title;
  m_Data.Width = props.Width;
  m_Data.Height = props.Height;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) !=
      0) {
    std::string error = "Error: SDL_Init(): " + std::string(SDL_GetError());
    std::cerr << error << std::endl;
    throw std::runtime_error(error);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

  auto window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_ALLOW_HIGHDPI);
  m_Window = SDL_CreateWindow(m_Data.Title.c_str(), SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, m_Data.Width,
                              m_Data.Height, window_flags);

  if (!m_Window) {
    std::string error =
        "Error: SDL_CreateWindow(): " + std::string(SDL_GetError());
    std::cerr << error << std::endl;
    throw std::runtime_error(error);
  }

  m_Context = SDL_GL_CreateContext(m_Window);
  SDL_GL_MakeCurrent(m_Window, m_Context);
  SDL_GL_SetSwapInterval(1);

  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (GLEW_OK != err) {
    std::string error =
        "Error: " + std::string((const char *)glewGetErrorString(err));
    std::cerr << error << std::endl;
    throw std::runtime_error(error);
  }
  std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
  std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << std::endl;

  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  if (std::filesystem::exists("resources/editor/fonts/Roboto-Medium.ttf")) {
    io.FontDefault = io.Fonts->AddFontFromFileTTF(
        "resources/editor/fonts/Roboto-Medium.ttf", 18.0f);
  }

  auto &style = ImGui::GetStyle();
  auto &colors = style.Colors;

  style.WindowRounding    = 4.0f;
  style.ChildRounding     = 4.0f;
  style.FrameRounding     = 4.0f;
  style.GrabRounding      = 4.0f;
  style.PopupRounding     = 4.0f;
  style.TabRounding       = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.WindowBorderSize  = 0.0f;
  style.ChildBorderSize   = 0.0f;
  style.PopupBorderSize   = 1.0f;
  style.FrameBorderSize   = 0.0f;
  style.WindowPadding     = ImVec2(10, 10);
  style.FramePadding      = ImVec2(8, 4);
  style.ItemSpacing       = ImVec2(8, 6);
  style.ItemInnerSpacing  = ImVec2(6, 4);
  style.IndentSpacing     = 18.0f;
  style.ScrollbarSize     = 12.0f;

  const ImVec4 bg        {0.09f, 0.10f, 0.12f, 1.0f};
  const ImVec4 panel     {0.12f, 0.13f, 0.16f, 1.0f};
  const ImVec4 panelHi   {0.16f, 0.18f, 0.22f, 1.0f};
  const ImVec4 accent    {0.25f, 0.55f, 0.95f, 1.0f};
  const ImVec4 accentHi  {0.35f, 0.65f, 1.00f, 1.0f};
  const ImVec4 text      {0.90f, 0.92f, 0.95f, 1.0f};
  const ImVec4 textDim   {0.50f, 0.55f, 0.62f, 1.0f};

  colors[ImGuiCol_WindowBg]            = bg;
  colors[ImGuiCol_ChildBg]             = bg;
  colors[ImGuiCol_PopupBg]             = panel;
  colors[ImGuiCol_Border]              = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);

  colors[ImGuiCol_Text]                = text;
  colors[ImGuiCol_TextDisabled]        = textDim;

  colors[ImGuiCol_FrameBg]             = panel;
  colors[ImGuiCol_FrameBgHovered]      = panelHi;
  colors[ImGuiCol_FrameBgActive]       = ImVec4(0.20f, 0.30f, 0.45f, 1.0f);

  colors[ImGuiCol_TitleBg]             = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  colors[ImGuiCol_TitleBgActive]       = ImVec4(0.08f, 0.10f, 0.14f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);

  colors[ImGuiCol_MenuBarBg]           = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);

  colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.05f, 0.06f, 0.08f, 1.0f);
  colors[ImGuiCol_ScrollbarGrab]       = panelHi;
  colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.22f, 0.26f, 0.32f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = accent;

  colors[ImGuiCol_CheckMark]           = accent;
  colors[ImGuiCol_SliderGrab]          = accent;
  colors[ImGuiCol_SliderGrabActive]    = accentHi;

  colors[ImGuiCol_Button]              = panel;
  colors[ImGuiCol_ButtonHovered]       = panelHi;
  colors[ImGuiCol_ButtonActive]        = accent;

  colors[ImGuiCol_Header]              = panel;
  colors[ImGuiCol_HeaderHovered]       = panelHi;
  colors[ImGuiCol_HeaderActive]        = ImVec4(0.20f, 0.30f, 0.45f, 1.0f);

  colors[ImGuiCol_Separator]           = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);
  colors[ImGuiCol_SeparatorHovered]    = accent;
  colors[ImGuiCol_SeparatorActive]     = accentHi;

  colors[ImGuiCol_ResizeGrip]          = ImVec4(0.18f, 0.22f, 0.28f, 1.0f);
  colors[ImGuiCol_ResizeGripHovered]   = accent;
  colors[ImGuiCol_ResizeGripActive]    = accentHi;

  colors[ImGuiCol_Tab]                 = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
  colors[ImGuiCol_TabHovered]          = panelHi;
  colors[ImGuiCol_TabActive]           = panel;
  colors[ImGuiCol_TabUnfocused]        = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
  colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);

  colors[ImGuiCol_DockingPreview]      = ImVec4(accent.x, accent.y, accent.z, 0.60f);
  colors[ImGuiCol_DockingEmptyBg]      = bg;

  ImGui_ImplSDL2_InitForOpenGL(m_Window, m_Context);
  ImGui_ImplOpenGL3_Init("#version 130");
}

void Window::Shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(m_Context);
  SDL_DestroyWindow(m_Window);
  SDL_Quit();
}

void Window::OnResize(int width, int height) {
  m_Data.Width = width;
  m_Data.Height = height;
}

std::pair<int, int> Window::GetDrawableSize() const {
  int w, h;
  SDL_GL_GetDrawableSize(m_Window, &w, &h);
  return {w, h};
}

void Window::OnUpdate() {
  SDL_GL_SwapWindow(m_Window);
}
}
