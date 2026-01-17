#include "SceneHierarchyPanel.h"
#include "../../Scripts/FloatingItem.h"
#include "../Renderer/Texture.h"
#include "../Scripting/ScriptRegistry.h"
#include "../Scripts/PlayerController.h"
#include "../Utils/PlatformUtils.h"

#include <algorithm>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace Engine {

SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene> &scene) {
  SetContext(scene);
}

void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene> &scene) {
  m_Context = scene;
  m_SelectedEntities.clear();
}

Entity SceneHierarchyPanel::GetSelectedEntity() const {
  if (m_SelectedEntities.empty() || !m_Context) {
    return Entity();
  }

  int id = *m_SelectedEntities.begin();
  for (const auto &e : m_Context->GetEntities()) {
    if (e.GetID() == id) {
      return e;
    }
  }

  return Entity();
}

void SceneHierarchyPanel::OnImGuiRender() {
  /*
  ImGui::Begin("Scene Settings");
  if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (m_Context) { // Ensure m_Context is valid before accessing scene
                     // properties
      glm::vec3 gravity = m_Context->GetGravity();
      if (ImGui::DragFloat3("Gravity", glm::value_ptr(gravity), 0.1f)) {
        m_Context->SetGravity(gravity);
      }
    }
  }
  ImGui::End();
  */

  ImGui::Begin("Debug Control");
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(0, 100), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(250, 500), ImGuiCond_FirstUseEver);
  ImGui::Begin("Scene Hierarchy");

  if (m_Context) {
    int entityToDelete = -1;
    bool anyItemHovered = false;

    for (const auto &entity : m_Context->GetEntities()) {
      if (entity.HasRelationship && entity.Relationship.Parent != 0) {
        Entity parent = m_Context->GetEntityByUUID(entity.Relationship.Parent);
        if (parent.GetID() != -1) {
          continue;
        }
      }

      bool deleted = false;
      DrawEntityNode(entity, deleted, anyItemHovered);
      if (deleted)
        entityToDelete = entity.GetID();
    }

    if (entityToDelete != -1) {
      if (m_OnHistorySaveCallback)
        m_OnHistorySaveCallback();
      m_Context->DestroyEntity(Entity(entityToDelete));
      m_SelectedEntities.erase(entityToDelete);
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
      m_SelectedEntities.clear();

    if (!anyItemHovered && ImGui::BeginPopupContextWindow(0, 1)) {
      if (ImGui::MenuItem("Create Empty Entity")) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        m_Context->CreateEntity("Empty Entity");
      }
      if (ImGui::MenuItem("Create Camera")) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        Entity entity = m_Context->CreateEntity("Camera");
        entity.HasCamera = true;
        entity.Camera.Primary = true;
        entity.HasSpriteRenderer = false;
        m_Context->UpdateEntity(entity);
      }
      if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas")) {
          auto e = m_Context->CreateEntity("UI Canvas");
          e.HasCanvas = true;
          e.Canvas.IsScreenSpace = true;
          e.HasRectTransform = true;
          e.HasSpriteRenderer = false;
          e.RectTransform.SizeDelta = {1280.0f, 720.0f};
          m_Context->UpdateEntity(e);
        }
        if (ImGui::MenuItem("Image")) {
          auto e = m_Context->CreateEntity("UI Image");
          e.HasImage = true;
          e.HasRectTransform = true;
          e.HasSpriteRenderer = false;
          m_Context->UpdateEntity(e);
        }
        if (ImGui::MenuItem("Button")) {
          auto e = m_Context->CreateEntity("UI Button");
          e.HasButton = true;
          e.HasImage = true;
          e.HasRectTransform = true;
          e.HasText = true;
          e.Text.TextString = "Button";
          e.HasSpriteRenderer = false;
          e.RectTransform.SizeDelta = {160.0f, 40.0f};
          m_Context->UpdateEntity(e);
        }
        if (ImGui::MenuItem("Text")) {
          auto e = m_Context->CreateEntity("UI Text");
          e.HasText = true;
          e.HasRectTransform = true;
          e.HasSpriteRenderer = false;
          e.Text.TextString = "New Text";
          e.RectTransform.SizeDelta = {200.0f, 50.0f};
          m_Context->UpdateEntity(e);
        }
        ImGui::EndMenu();
      }
      ImGui::EndPopup();
    }
  }

  ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 30.0f));
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ITEM")) {
      int droppedID = *(const int *)payload->Data;
      Entity droppedEntity;
      bool found = false;
      for (auto &e : m_Context->GetEntities()) {
        if (e.GetID() == droppedID) {
          droppedEntity = e;
          found = true;
          break;
        }
      }

      if (found) {
        if (droppedEntity.HasRelationship &&
            droppedEntity.Relationship.Parent != 0) {
          Entity oldParent =
              m_Context->GetEntityByUUID(droppedEntity.Relationship.Parent);
          if (oldParent.GetID() != -1) {
            auto &children = oldParent.Relationship.Children;
            auto it = std::find(children.begin(), children.end(),
                                droppedEntity.GetUUID());
            if (it != children.end()) {
              children.erase(it);
              m_Context->UpdateEntity(oldParent);
            }
          }
        }
        droppedEntity.HasRelationship = true;
        droppedEntity.Relationship.Parent = 0;
        m_Context->UpdateEntity(droppedEntity);
      }
    }
    ImGui::EndDragDropTarget();
  }

  ImGui::End();

  ImGui::Begin("Inspector");
  if (!m_SelectedEntities.empty() && m_Context) {
    Entity *selectedEntity = nullptr;
    int firstID = *m_SelectedEntities.begin();
    for (auto &e : m_Context->GetEntities()) {
      if (e.GetID() == firstID) {
        selectedEntity = &e;
        break;
      }
    }

    if (selectedEntity) {
      DrawComponents(selectedEntity);
    }
  }
  ImGui::End();
}

