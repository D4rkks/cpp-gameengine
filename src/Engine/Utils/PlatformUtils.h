#pragma once

#include <string>

namespace Engine {

class FileDialogs {
public:
  static std::string OpenFile(const char *filter, const char *initialDir = nullptr);
  static std::string SaveFile(const char *filter, const char *initialDir = nullptr);
};

}
