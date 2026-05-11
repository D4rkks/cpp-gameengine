#pragma once

#include "../Animation/Skeleton.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "VertexArray.h"
namespace Engine {

struct MeshVertex {
  glm::vec3 Position;
  glm::vec3 Normal;
  glm::vec2 TexCoord;
};

struct MeshSkinningVertex {
  glm::vec4 BoneIDs{0.0f};
  glm::vec4 BoneWeights{0.0f};
};

class Mesh {
public:
  Mesh(const std::string &filepath);
  ~Mesh();

  void Bind() const;
  void Unbind() const;

  const std::shared_ptr<VertexArray> &GetVertexArray() const {
    return m_VertexArray;
  }
  const std::string &GetFilePath() const { return m_FilePath; }

  bool IsLoaded() const { return m_IsLoaded; }
  const glm::vec3 &GetLocalDimensions() const { return m_LocalDimensions; }
  const glm::vec3 &GetAABBMin() const { return m_AABBMin; }
  const glm::vec3 &GetAABBMax() const { return m_AABBMax; }

  const std::vector<MeshVertex> &GetVertices() const { return m_Vertices; }
  const std::vector<uint32_t>   &GetIndices()  const { return m_Indices; }

private:
  std::string m_FilePath;
  bool m_IsLoaded = false;
  glm::vec3 m_LocalDimensions = {1.0f, 1.0f, 1.0f};
  glm::vec3 m_AABBMin = {0.0f, 0.0f, 0.0f};
  glm::vec3 m_AABBMax = {0.0f, 0.0f, 0.0f};

  std::vector<MeshVertex> m_Vertices;
  std::vector<uint32_t>   m_Indices;

  std::shared_ptr<VertexArray> m_VertexArray;
  std::shared_ptr<VertexBuffer> m_VertexBuffer;
  std::shared_ptr<IndexBuffer> m_IndexBuffer;
};

}