void SceneHierarchyPanel::DrawEntityNode(Entity entity, bool &outDeleted,
                                         bool &outHovered) {
  std::string tag =
      entity.Name + " (ID: " + std::to_string(entity.GetID()) + ")";

  ImGuiTreeNodeFlags flags =
      (IsSelected(entity.GetID()) ? ImGuiTreeNodeFlags_Selected : 0) |
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

  bool hasChildren =
      entity.HasRelationship && !entity.Relationship.Children.empty();
  if (!hasChildren)
    flags |= ImGuiTreeNodeFlags_Leaf;

  bool opened = ImGui::TreeNodeEx((void *)(uint64_t)(uint32_t)entity.GetID(),
                                  flags, tag.c_str());
  if (ImGui::IsItemClicked()) {
    bool controlDown = ImGui::GetIO().KeyCtrl;
    if (controlDown) {
      if (IsSelected(entity.GetID()))
        m_SelectedEntities.erase(entity.GetID());
      else
        m_SelectedEntities.insert(entity.GetID());
    } else {
      m_SelectedEntities.clear();
      m_SelectedEntities.insert(entity.GetID());
    }
  }
  if (ImGui::IsItemHovered()) {
    outHovered = true;
  }

  if (ImGui::BeginDragDropSource()) {
    int id = entity.GetID();
    ImGui::SetDragDropPayload("SCENE_HIERARCHY_ITEM", &id, sizeof(int));
    ImGui::Text("%s", tag.c_str());
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ITEM")) {
      int droppedID = *(const int *)payload->Data;

      if (droppedID != entity.GetID()) {
        Entity droppedEntity;
        bool foundDropped = false;
        for (auto &e : m_Context->GetEntities()) {
          if (e.GetID() == droppedID) {
            droppedEntity = e;
            foundDropped = true;
            break;
          }
        }

        if (foundDropped) {
          if (droppedEntity.HasRelationship &&
              droppedEntity.Relationship.Parent != 0) {
            Entity oldParent =
                m_Context->GetEntityByUUID(droppedEntity.Relationship.Parent);
            if (oldParent.GetID() != -1) {
              auto &children = oldParent.Relationship.Children;
              auto it = std::find(children.begin(), children.end(),
                                  droppedEntity.GetUUID());
              if (it != children.end()) {
                children.erase(it);
                m_Context->UpdateEntity(oldParent);
              }
            }
          }

          droppedEntity.HasRelationship = true;
          droppedEntity.Relationship.Parent = entity.GetUUID();

          m_Context->UpdateEntity(droppedEntity);

          entity.HasRelationship = true;
          entity.Relationship.Children.push_back(droppedEntity.GetUUID());
          m_Context->UpdateEntity(entity);
        }
      }
    }

    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
      const char *path = (const char *)payload->Data;
      std::filesystem::path filepath = std::filesystem::path(path);

      if (filepath.extension() == ".h") {
        std::string filename = filepath.filename().string();
        if (filename == "FloatingItem.h") {
          if (!entity.HasScript) {
            entity.AddScript<FloatingItem>("FloatingItem");
            m_Context->UpdateEntity(entity);
          }
        } else if (filename == "PlayerController.h") {
          if (!entity.HasScript) {
            entity.AddScript<PlayerController>("PlayerController");
            m_Context->UpdateEntity(entity);
          }
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::MenuItem("Delete Entity"))
      outDeleted = true;

    if (ImGui::BeginMenu("Create Child")) {
      if (ImGui::MenuItem("Empty Entity")) {
        auto child = m_Context->CreateEntity("Child");
        child.HasRelationship = true;
        child.Relationship.Parent = entity.GetUUID();

        Entity parent = m_Context->GetEntityByUUID(entity.GetUUID());
        parent.Relationship.Children.push_back(child.GetUUID());
        parent.HasRelationship = true;
        m_Context->UpdateEntity(parent);
        m_Context->UpdateEntity(child);
      }

      if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas")) {
          auto child = m_Context->CreateEntity("UI Canvas");
          child.HasRelationship = true;
          child.Relationship.Parent = entity.GetUUID();
          child.HasCanvas = true;
          child.Canvas.IsScreenSpace = true;
          child.HasRectTransform = true;
          child.HasSpriteRenderer = false;
          child.RectTransform.SizeDelta = {1280.0f, 720.0f};

          Entity parent = m_Context->GetEntityByUUID(entity.GetUUID());
          parent.Relationship.Children.push_back(child.GetUUID());
          parent.HasRelationship = true;
          m_Context->UpdateEntity(parent);
          m_Context->UpdateEntity(child);
        }
        if (ImGui::MenuItem("Image")) {
          auto child = m_Context->CreateEntity("UI Image");
          child.HasRelationship = true;
          child.Relationship.Parent = entity.GetUUID();
          child.HasImage = true;
          child.HasRectTransform = true;
          child.HasSpriteRenderer = false;

          Entity parent = m_Context->GetEntityByUUID(entity.GetUUID());
          parent.Relationship.Children.push_back(child.GetUUID());
          parent.HasRelationship = true;
          m_Context->UpdateEntity(parent);
          m_Context->UpdateEntity(child);
        }
        if (ImGui::MenuItem("Button")) {
          auto child = m_Context->CreateEntity("UI Button");
          child.HasRelationship = true;
          child.Relationship.Parent = entity.GetUUID();
          child.HasButton = true;
          child.HasImage = true;
          child.HasRectTransform = true;
          child.HasText = true;
          child.Text.TextString = "Button";
          child.Text.Color = {0, 0, 0, 1};
          child.HasSpriteRenderer = false;
          child.RectTransform.SizeDelta = {160.0f, 40.0f};

          Entity parent = m_Context->GetEntityByUUID(entity.GetUUID());
          parent.Relationship.Children.push_back(child.GetUUID());
          parent.HasRelationship = true;
          m_Context->UpdateEntity(parent);
          m_Context->UpdateEntity(child);
        }
        if (ImGui::MenuItem("Text")) {
          auto child = m_Context->CreateEntity("UI Text");
          child.HasRelationship = true;
          child.Relationship.Parent = entity.GetUUID();
          child.HasText = true;
          child.HasRectTransform = true;
          child.Text.TextString = "New Text";
          child.HasSpriteRenderer = false;
          child.RectTransform.SizeDelta = {200.0f, 50.0f};

          Entity parent = m_Context->GetEntityByUUID(entity.GetUUID());
          parent.Relationship.Children.push_back(child.GetUUID());
          parent.HasRelationship = true;
          m_Context->UpdateEntity(parent);
          m_Context->UpdateEntity(child);
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    ImGui::EndPopup();
  }

  if (opened) {
    if (entity.HasRelationship) {
      for (auto childUUID : entity.Relationship.Children) {
        Entity child = m_Context->GetEntityByUUID(childUUID);
        if (child.GetID() != -1) {
          bool childDeleted = false;
          DrawEntityNode(child, childDeleted, outHovered);
          if (childDeleted) {
            m_Context->DestroyEntity(child);
          }
        }
      }
    }
    ImGui::TreePop();
  }
}

template <typename T, typename UIFunction>
static void DrawComponent(const std::string &name, Entity *entity,
                          bool &hasComponent, T &component,
                          UIFunction uiFunction,
                          const std::function<void()> &historyCallback,
                          const std::function<void()> &updateCallback) {
  const ImGuiTreeNodeFlags treeNodeFlags =
      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
      ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
      ImGuiTreeNodeFlags_FramePadding;

  if (hasComponent) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2{0, 8});
    float lineHeight =
        ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImGui::Spacing(); 
    ImGui::Separator();
    bool open = ImGui::TreeNodeEx((void *)typeid(T).hash_code(), treeNodeFlags,
                                  name.c_str());
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8, 4});

    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Remove Component")) {
        if (historyCallback)
          historyCallback();
        hasComponent = false;
        if (updateCallback)
          updateCallback();
      }
      ImGui::EndPopup();
    }

    if (open) {
      uiFunction(component);
      ImGui::TreePop();
    }
    ImGui::PopStyleVar();
  }
}

