#pragma once

#include "Scene.h"
#include <memory>
#include <string>

namespace Engine {

class SceneSerializer {
public:
  SceneSerializer(const std::shared_ptr<Scene> &scene);

  void Serialize(const std::string &filepath);
  bool Deserialize(const std::string &filepath);

  std::string SerializeToString();
  bool DeserializeFromString(const std::string &data);

private:
  std::shared_ptr<Scene> m_Scene;
};

}
