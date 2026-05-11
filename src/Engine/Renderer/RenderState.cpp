#include "RenderState.h"
#include <GL/glew.h>

namespace Engine {

void RenderState::SetBlend(bool enabled) {
  enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

void RenderState::SetDepthTest(bool enabled) {
  enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
}

void RenderState::SetCullFace(bool enabled) {
  enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void RenderState::SetDepthWrite(bool enabled) {
  glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void RenderState::SetDefaultBlendFunc() {
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void RenderState::SetViewport(int x, int y, int width, int height) {
  glViewport(x, y, width, height);
}

ScopedRenderState::ScopedRenderState() {
  m_Blend = glIsEnabled(GL_BLEND) == GL_TRUE;
  m_DepthTest = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
  m_CullFace = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
  GLboolean depthMask = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  m_DepthMask = depthMask == GL_TRUE;
}

ScopedRenderState::~ScopedRenderState() {
  RenderState::SetBlend(m_Blend);
  RenderState::SetDepthTest(m_DepthTest);
  RenderState::SetCullFace(m_CullFace);
  RenderState::SetDepthWrite(m_DepthMask);
}

}