static bool DrawVec3Control(const std::string &label, glm::vec3 &values,
                            const std::function<void()> &historyCallback,
                            float resetValue = 0.0f,
                            float columnWidth = 100.0f) {
  bool changed = false;
  ImGui::PushID(label.c_str());

  ImGui::Columns(2);
  ImGui::SetColumnWidth(0, columnWidth);
  ImGui::Text(label.c_str());
  ImGui::NextColumn();

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2, 0});

  float lineHeight =
      ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
  ImVec2 buttonSize = {lineHeight - 2.0f, lineHeight};

  float widthRaw = ImGui::GetContentRegionAvail().x;
  float componentWidth = (widthRaw - (buttonSize.x * 3.0f)) / 3.0f;

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
  if (ImGui::Button("X", buttonSize)) {
    values.x = resetValue;
    changed = true;
  }
  ImGui::PopStyleColor(3);

  ImGui::SameLine();
  ImGui::SetNextItemWidth(componentWidth);
  if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.3f")) {
    changed = true;
  }
  if (ImGui::IsItemActivated() && historyCallback)
    historyCallback();
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.3f, 0.8f, 0.3f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
  if (ImGui::Button("Y", buttonSize)) {
    values.y = resetValue;
    changed = true;
  }
  ImGui::PopStyleColor(3);

  ImGui::SameLine();
  ImGui::SetNextItemWidth(componentWidth);
  if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.3f")) {
    changed = true;
  }
  if (ImGui::IsItemActivated() && historyCallback)
    historyCallback();
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
  if (ImGui::Button("Z", buttonSize)) {
    values.z = resetValue;
    changed = true;
  }
  ImGui::PopStyleColor(3);

  ImGui::SameLine();
  ImGui::SetNextItemWidth(componentWidth);
  if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.3f")) {
    changed = true;
  }
  if (ImGui::IsItemActivated() && historyCallback)
    historyCallback();

  ImGui::PopStyleVar();
  ImGui::Columns(1);

  ImGui::PopID();

  return changed;
}

