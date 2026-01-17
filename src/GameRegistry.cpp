#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Scripts/FollowPlayer.h"
#include "Engine/Scripts/PlayerController.h"
#include "Scripts/FloatingItem.h"
#include "Scripts/MainMenu.h"
#include "Scripts/PauseMenu.h"


namespace Engine {

void RegisterGameScripts() {
  ScriptRegistry::Register("PlayerController", []() { return new PlayerController(); });
  ScriptRegistry::Register("PauseMenu", []() { return new PauseMenu(); });
  ScriptRegistry::Register("MainMenu", []() { return new MainMenu(); });
  ScriptRegistry::Register("FloatingItem", []() { return new FloatingItem(); });
  ScriptRegistry::Register("FollowPlayer", []() { return new FollowPlayer(); });
}

}
