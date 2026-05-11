#include "VertexArray.h"
#include <GL/glew.h>

namespace Engine {

static GLenum ShaderDataTypeToOpenGLBaseType(unsigned int type) {
  switch (type) {
  case GL_FLOAT:
    return GL_FLOAT;
  case GL_INT:
    return GL_INT;
  case GL_UNSIGNED_INT:
    return GL_UNSIGNED_INT;
  case GL_BOOL:
    return GL_BOOL;
  }
  return 0;
}

VertexArray::VertexArray() { glGenVertexArrays(1, &m_RendererID); }

VertexArray::~VertexArray() { glDeleteVertexArrays(1, &m_RendererID); }

void VertexArray::Bind() const { glBindVertexArray(m_RendererID); }

void VertexArray::Unbind() const { glBindVertexArray(0); }

void VertexArray::AddVertexBuffer(
    const std::shared_ptr<VertexBuffer> &vertexBuffer) {
  glBindVertexArray(m_RendererID);
  vertexBuffer->Bind();

  const auto &layout = vertexBuffer->GetLayout();

  unsigned int index = 0;
  for (auto &existing : m_VertexBuffers) {
    index += (unsigned int)existing->GetLayout().GetElements().size();
  }
  for (const auto &element : layout) {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index,
                          element.Size / 4,
                          element.Type, element.Normalized ? GL_TRUE : GL_FALSE,
                          layout.GetStride(),
                          (const void *)(uintptr_t)element.Offset);
    index++;
  }

  m_VertexBuffers.push_back(vertexBuffer);
}

void VertexArray::SetIndexBuffer(
    const std::shared_ptr<IndexBuffer> &indexBuffer) {
  glBindVertexArray(m_RendererID);
  indexBuffer->Bind();
  m_IndexBuffer = indexBuffer;
}

}
