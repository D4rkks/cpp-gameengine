#pragma once

#include "../Core/EngineAPI.h"

namespace Engine {

class ENGINE_API RenderState {
public:
  static void SetBlend(bool enabled);
  static void SetDepthTest(bool enabled);
  static void SetCullFace(bool enabled);
  static void SetDepthWrite(bool enabled);
  static void SetDefaultBlendFunc();
  static void SetViewport(int x, int y, int width, int height);
};

class ENGINE_API ScopedRenderState {
public:
  ScopedRenderState();
  ~ScopedRenderState();

private:
  bool m_Blend = false;
  bool m_DepthTest = false;
  bool m_CullFace = false;
  bool m_DepthMask = true;
};

}
