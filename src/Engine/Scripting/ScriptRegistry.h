#pragma once

#include "../Scene/Components.h"
#include <functional>
#include <map>
#include <string>

namespace Engine {

class ScriptRegistry {
public:
  using ScriptInstantiator = std::function<ScriptableEntity *()>;

  static void Register(const std::string &name,
                       ScriptInstantiator instantiator) {
    GetRegistry()[name] = instantiator;
  }

  static ScriptableEntity *Create(const std::string &name) {
    if (GetRegistry().find(name) != GetRegistry().end()) {
      return GetRegistry()[name]();
    }
    return nullptr;
  }

  static const std::map<std::string, ScriptInstantiator> &GetScripts() {
    return GetRegistry();
  }

private:
  static std::map<std::string, ScriptInstantiator> &GetRegistry() {
    static std::map<std::string, ScriptInstantiator> registry;
    return registry;
  }
};

}
