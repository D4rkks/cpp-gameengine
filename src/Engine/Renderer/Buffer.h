#pragma once

#include <GL/glew.h>
#include <string>
#include <vector>

namespace Engine {

struct BufferElement {
  std::string Name;
  unsigned int Type;
  unsigned int Size;
  unsigned int Offset;
  bool Normalized;

  BufferElement(unsigned int type, unsigned int count, const std::string &name,
                bool normalized = false)
      : Name(name), Type(type), Size(GetSizeOfType(type) * count), Offset(0),
        Normalized(normalized) {}

  static unsigned int GetSizeOfType(unsigned int type) {
    switch (type) {
    case GL_FLOAT:
      return 4;
    case GL_UNSIGNED_INT:
      return 4;
    case GL_UNSIGNED_BYTE:
      return 1;
    }
    return 0;
  }
};

class BufferLayout {
public:
  BufferLayout() {}
  BufferLayout(const std::initializer_list<BufferElement> &elements)
      : m_Elements(elements) {
    CalculateOffsetsAndStride();
  }

  const std::vector<BufferElement> &GetElements() const { return m_Elements; }
  unsigned int GetStride() const { return m_Stride; }
  std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
  std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
  std::vector<BufferElement>::const_iterator begin() const {
    return m_Elements.begin();
  }
  std::vector<BufferElement>::const_iterator end() const {
    return m_Elements.end();
  }

private:
  void CalculateOffsetsAndStride() {
    unsigned int offset = 0;
    m_Stride = 0;
    for (auto &element : m_Elements) {
      element.Offset = offset;
      offset += element.Size;
      m_Stride += element.Size;
    }
  }

  std::vector<BufferElement> m_Elements;
  unsigned int m_Stride = 0;
};

class VertexBuffer {
public:
  VertexBuffer(float *vertices, uint32_t size);
  ~VertexBuffer();

  void Bind() const;
  void Unbind() const;

  void SetLayout(const BufferLayout &layout) { m_Layout = layout; }
  const BufferLayout &GetLayout() const { return m_Layout; }

private:
  unsigned int m_RendererID;
  BufferLayout m_Layout;
};

class IndexBuffer {
public:
  IndexBuffer(uint32_t *indices, uint32_t count);
  ~IndexBuffer();

  void Bind() const;
  void Unbind() const;

  uint32_t GetCount() const { return m_Count; }

private:
  unsigned int m_RendererID;
  uint32_t m_Count;
};

}
