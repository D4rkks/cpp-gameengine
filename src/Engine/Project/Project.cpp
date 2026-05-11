#include "Project.h"

namespace Engine {

const SceneEntry* Project::FindScene(const std::string& name) const {
    for (const auto& e : Scenes) {
        if (e.Name == name) return &e;
    }
    return nullptr;
}

}