static bool DrawAnchorPresets(RectTransformComponent &ui) {
  ImGui::PushID("AnchorPresets");
  ImGui::Text("Presets");
  bool changed = false;

  float buttonSize = 25.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

  auto PresetBtn = [&](const char *id, glm::vec2 anchorMin, glm::vec2 anchorMax,
                       glm::vec2 pivot, bool stretch = false) {
    if (ImGui::Button(id, ImVec2(buttonSize, buttonSize))) {
      ui.AnchorsMin = anchorMin;
      ui.AnchorsMax = anchorMax;
      ui.Pivot = pivot;

      ui.Position = {0.0f, 0.0f};

      if (stretch) {

      }
      changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Min:(%.1f,%.1f) Max:(%.1f,%.1f)", anchorMin.x,
                        anchorMin.y, anchorMax.x, anchorMax.y);
    }
  };

  PresetBtn("TL", {0, 0}, {0, 0}, {0, 0});
  ImGui::SameLine();
  PresetBtn("TC", {0.5f, 0}, {0.5f, 0}, {0.5f, 0});
  ImGui::SameLine();
  PresetBtn("TR", {1, 0}, {1, 0}, {1, 0});
  ImGui::SameLine();
  PresetBtn("HSt", {0, 0}, {1, 0}, {0.5f, 0}, true);

  PresetBtn("ML", {0, 0.5f}, {0, 0.5f}, {0, 0.5f});
  ImGui::SameLine();
  PresetBtn("MC", {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f});
  ImGui::SameLine();
  PresetBtn("MR", {1, 0.5f}, {1, 0.5f}, {1, 0.5f});
  ImGui::SameLine();
  PresetBtn("HSm", {0, 0.5f}, {1, 0.5f}, {0.5f, 0.5f}, true);

  PresetBtn("BL", {0, 1}, {0, 1}, {0, 1});
  ImGui::SameLine();
  PresetBtn("BC", {0.5f, 1}, {0.5f, 1}, {0.5f, 1});
  ImGui::SameLine();
  PresetBtn("BR", {1, 1}, {1, 1}, {1, 1});
  ImGui::SameLine();
  PresetBtn("HSb", {0, 1}, {1, 1}, {0.5f, 1}, true);

  ImGui::Dummy(ImVec2(0, 2));

  PresetBtn("VSl", {0, 0}, {0, 1}, {0, 0.5f}, true);
  ImGui::SameLine();
  PresetBtn("VSc", {0.5f, 0}, {0.5f, 1}, {0.5f, 0.5f}, true);
  ImGui::SameLine();
  PresetBtn("VSr", {1, 0}, {1, 1}, {1, 0.5f}, true);
  ImGui::SameLine();
  PresetBtn("All", {0, 0}, {1, 1}, {0.5f, 0.5f}, true);

  ImGui::PopStyleVar();
  ImGui::PopID();
  return changed;
}

