#pragma once

#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include <functional>
#include <memory>
#include <set>

namespace Engine {

class SceneHierarchyPanel {
public:
  SceneHierarchyPanel() = default;
  SceneHierarchyPanel(const std::shared_ptr<Scene> &scene);

  void SetContext(const std::shared_ptr<Scene> &scene);
  void OnImGuiRender();

  void SetHistoryCallback(const std::function<void()> &callback) {
    m_OnHistorySaveCallback = callback;
  }

  Entity GetSelectedEntity() const;
  const std::set<int> &GetSelectedEntities() const {
    return m_SelectedEntities;
  }

  void SetSelectedEntity(Entity entity) {
    m_SelectedEntities.clear();
    if (entity.GetID() != -1)
      m_SelectedEntities.insert(entity.GetID());
  }

  void AddSelectedEntity(int id) { m_SelectedEntities.insert(id); }
  void ClearSelection() { m_SelectedEntities.clear(); }
  bool IsSelected(int id) const {
    return m_SelectedEntities.find(id) != m_SelectedEntities.end();
  }

private:
  void DrawEntityNode(Entity entity, bool &outDeleted, bool &outHovered);
  void DrawComponents(Entity *entity);

private:
  std::shared_ptr<Scene> m_Context;
  std::set<int> m_SelectedEntities;
  std::function<void()> m_OnHistorySaveCallback;
};

}
