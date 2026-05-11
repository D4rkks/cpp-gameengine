#include "Mesh.h"
#include <algorithm>
#include <iostream>
#include <limits>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace Engine {

namespace {
bool LoadGLTF(const std::string &filepath,
              std::vector<MeshVertex> &outVerts,
              std::vector<uint32_t> &outIdx) {
  cgltf_options opts = {};
  cgltf_data *data = nullptr;
  if (cgltf_parse_file(&opts, filepath.c_str(), &data) != cgltf_result_success)
    return false;
  if (cgltf_load_buffers(&opts, data, filepath.c_str()) != cgltf_result_success) {
    cgltf_free(data); return false;
  }

  for (size_t mi = 0; mi < data->meshes_count; ++mi) {
    cgltf_mesh &mesh = data->meshes[mi];
    for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
      cgltf_primitive &prim = mesh.primitives[pi];
      cgltf_accessor *posA = nullptr, *normA = nullptr, *uvA = nullptr;
      for (size_t a = 0; a < prim.attributes_count; ++a) {
        if (prim.attributes[a].type == cgltf_attribute_type_position) posA = prim.attributes[a].data;
        if (prim.attributes[a].type == cgltf_attribute_type_normal)   normA = prim.attributes[a].data;
        if (prim.attributes[a].type == cgltf_attribute_type_texcoord && !uvA)
          uvA = prim.attributes[a].data;
      }
      if (!posA) continue;
      size_t baseVert = outVerts.size();
      for (size_t v = 0; v < posA->count; ++v) {
        MeshVertex mv{};
        cgltf_accessor_read_float(posA, v, &mv.Position.x, 3);
        if (normA) cgltf_accessor_read_float(normA, v, &mv.Normal.x, 3);
        else       mv.Normal = glm::vec3(0, 1, 0);
        if (uvA)   cgltf_accessor_read_float(uvA, v, &mv.TexCoord.x, 2);
        outVerts.push_back(mv);
      }
      if (prim.indices) {
        for (size_t i = 0; i < prim.indices->count; ++i) {
          uint32_t idx = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
          outIdx.push_back((uint32_t)(baseVert + idx));
        }
      } else {
        for (size_t i = 0; i < posA->count; ++i)
          outIdx.push_back((uint32_t)(baseVert + i));
      }
    }
  }
  cgltf_free(data);
  return !outVerts.empty();
}
}

Mesh::Mesh(const std::string &filepath) : m_FilePath(filepath) {

  std::string ext;
  size_t dot = filepath.find_last_of('.');
  if (dot != std::string::npos) {
    ext = filepath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
  }

  std::vector<MeshVertex> vertices;
  std::vector<uint32_t> indices;

  if (ext == ".gltf" || ext == ".glb") {
    if (!LoadGLTF(filepath, vertices, indices)) {
      std::cerr << "[Mesh] Failed to load GLTF: " << filepath << std::endl;
      return;
    }
    m_IsLoaded = true;
    goto FinalizeMesh;
  }

  {
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
  }

FinalizeMesh:
  if (!vertices.empty()) {
    glm::vec3 min(std::numeric_limits<float>::max());
    glm::vec3 max(std::numeric_limits<float>::lowest());

    for (const auto &v : vertices) {
      min = glm::min(min, v.Position);
      max = glm::max(max, v.Position);
    }

    glm::vec3 size = max - min;
    m_LocalDimensions = size;
    m_AABBMin = min;
    m_AABBMax = max;

  }

  m_Vertices = vertices;
  m_Indices  = indices;

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
