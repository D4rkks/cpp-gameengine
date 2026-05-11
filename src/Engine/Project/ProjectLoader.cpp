#include "ProjectLoader.h"
#include <vector>

namespace Engine {

ProjectResult ProjectLoader::LoadFromDirectory(const std::filesystem::path& dir, Project& out) {
    if (!std::filesystem::exists(dir))
        return {false, "directory does not exist: " + dir.string()};

    std::vector<std::filesystem::path> candidates;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".myproject")
            candidates.push_back(e.path());
    }

    if (candidates.empty())
        return {false, "no .myproject in " + dir.string()};
    if (candidates.size() > 1)
        return {false, "multiple .myproject files in " + dir.string()};

    return ProjectSerializer::Deserialize(candidates.front(), out);
}

ProjectResult ProjectLoader::LoadFromFile(const std::filesystem::path& file, Project& out) {
    return ProjectSerializer::Deserialize(file, out);
}

}
