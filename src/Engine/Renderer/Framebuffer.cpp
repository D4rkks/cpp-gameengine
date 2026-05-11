#include "Framebuffer.h"
#include <GL/glew.h>
#include <iostream>

namespace Engine {

namespace {
GLenum InternalFormat(FramebufferFormat f) {
  switch (f) {
  case FramebufferFormat::RGBA8:   return GL_RGBA8;
  case FramebufferFormat::RGBA16F: return GL_RGBA16F;
  case FramebufferFormat::Depth:   return GL_DEPTH_COMPONENT24;
  }
  return GL_RGBA8;
}
GLenum DataFormat(FramebufferFormat f) {
  switch (f) {
  case FramebufferFormat::RGBA8:
  case FramebufferFormat::RGBA16F: return GL_RGBA;
  case FramebufferFormat::Depth:   return GL_DEPTH_COMPONENT;
  }
  return GL_RGBA;
}
GLenum DataType(FramebufferFormat f) {
  switch (f) {
  case FramebufferFormat::RGBA8:   return GL_UNSIGNED_BYTE;
  case FramebufferFormat::RGBA16F: return GL_FLOAT;
  case FramebufferFormat::Depth:   return GL_FLOAT;
  }
  return GL_UNSIGNED_BYTE;
}
}

Framebuffer::Framebuffer(const FramebufferSpecification &spec)
    : m_Specification(spec) {
  Invalidate();
}

Framebuffer::~Framebuffer() {
  glDeleteFramebuffers(1, &m_RendererID);
  for (auto id : m_ColorAttachments)
    if (id) glDeleteTextures(1, &id);
  m_ColorAttachments.clear();
  if (m_DepthAttachment) {
    if (m_Specification.DepthAsTexture) glDeleteTextures(1, &m_DepthAttachment);
    else                                glDeleteRenderbuffers(1, &m_DepthAttachment);
  }
}

void Framebuffer::Invalidate() {
  if (m_RendererID) {
    glDeleteFramebuffers(1, &m_RendererID);
    for (auto id : m_ColorAttachments)
      if (id) glDeleteTextures(1, &id);
    m_ColorAttachments.clear();
    if (m_DepthAttachment) {
      if (m_Specification.DepthAsTexture) glDeleteTextures(1, &m_DepthAttachment);
      else                                glDeleteRenderbuffers(1, &m_DepthAttachment);
    }
    m_DepthAttachment = 0;
  }

  glGenFramebuffers(1, &m_RendererID);
  glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

  const bool msaa      = m_Specification.Samples > 1;
  const bool depthOnly = m_Specification.Format == FramebufferFormat::Depth;

  std::vector<FramebufferFormat> colorFormats;
  if (!depthOnly) {
    colorFormats.push_back(m_Specification.Format);
    for (auto f : m_Specification.ExtraColorAttachments)
      colorFormats.push_back(f);
  }

  m_ColorAttachments.resize(colorFormats.size(), 0);
  for (size_t i = 0; i < colorFormats.size(); ++i) {
    glGenTextures(1, &m_ColorAttachments[i]);
    GLenum attachment = GL_COLOR_ATTACHMENT0 + (GLenum)i;
    if (msaa) {
      glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachments[i]);
      glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                              (GLsizei)m_Specification.Samples,
                              InternalFormat(colorFormats[i]),
                              m_Specification.Width, m_Specification.Height,
                              GL_TRUE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                             GL_TEXTURE_2D_MULTISAMPLE,
                             m_ColorAttachments[i], 0);
    } else {
      glBindTexture(GL_TEXTURE_2D, m_ColorAttachments[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat(colorFormats[i]),
                   m_Specification.Width, m_Specification.Height, 0,
                   DataFormat(colorFormats[i]), DataType(colorFormats[i]),
                   nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D,
                             m_ColorAttachments[i], 0);
    }
  }

  if (!m_ColorAttachments.empty()) {
    std::vector<GLenum> drawBufs(m_ColorAttachments.size());
    for (size_t i = 0; i < m_ColorAttachments.size(); ++i)
      drawBufs[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
    glDrawBuffers((GLsizei)drawBufs.size(), drawBufs.data());
  }

  if (m_Specification.HasDepth || depthOnly) {
    if (m_Specification.DepthAsTexture || depthOnly) {
      glGenTextures(1, &m_DepthAttachment);
      glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                   m_Specification.Width, m_Specification.Height, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
      glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                             m_DepthAttachment, 0);
    } else {
      glGenRenderbuffers(1, &m_DepthAttachment);
      glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
      if (msaa) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                         (GLsizei)m_Specification.Samples,
                                         GL_DEPTH_COMPONENT24,
                                         m_Specification.Width,
                                         m_Specification.Height);
      } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              m_Specification.Width, m_Specification.Height);
      }
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, m_DepthAttachment);
    }
  }

  if (depthOnly) {
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  }

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "Framebuffer is incomplete!" << std::endl;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
  glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
  glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

void Framebuffer::Unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void Framebuffer::BlitColorTo(Framebuffer &target) {
  BlitColorTo(target, 0);
}

void Framebuffer::BlitColorTo(Framebuffer &target, int attachmentIndex) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.m_RendererID);
  glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
  GLenum drawBuf = GL_COLOR_ATTACHMENT0 + attachmentIndex;
  glDrawBuffers(1, &drawBuf);
  glBlitFramebuffer(0, 0, m_Specification.Width, m_Specification.Height,
                    0, 0, target.m_Specification.Width, target.m_Specification.Height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);

  std::vector<GLenum> drawBufs(target.m_ColorAttachments.size());
  for (size_t i = 0; i < target.m_ColorAttachments.size(); ++i)
    drawBufs[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
  if (!drawBufs.empty())
    glDrawBuffers((GLsizei)drawBufs.size(), drawBufs.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::BlitDepthTo(Framebuffer &target) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.m_RendererID);
  glBlitFramebuffer(0, 0, m_Specification.Width, m_Specification.Height,
                    0, 0, target.m_Specification.Width, target.m_Specification.Height,
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

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

}
