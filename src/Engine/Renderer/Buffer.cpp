#include "Buffer.h"
#include <GL/glew.h>

namespace Engine {

VertexBuffer::VertexBuffer(float *vertices, uint32_t size) {
  glGenBuffers(1, &m_RendererID);
  glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
  glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
}

VertexBuffer::~VertexBuffer() { glDeleteBuffers(1, &m_RendererID); }

void VertexBuffer::Bind() const { glBindBuffer(GL_ARRAY_BUFFER, m_RendererID); }

void VertexBuffer::Unbind() const { glBindBuffer(GL_ARRAY_BUFFER, 0); }

IndexBuffer::IndexBuffer(uint32_t *indices, uint32_t count) : m_Count(count) {
  glGenBuffers(1, &m_RendererID);
  // Element bindings belong to the current VAO. Uploading a new index buffer
  // must not replace the indices of an unrelated, previously loaded mesh.
  GLint previous = 0;
  glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &previous);
  glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
  glBufferData(GL_COPY_WRITE_BUFFER, count * sizeof(uint32_t), indices,
               GL_STATIC_DRAW);
  glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)previous);
}

IndexBuffer::~IndexBuffer() { glDeleteBuffers(1, &m_RendererID); }

void IndexBuffer::Bind() const {
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
}

void IndexBuffer::Unbind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }

}
