#pragma once

#include "../Core/EngineAPI.h"

namespace Engine {

class Scene;

class ENGINE_API SceneSystem {
public:
  virtual ~SceneSystem() = default;
  virtual void OnRuntimeUpdate(Scene &scene, float ts) {}
  virtual void OnEditorUpdate(Scene &scene, float ts) {}
};

}
