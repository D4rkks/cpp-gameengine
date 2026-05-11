#include "AssetRegistry.h"
#include <algorithm>
#include <cctype>

namespace Engine {

namespace {
std::string LowerExt(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext;
}
}

void AssetRegistry::Scan(const std::filesystem::path &root) {
  m_Root = root;
  m_Assets.clear();
  if (root.empty() || !std::filesystem::exists(root))
    return;

  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file())
      continue;

    AssetRecord record;
    record.Path = entry.path();
    record.RelativePath = std::filesystem::relative(entry.path(), root);
    record.Type = Classify(entry.path());
    record.SizeBytes = entry.file_size();
    m_Assets.push_back(record);
  }
}

size_t AssetRegistry::CountByType(AssetType type) const {
  return (size_t)std::count_if(m_Assets.begin(), m_Assets.end(),
                              [type](const AssetRecord &asset) {
                                return asset.Type == type;
                              });
}

AssetType AssetRegistry::Classify(const std::filesystem::path &path) {
  const std::string ext = LowerExt(path);
  if (ext == ".scene")
    return AssetType::Scene;
  if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
    return AssetType::Model;
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" ||
      ext == ".bmp" || ext == ".hdr")
    return AssetType::Texture;
  if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
    return AssetType::Audio;
  if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cs")
    return AssetType::Script;
  if (ext == ".ttf" || ext == ".otf")
    return AssetType::Font;
  if (ext == ".mat")
    return AssetType::Material;
  return AssetType::Unknown;
}

const char *AssetRegistry::ToString(AssetType type) {
  switch (type) {
  case AssetType::Scene: return "Scene";
  case AssetType::Model: return "Model";
  case AssetType::Texture: return "Texture";
  case AssetType::Audio: return "Audio";
  case AssetType::Script: return "Script";
  case AssetType::Font: return "Font";
  case AssetType::Material: return "Material";
  default: return "Unknown";
  }
}

}
