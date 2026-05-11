#include "ProjectSerializer.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <set>

namespace Engine {

ProjectResult ProjectSerializer::Serialize(const Project& project, const std::filesystem::path& path) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Name"         << YAML::Value << project.Name;
    out << YAML::Key << "StartupScene" << YAML::Value << project.StartupSceneName;
    out << YAML::Key << "GameModule"   << YAML::Value << project.GameModulePath;
    out << YAML::Key << "Scenes"       << YAML::Value << YAML::BeginSeq;
    for (const auto& s : project.Scenes) {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << s.Name;
        out << YAML::Key << "Path" << YAML::Value << s.RelativePath;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::EndMap;

    std::ofstream fout(path);
    if (!fout) return {false, "cannot write " + path.string()};
    fout << out.c_str();
    return {true, ""};
}

ProjectResult ProjectSerializer::Deserialize(const std::filesystem::path& path, Project& out) {
    if (!std::filesystem::exists(path))
        return {false, "project file not found: " + path.string()};

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return {false, std::string("YAML parse error: ") + e.what()};
    }

    auto proj = root["Project"];
    if (!proj) return {false, "missing 'Project' root node"};

    out = {};
    out.Name             = proj["Name"]         ? proj["Name"].as<std::string>()         : "";
    out.StartupSceneName = proj["StartupScene"] ? proj["StartupScene"].as<std::string>() : "";
    out.GameModulePath   = proj["GameModule"]   ? proj["GameModule"].as<std::string>()   : "Game.dll";
    out.ProjectRoot      = path.parent_path();

    auto scenes = proj["Scenes"];
    if (scenes) {
        for (auto n : scenes) {
            SceneEntry e;
            e.Name         = n["Name"].as<std::string>();
            e.RelativePath = n["Path"].as<std::string>();
            out.Scenes.push_back(e);
        }
    }

    std::set<std::string> seen;
    for (const auto& s : out.Scenes) {
        if (!seen.insert(s.Name).second)
            return {false, "duplicate scene name in project: " + s.Name};
    }

    if (out.FindScene(out.StartupSceneName) == nullptr)
        return {false, "startup scene '" + out.StartupSceneName + "' not found in scene list"};

    for (const auto& s : out.Scenes) {
        auto full = out.ProjectRoot / s.RelativePath;
        if (!std::filesystem::exists(full))
            return {false, "scene file missing on disk: " + s.RelativePath};
    }

    return {true, ""};
}

}
