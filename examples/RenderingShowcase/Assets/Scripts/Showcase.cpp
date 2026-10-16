#include "Engine/Core/Application.h"
#include "Engine/Renderer/RenderSettings.h"
#include "Engine/Scene/Components.h"
#include "Engine/Scripting/ScriptMacros.h"

// Projects opt in to the clean stereo look without changing their desktop art.
class Showcase : public Engine::ScriptableEntity {
public:
    void OnCreate() override {
        auto& look = Engine::RenderSettings::Get();
        look.VR.Enabled = true;
    }
};
REGISTER_SCRIPT(Showcase)
