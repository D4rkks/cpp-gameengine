#pragma once

#include "../Engine/Core/Application.h"
#include "../Engine/Core/Input.h"
#include "../Engine/Scene/Components.h"
#include <imgui.h>
#include <iostream>

namespace Engine {

class PauseMenu : public ScriptableEntity {
public:
  void OnCreate() override {
    m_Show = false;
    Application::Get().SetMouseLocked(true);
  }

  void OnDestroy() override {}

  void OnUpdate(float ts) override {
    bool pressed = Input::IsKeyPressed(SDL_SCANCODE_P) ||
                   Input::IsKeyPressed(SDL_SCANCODE_ESCAPE);

    if (pressed && !m_KeyProcessed) {
      m_Show = !m_Show;

      Application::Get().SetMouseLocked(!m_Show);
      m_KeyProcessed = true;
    }

    if (!pressed) {
      m_KeyProcessed = false;
    }
  }

  void OnImGuiRender() override {
    if (!m_Show)
      return;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                   ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Pause Menu", &m_Show,
                 ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("GAME PAUSED");
    ImGui::Separator();

    if (ImGui::Button("Resume Game", ImVec2(200, 50))) {
      m_Show = false;
      Application::Get().SetMouseLocked(true);
    }

    if (ImGui::Button("Quit to Desktop", ImVec2(200, 50))) {
      std::cout << "Quit Pressed!" << std::endl;
    }

    ImGui::End();
  }

private:
  bool m_Show = false;
  bool m_KeyProcessed = false;
};

}
