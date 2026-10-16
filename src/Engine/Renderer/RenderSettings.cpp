#include "RenderSettings.h"

namespace Engine {

RenderSettings &RenderSettings::Get() {
  static RenderSettings s_Settings;
  return s_Settings;
}

}
