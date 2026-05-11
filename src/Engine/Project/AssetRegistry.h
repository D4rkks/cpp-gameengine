#pragma once

#include "../Core/EngineAPI.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

enum class AssetType {
  Unknown = 0,
  Scene,
  Model,
  Texture,
  Audio,
  Script,
  Font,
  Material
};

struct AssetRecord {
  std::filesystem::path Path;
  std::filesystem::path RelativePath;
  AssetType Type = AssetType::Unknown;
  uintmax_t SizeBytes = 0;
};

class ENGINE_API AssetRegistry {
public:
  void Scan(const std::filesystem::path &root);
  const std::vector<AssetRecord> &GetAssets() const { return m_Assets; }
  size_t CountByType(AssetType type) const;
  static AssetType Classify(const std::filesystem::path &path);
  static const char *ToString(AssetType type);

private:
  std::filesystem::path m_Root;
  std::vector<AssetRecord> m_Assets;
};

}