void SceneHierarchyPanel::DrawComponents(Entity *entity) {

  if (!entity)
    return;

  if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 80.0f);
    ImGui::Text("Name");
    ImGui::NextColumn();

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, entity->Name.c_str(), sizeof(buffer));
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::InputText("##Name", buffer, sizeof(buffer))) {
      entity->Name = std::string(buffer);
      m_Context->UpdateEntity(*entity);
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemActivated() && m_OnHistorySaveCallback) {
      m_OnHistorySaveCallback();
    }
    ImGui::Columns(1);
    ImGui::PopStyleVar();
  }

  DrawComponent<SpriteRendererComponent>(
      "Sprite Renderer", entity, entity->HasSpriteRenderer,
      entity->SpriteRenderer,
      [&](auto &component) {
        ImGui::PushID("SpriteRenderer");
        bool changed = false;

        ImGui::Text("Texture");
        if (component.Texture) {
          ImGui::Image((void *)(intptr_t)component.Texture->GetID(),
                       ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
          ImGui::SameLine();
          if (ImGui::Button("X", ImVec2(24, 24))) {
            component.Texture = nullptr;
            component.TexturePath = "";
            changed = true;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove Texture");
        } else {
          ImGui::Button("Drop Here", ImVec2(64, 64));
        }

        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload =
                  ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char *path = (const char *)payload->Data;
            component.Texture = std::make_shared<Texture2D>(path);
            component.TexturePath = std::string(path);
            changed = true;
          }
          ImGui::EndDragDropTarget();
        }


        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Tiling Factor");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat("##Tiling Factor", &component.TilingFactor, 0.1f,
                             0.0f, 100.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Color");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::ColorEdit4("##Color", &component.Color.r))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();

        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<CameraComponent>(
      "Camera", entity, entity->HasCamera, entity->Camera,
      [&](auto &component) {
        ImGui::PushID("Camera");
        bool changed = false;

        const char *projectionTypeStrings[] = {"Perspective", "Orthographic"};
        const char *currentProjectionTypeString =
            projectionTypeStrings[(int)component.Type];
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Projection");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::BeginCombo("##Projection", currentProjectionTypeString)) {
          for (int i = 0; i < 2; i++) {
            bool isSelected =
                currentProjectionTypeString == projectionTypeStrings[i];
            if (ImGui::Selectable(projectionTypeStrings[i], isSelected)) {
              if (m_OnHistorySaveCallback)
                m_OnHistorySaveCallback();
              currentProjectionTypeString = projectionTypeStrings[i];
              component.Type = (CameraComponent::ProjectionType)i;
              changed = true;
            }
            if (isSelected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        if (component.Type == CameraComponent::ProjectionType::Perspective) {
          ImGui::Columns(2);
          ImGui::SetColumnWidth(0, 100.0f);

          ImGui::Text("Vertical FOV");
          ImGui::NextColumn();
          ImGui::PushItemWidth(-1.0f);
          if (ImGui::DragFloat("##Vertical FOV", &component.PerspectiveFOV))
            changed = true;
          ImGui::PopItemWidth();
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          ImGui::NextColumn();

          ImGui::Text("Near");
          ImGui::NextColumn();
          ImGui::PushItemWidth(-1.0f);
          if (ImGui::DragFloat("##Near", &component.PerspectiveNear))
            changed = true;
          ImGui::PopItemWidth();
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          ImGui::NextColumn();

          ImGui::Text("Far");
          ImGui::NextColumn();
          ImGui::PushItemWidth(-1.0f);
          if (ImGui::DragFloat("##Far", &component.PerspectiveFar))
            changed = true;
          ImGui::PopItemWidth();
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();

          ImGui::Columns(1);
        }

        if (component.Type == CameraComponent::ProjectionType::Orthographic) {
          if (ImGui::DragFloat("Size", &component.OrthographicSize))
            changed = true;
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          if (ImGui::DragFloat("Near", &component.OrthographicNear))
            changed = true;
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          if (ImGui::DragFloat("Far", &component.OrthographicFar))
            changed = true;
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
        }

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Primary");
        ImGui::NextColumn();
        if (ImGui::Checkbox("##Primary", &component.Primary)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          changed = true;
        }
        ImGui::Columns(1);

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Anti-Aliasing");
        ImGui::NextColumn();
        if (ImGui::Checkbox("##AA", &component.AntiAliasing)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          changed = true;
        }
        ImGui::NextColumn();
        ImGui::Text("Frustum Culling");
        ImGui::NextColumn();
        if (ImGui::Checkbox("##Culling", &component.FrustumCulling)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          changed = true;
        }
        ImGui::Columns(1);

        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<RigidBodyComponent>(
      "Rigid Body", entity, entity->HasRigidBody, entity->RigidBody,
      [&](auto &component) {
        ImGui::PushID("RigidBody");
        bool changed = false;

        int bodyType = (int)component.Type;
        const char *bodyTypeStrings[] = {"Static", "Dynamic", "Kinematic"};
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);

        ImGui::Text("Body Type");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::Combo("##Body Type", &bodyType, bodyTypeStrings, 3)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          component.Type = (RigidBodyComponent::BodyType)bodyType;
          changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        if (ImGui::DragFloat("Mass", &component.Mass, 0.1f, 0.0f, 100.0f))
          changed = true;
        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        if (ImGui::DragFloat("Friction", &component.Friction, 0.1f, 0.0f, 1.0f))
          changed = true;
        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        if (ImGui::DragFloat("Restitution", &component.Restitution, 0.1f, 0.0f,
                             1.0f))
          changed = true;
        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();

        if (ImGui::Checkbox("Fixed Rotation", &component.FixedRotation)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          changed = true;
        }

        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<BoxColliderComponent>(
      "Box Collider", entity, entity->HasBoxCollider, entity->BoxCollider,
      [&](auto &component) {
        ImGui::PushID("BoxCollider");
        bool changed = false;
        changed |= DrawVec3Control("Offset", component.Offset,
                                   m_OnHistorySaveCallback, 0.0f);
        changed |= DrawVec3Control("Size", component.Size,
                                   m_OnHistorySaveCallback, 0.5f);
        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<MeshRendererComponent>(
      "Mesh Renderer", entity, entity->HasMeshRenderer, entity->MeshRenderer,
      [&](auto &component) {
        ImGui::PushID("Mesh Renderer");
        bool changed = false;
        if (entity->HasSpriteRenderer && entity->HasMeshRenderer) {
          ImGui::PushStyleColor(ImGuiCol_Text, {1, 1, 0, 1});
          ImGui::TextWrapped("Warning: Both Sprite and Mesh renders are "
                             "active. The cube is likely blocking your mesh.");
          ImGui::PopStyleColor();
        }

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Color");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::ColorEdit4("##Color", glm::value_ptr(component.Color)))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        std::string label =
            component.FilePath.empty()
                ? "Drag .obj here"
                : std::filesystem::path(component.FilePath).filename().string();
        ImGui::Button(label.c_str(), ImVec2(0, 0));

        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload =
                  ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char *path = (const char *)payload->Data;
            std::filesystem::path meshPath(path);
            if (meshPath.extension() == ".obj") {
              std::string p = meshPath.string();
              component.FilePath = p;
              component.Mesh = std::make_shared<Mesh>(p);
              m_Context->UpdateEntity(*entity);
            }
          }
          ImGui::EndDragDropTarget();
        }

        ImGui::Text("Diffuse Map");
        std::string diffuseLabel =
            component.DiffusePath.empty()
                ? "Drag Texture here"
                : std::filesystem::path(component.DiffusePath)
                      .filename()
                      .string();
        ImGui::Button(diffuseLabel.c_str(), ImVec2(0, 0));
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload =
                  ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char *path = (const char *)payload->Data;
            std::filesystem::path texPath(path);
            std::string ext = texPath.extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".tga") {
              component.DiffusePath = texPath.string();
              component.DiffuseMap =
                  std::make_shared<Texture2D>(component.DiffusePath);
              m_Context->UpdateEntity(*entity);
            }
          }
          ImGui::EndDragDropTarget();
        }

        ImGui::Text("Normal Map");
        std::string normalLabel =
            component.NormalPath.empty()
                ? "Drag Texture here"
                : std::filesystem::path(component.NormalPath)
                      .filename()
                      .string();
        ImGui::Button(normalLabel.c_str(), ImVec2(0, 0));
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload =
                  ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char *path = (const char *)payload->Data;
            std::filesystem::path texPath(path);
            std::string ext = texPath.extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".tga") {
              component.NormalPath = texPath.string();
              component.NormalMap =
                  std::make_shared<Texture2D>(component.NormalPath);
              m_Context->UpdateEntity(*entity);
            }
          }
          ImGui::EndDragDropTarget();
        }

        ImGui::Separator();
        ImGui::Text("Level of Detail (LOD)");
        if (ImGui::Button("Add LOD")) {
          component.LODs.push_back({nullptr, 100.0f});
          m_Context->UpdateEntity(*entity);
        }

        for (size_t i = 0; i < component.LODs.size(); i++) {
          ImGui::PushID((int)i);
          std::string lodLabel =
              component.LODs[i].Mesh
                  ? std::filesystem::path(component.LODs[i].Mesh->GetFilePath())
                        .filename()
                        .string()
                  : "Drag .obj here";
          ImGui::Button(lodLabel.c_str(), ImVec2(100, 0));
          if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
              const char *path = (const char *)payload->Data;
              std::filesystem::path meshPath(path);
              if (meshPath.extension() == ".obj") {
                component.LODs[i].Mesh =
                    std::make_shared<Mesh>(meshPath.string());
                m_Context->UpdateEntity(*entity);
              }
            }
            ImGui::EndDragDropTarget();
          }
          ImGui::SameLine();
          ImGui::PushItemWidth(60);
          if (ImGui::DragFloat("##Dist", &component.LODs[i].Distance, 1.0f,
                               0.0f, 1000.0f)) {
            m_Context->UpdateEntity(*entity);
          }
          ImGui::PopItemWidth();
          ImGui::SameLine();
          if (ImGui::Button("X")) {
            component.LODs.erase(component.LODs.begin() + i);
            m_Context->UpdateEntity(*entity);
            ImGui::PopID();
            break;
          }
          ImGui::PopID();
        }

        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<LightComponent>(
      "Light", entity, entity->HasLight, entity->Light,
      [&](auto &component) {
        ImGui::PushID("Light");
        bool changed = false;

        int lightType = (int)component.Type;
        const char *lightTypeStrings[] = {"Directional", "Point"};
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);

        ImGui::Text("Light Type");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::Combo("##Light Type", &lightType, lightTypeStrings, 2)) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          component.Type = (LightComponent::LightType)lightType;
          changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        if (ImGui::ColorEdit3("Color", glm::value_ptr(component.Color)))
          changed = true;
        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();

        if (ImGui::DragFloat("Intensity", &component.Intensity, 0.1f, 0.0f,
                             100.0f))
          changed = true;
        if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();

        if (component.Type == LightComponent::LightType::Point) {
          if (ImGui::DragFloat("Radius", &component.Radius, 0.5f, 0.0f,
                               1000.0f))
            changed = true;
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
        }

        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<SkyboxComponent>(
      "Skybox", entity, entity->HasSkybox, entity->Skybox,
      [&](auto &component) {
        ImGui::PushID("Skybox");
        bool changed = false;
        const char *faceNames[] = {"Right (+X)",  "Left (-X)", "Top (+Y)",
                                   "Bottom (-Y)", "Back (+Z)", "Front (-Z)"};
        for (int i = 0; i < 6; i++) {
          ImGui::PushID(i);
          ImGui::Columns(2);
          ImGui::SetColumnWidth(0, 100.0f);
          ImGui::Text(faceNames[i]);
          ImGui::NextColumn();

          char buffer[256];
          memset(buffer, 0, sizeof(buffer));
          strncpy(buffer, component.FacePaths[i].c_str(), sizeof(buffer));

          float buttonSize = 24.0f;
          ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - buttonSize -
                               4.0f);
          if (ImGui::InputText("##Path", buffer, sizeof(buffer))) {
            component.FacePaths[i] = buffer;
            changed = true;
          }
          ImGui::PopItemWidth();
          if (ImGui::IsItemActivated() && m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();

          ImGui::SameLine();
          if (ImGui::Button("...", ImVec2(buttonSize, 0))) {
          }
          ImGui::PopID();
          ImGui::Columns(1);
        }
        if (changed)
          m_Context->UpdateEntity(*entity);
        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<AudioSourceComponent>(
      "Audio Source", entity, entity->HasAudioSource, entity->AudioSource,
      [&](auto &component) {
        ImGui::Text("Audio Clip");
        ImGui::SameLine();
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, component.FilePath.c_str(), sizeof(buffer));
        if (ImGui::InputText("##AudioClip", buffer, sizeof(buffer))) {
          component.FilePath = buffer;
          m_Context->UpdateEntity(*entity);
        }
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload =
                  ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char *path = (const char *)payload->Data;
            std::filesystem::path filepath = std::filesystem::path(path);
            if (filepath.extension() == ".wav") {
              component.FilePath = filepath.string();
              m_Context->UpdateEntity(*entity);
            }
          }
          ImGui::EndDragDropTarget();
        }

        // Settings
        if (ImGui::DragFloat("Volume", &component.Volume, 0.01f, 0.0f, 1.0f))
          m_Context->UpdateEntity(*entity);
        if (ImGui::DragFloat("Pitch", &component.Pitch, 0.01f, 0.1f, 10.0f))
          m_Context->UpdateEntity(*entity);
        if (ImGui::DragFloat("Range", &component.Range, 1.0f, 1.0f, 2000.0f))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Loop", &component.Loop))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Play On Awake", &component.PlayOnAwake))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Spatial (3D)", &component.Spatial))
          m_Context->UpdateEntity(*entity);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<NativeScriptComponent>(
      "Native Script", entity, entity->HasScript, entity->m_NativeScript,
      [&](auto &component) {
        ImGui::Text("Script: %s", component.ScriptName.c_str());
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  // UI Components
  DrawComponent<RectTransformComponent>(
      "Rect Transform", entity, entity->HasRectTransform, entity->RectTransform,
      [&](auto &component) {
        ImGui::DragFloat2("Anchors Min", glm::value_ptr(component.AnchorsMin),
                          0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Anchors Max", glm::value_ptr(component.AnchorsMax),
                          0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Pivot", glm::value_ptr(component.Pivot), 0.01f, 0.0f,
                          1.0f);
        ImGui::DragFloat2("Position", glm::value_ptr(component.Position), 1.0f);
        ImGui::DragFloat2("Size Delta", glm::value_ptr(component.SizeDelta),
                          1.0f);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<CanvasComponent>(
      "Canvas", entity, entity->HasCanvas, entity->Canvas,
      [&](auto &component) {
        if (ImGui::Checkbox("Enabled", &component.Enabled))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Is Screen Space", &component.IsScreenSpace))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Pixel Perfect", &component.IsPixelPerfect))
          m_Context->UpdateEntity(*entity);
        ImGui::DragInt("Sorting Order", &component.SortingOrder);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<ImageComponent>(
      "Image", entity, entity->HasImage, entity->Image,
      [&](auto &component) {
        if (ImGui::ColorEdit4("Color", glm::value_ptr(component.Color)))
          m_Context->UpdateEntity(*entity);
        if (ImGui::Checkbox("Visible", &component.Visible))
          m_Context->UpdateEntity(*entity);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<ButtonComponent>(
      "Button", entity, entity->HasButton, entity->Button,
      [&](auto &component) {
        bool changed = false;
        changed |= ImGui::ColorEdit4("Normal Color",
                                     glm::value_ptr(component.NormalColor));
        changed |= ImGui::ColorEdit4("Hover Color",
                                     glm::value_ptr(component.HoverColor));
        changed |= ImGui::ColorEdit4("Clicked Color",
                                     glm::value_ptr(component.ClickedColor));
        if (changed)
          m_Context->UpdateEntity(*entity);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  DrawComponent<TextComponent>(
      "Text", entity, entity->HasText, entity->Text,
      [&](auto &component) {
        bool changed = false;
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, component.TextString.c_str(), sizeof(buffer));
        if (ImGui::InputText("Text", buffer, sizeof(buffer))) {
          component.TextString = buffer;
          changed = true;
        }
        changed |= ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
        ImGui::DragFloat("Font Size", &component.FontSize, 0.5f);
        if (changed)
          m_Context->UpdateEntity(*entity);
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  float buttonWidth = ImGui::GetContentRegionAvail().x;
  if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
    ImGui::OpenPopup("AddComponent");
  }

  if (ImGui::BeginPopup("AddComponent")) {
    if (ImGui::MenuItem("Camera")) {
      if (!entity->HasCamera) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasCamera = true;
        entity->Camera = CameraComponent();
        entity->HasSpriteRenderer = false;
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::MenuItem("Rigid Body")) {
      if (!entity->HasRigidBody) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasRigidBody = true;
        entity->RigidBody = RigidBodyComponent();
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::MenuItem("Audio Source")) {
      if (!entity->HasAudioSource) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasAudioSource = true;
        entity->AudioSource = AudioSourceComponent();
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::MenuItem("Box Collider")) {
      if (!entity->HasBoxCollider) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasBoxCollider = true;
        entity->BoxCollider = BoxColliderComponent();

        // Auto-Size based on existing components
        if (entity->HasMeshRenderer && entity->MeshRenderer.Mesh) {
          entity->BoxCollider.Size =
              entity->MeshRenderer.Mesh->GetLocalDimensions();
        } else if (entity->HasSpriteRenderer) {
          entity->BoxCollider.Size = {1.0f, 1.0f, 0.1f};
        } else {
          entity->BoxCollider.Size = {1.0f, 1.0f, 1.0f};
        }

        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::MenuItem("Mesh Renderer")) {
      if (!entity->HasMeshRenderer) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasMeshRenderer = true;
        entity->MeshRenderer = MeshRendererComponent();
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::BeginMenu("Scripts")) {
      for (const auto &[name, instantiator] : ScriptRegistry::GetScripts()) {
        if (ImGui::MenuItem(name.c_str())) {
          if (!entity->HasScript) {
            if (m_OnHistorySaveCallback)
              m_OnHistorySaveCallback();
            auto script = instantiator();
            entity->m_NativeScript.Instance = script;
            entity->m_NativeScript.DestroyScript =
                [](NativeScriptComponent *nsc) {
                  delete nsc->Instance;
                  nsc->Instance = nullptr;
                };
            entity->m_NativeScript.ScriptName = name;
            script->m_Entity = entity;
            script->OnCreate();
            entity->HasScript = true;
            m_Context->UpdateEntity(*entity);
          }
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Light")) {
      if (!entity->HasLight) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasLight = true;
        entity->Light = LightComponent();
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::MenuItem("Skybox")) {
      if (!entity->HasSkybox) {
        if (m_OnHistorySaveCallback)
          m_OnHistorySaveCallback();
        entity->HasSkybox = true;
        entity->Skybox = SkyboxComponent();
        m_Context->UpdateEntity(*entity);
      }
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::BeginMenu("UI")) {
      if (ImGui::MenuItem("Canvas")) {
        if (!entity->HasCanvas) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          entity->HasCanvas = true;
          entity->Canvas.IsScreenSpace = true;
          entity->HasRectTransform = true;
          m_Context->UpdateEntity(*entity);
        }
      }
      if (ImGui::MenuItem("Image")) {
        if (!entity->HasImage) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          entity->HasImage = true;
          entity->HasRectTransform = true;
          m_Context->UpdateEntity(*entity);
        }
      }
      if (ImGui::MenuItem("Button")) {
        if (!entity->HasButton) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          entity->HasButton = true;
          entity->HasImage = true; // Button usually needs image
          entity->HasRectTransform = true;
          m_Context->UpdateEntity(*entity);
        }
      }
      if (ImGui::MenuItem("Text")) {
        if (!entity->HasText) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          entity->HasText = true;
          entity->Text.TextString = "New Text";
          entity->HasRectTransform = true;
          m_Context->UpdateEntity(*entity);
        }
      }
      if (ImGui::MenuItem("Rect Transform")) {
        if (!entity->HasRectTransform) {
          if (m_OnHistorySaveCallback)
            m_OnHistorySaveCallback();
          entity->HasRectTransform = true;
          m_Context->UpdateEntity(*entity);
        }
      }
      ImGui::EndMenu();
    }

    ImGui::EndPopup();
  }
  DrawComponent<RectTransformComponent>(
      "Rect Transform (Anchored)", entity, entity->HasRectTransform,
      entity->RectTransform,
      [&](auto &component) {
        ImGui::PushID("RectTransform");
        bool changed = false;

        // Draw Presets
        if (DrawAnchorPresets(component))
          changed = true;

        ImGui::Separator();

        // Anchors
        ImGui::Text("Anchors");
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Min");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat2("##AnchorMin",
                              glm::value_ptr(component.AnchorsMin), 0.01f, 0.0f,
                              1.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::Text("Max");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat2("##AnchorMax",
                              glm::value_ptr(component.AnchorsMax), 0.01f, 0.0f,
                              1.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        ImGui::Separator();

        // Pivot
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Pivot");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat2("##Pivot", glm::value_ptr(component.Pivot), 0.01f,
                              0.0f, 1.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        ImGui::Separator();

        // Position & Size
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Position");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat2("##Position", glm::value_ptr(component.Position),
                              1.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::Text("Size Delta");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::DragFloat2("##SizeDelta",
                              glm::value_ptr(component.SizeDelta), 1.0f))
          changed = true;
        ImGui::PopItemWidth();
        ImGui::Columns(1);

        if (changed)
          m_Context->UpdateEntity(*entity);

        ImGui::PopID();
      },
      m_OnHistorySaveCallback, [&]() { m_Context->UpdateEntity(*entity); });
}

}
