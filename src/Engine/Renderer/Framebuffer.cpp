#include "Framebuffer.h"
#include <GL/glew.h>
#include <iostream>

namespace Engine {

Framebuffer::Framebuffer(const FramebufferSpecification &spec)
    : m_Specification(spec) {
  Invalidate();
}

Framebuffer::~Framebuffer() {
  glDeleteFramebuffers(1, &m_RendererID);
  glDeleteTextures(1, &m_ColorAttachment);
  glDeleteRenderbuffers(1, &m_DepthAttachment);
}

void Framebuffer::Invalidate() {
  if (m_RendererID) {
    glDeleteFramebuffers(1, &m_RendererID);
    glDeleteTextures(1, &m_ColorAttachment);
    glDeleteRenderbuffers(1, &m_DepthAttachment);
  }

  glGenFramebuffers(1, &m_RendererID);
  glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

  glGenTextures(1, &m_ColorAttachment);
  glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width,
               m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_ColorAttachment, 0);

  glGenRenderbuffers(1, &m_DepthAttachment);
  glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                        m_Specification.Width, m_Specification.Height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, m_DepthAttachment);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "Framebuffer is incomplete!" << std::endl;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
  glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
  glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

void Framebuffer::Unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void Framebuffer::Resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 ||
      (width == m_Specification.Width && height == m_Specification.Height))
    return;

  m_Specification.Width = width;
  m_Specification.Height = height;

  Invalidate();
}

Framebuffer *Framebuffer::Create(const FramebufferSpecification &spec) {
  return new Framebuffer(spec);
}

} // namespace Engine
