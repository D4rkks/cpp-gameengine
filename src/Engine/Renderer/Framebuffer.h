#pragma once

#include <cstdint>
#include <vector>

namespace Engine {

enum class FramebufferFormat {
  RGBA8,
  RGBA16F,
  Depth
};

struct FramebufferSpecification {
  uint32_t Width = 1280, Height = 720;
  uint32_t Samples = 1;
  FramebufferFormat Format = FramebufferFormat::RGBA8;
  std::vector<FramebufferFormat> ExtraColorAttachments;
  bool HasDepth = true;
  bool DepthAsTexture = false;
  bool SwapChainTarget = false;
};

class Framebuffer {
public:
  Framebuffer(const FramebufferSpecification &spec);
  ~Framebuffer();

  void Invalidate();

  void Bind();
  void Unbind();

  void BlitColorTo(Framebuffer &target);

  void BlitColorTo(Framebuffer &target, int attachmentIndex);

  void BlitDepthTo(Framebuffer &target);

  void Resize(uint32_t width, uint32_t height);

  uint32_t GetColorAttachmentRendererID(int index = 0) const {
    return (index < (int)m_ColorAttachments.size()) ? m_ColorAttachments[index] : 0;
  }
  uint32_t GetDepthAttachmentRendererID() const { return m_DepthAttachment; }
  bool     IsMultisample() const { return m_Specification.Samples > 1; }

  const FramebufferSpecification &GetSpecification() const {
    return m_Specification;
  }

  static Framebuffer *Create(const FramebufferSpecification &spec);

private:
  uint32_t m_RendererID = 0;
  std::vector<uint32_t> m_ColorAttachments;
  uint32_t m_DepthAttachment = 0;
  FramebufferSpecification m_Specification;
};

}
