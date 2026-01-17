#include "Mesh.h"
#include <algorithm>
#include <iostream>
#include <limits>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

namespace Engine {

Mesh::Mesh(const std::string &filepath) : m_FilePath(filepath) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                        filepath.c_str())) {
    std::cerr << "Failed to load mesh: " << filepath << std::endl;
    std::cerr << "Warning: " << warn << std::endl;
    std::cerr << "Error: " << err << std::endl;
    return;
  }

  std::vector<MeshVertex> vertices;
  std::vector<uint32_t> indices;

  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      MeshVertex vertex{};

      vertex.Position = {attrib.vertices[3 * index.vertex_index + 0],
                         attrib.vertices[3 * index.vertex_index + 1],
                         attrib.vertices[3 * index.vertex_index + 2]};

      if (index.normal_index >= 0) {
        vertex.Normal = {attrib.normals[3 * index.normal_index + 0],
                         attrib.normals[3 * index.normal_index + 1],
                         attrib.normals[3 * index.normal_index + 2]};
      }

      if (index.texcoord_index >= 0) {
        vertex.TexCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
                           attrib.texcoords[2 * index.texcoord_index + 1]};
      }

      vertices.push_back(vertex);
      indices.push_back(
          (uint32_t)indices.size());
    }
  }

  if (!vertices.empty()) {
    glm::vec3 min(std::numeric_limits<float>::max());
    glm::vec3 max(std::numeric_limits<float>::lowest());

    for (const auto &v : vertices) {
      min = glm::min(min, v.Position);
      max = glm::max(max, v.Position);
    }

    glm::vec3 center = (min + max) * 0.5f;
    glm::vec3 size = max - min;
    float maxDim = std::max({size.x, size.y, size.z});
    float scale = (maxDim > 0.0001f) ? (1.0f / maxDim) : 1.0f;

    m_LocalDimensions = size * scale;
    m_AABBMin = (min - center) * scale;
    m_AABBMax = (max - center) * scale;

    for (auto &v : vertices) {
      v.Position = (v.Position - center) * scale;
    }
  }

  m_VertexArray = std::make_shared<VertexArray>();

  m_VertexBuffer = std::make_shared<VertexBuffer>(
      (float *)vertices.data(), vertices.size() * sizeof(MeshVertex));

  BufferLayout layout = {{GL_FLOAT, 3, "a_Position"},
                         {GL_FLOAT, 3, "a_Normal"},
                         {GL_FLOAT, 2, "a_TexCoord"}};
  m_VertexBuffer->SetLayout(layout);
  m_VertexArray->AddVertexBuffer(m_VertexBuffer);

  m_IndexBuffer = std::make_shared<IndexBuffer>(indices.data(), indices.size());
  m_VertexArray->SetIndexBuffer(m_IndexBuffer);

  m_IsLoaded = true;
}

Mesh::~Mesh() {}

void Mesh::Bind() const {
  if (m_VertexArray)
    m_VertexArray->Bind();
}

void Mesh::Unbind() const {
  if (m_VertexArray)
    m_VertexArray->Unbind();
}

}
