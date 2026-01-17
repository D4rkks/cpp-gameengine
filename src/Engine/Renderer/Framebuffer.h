#pragma once

#include <cstdint>

namespace Engine {

struct FramebufferSpecification {
  uint32_t Width, Height;
  uint32_t Samples = 1;
  bool SwapChainTarget = false;
};

class Framebuffer {
public:
  Framebuffer(const FramebufferSpecification &spec);
  ~Framebuffer();

  void Invalidate();

  void Bind();
  void Unbind();

  void Resize(uint32_t width, uint32_t height);

  uint32_t GetColorAttachmentRendererID() const { return m_ColorAttachment; }

  const FramebufferSpecification &GetSpecification() const {
    return m_Specification;
  }

  static Framebuffer *Create(const FramebufferSpecification &spec);

private:
  uint32_t m_RendererID = 0;
  uint32_t m_ColorAttachment = 0;
  uint32_t m_DepthAttachment = 0;
  FramebufferSpecification m_Specification;
};

}
