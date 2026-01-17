#pragma once

#include "../Engine/Core/Application.h"
#include "../Engine/Scene/Components.h"
#include <imgui.h>
#include <iostream>

namespace Engine {

class MainMenu : public ScriptableEntity {
public:
  void OnCreate() override {
    Application::Get().SetMouseLocked(false);
  }

  void OnDestroy() override {}

  void OnUpdate(float ts) override {}

  void OnImGuiRender() override {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 400));

    ImGui::Begin("Main Menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize);

    float windowWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize("MY ENGINE GAME").x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("MY ENGINE GAME");

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 20.0f));

    PlayButton();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    SettingsButton();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    QuitButton();

    ImGui::End();
  }

private:
  void PlayButton() {
    float windowWidth = ImGui::GetWindowSize().x;
    if (ImGui::Button("PLAY", ImVec2(windowWidth - 20, 50))) {
      //not good
      std::cout << "[MainMenu] Play Pressed!" << std::endl;
    }
  }

  void SettingsButton() {
    float windowWidth = ImGui::GetWindowSize().x;
    if (ImGui::Button("SETTINGS", ImVec2(windowWidth - 20, 50))) {
      std::cout << "[MainMenu] Settings Pressed!" << std::endl;
      m_ShowSettings = !m_ShowSettings;
    }

    if (m_ShowSettings) {
      ImGui::OpenPopup("SettingsPopup");
    }

    if (ImGui::BeginPopup("SettingsPopup")) {
      ImGui::Text("Volume");
      ImGui::SliderFloat("##vol", &m_Volume, 0.0f, 1.0f);
      if (ImGui::Button("Close")) {
        m_ShowSettings = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  void QuitButton() {
    float windowWidth = ImGui::GetWindowSize().x;
    if (ImGui::Button("QUIT", ImVec2(windowWidth - 20, 50))) {
      std::cout << "[MainMenu] Quit Pressed!" << std::endl;
    }
  }

  bool m_ShowSettings = false;
  float m_Volume = 1.0f;
};

}
