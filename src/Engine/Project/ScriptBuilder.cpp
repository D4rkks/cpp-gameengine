#include "ScriptBuilder.h"
#include "Engine/Core/EngineBuildInfo.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>

namespace Engine {

namespace {
std::filesystem::path BuildDir(const Project& p) {
    return p.ProjectRoot / ".cache" / "build";
}
std::filesystem::path GameDllPath(const Project& p) {
    return p.ProjectRoot / p.GameModulePath;
}
}

void ScriptBuilder::EnsureCMakeTree(const Project& project) {
    namespace fs = std::filesystem;
    auto buildDir = BuildDir(project);
    fs::create_directories(buildDir);

    auto cmakePath = buildDir / "CMakeLists.txt";
    std::ofstream o(cmakePath);
    o << "cmake_minimum_required(VERSION 3.20)\n"
      << "project(Game)\n"
      << "set(CMAKE_CXX_STANDARD 20)\n"
      << "file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS \""
      << (project.ProjectRoot / "Assets/Scripts").generic_string() << "/*.cpp\")\n"
      << "list(APPEND GAME_SOURCES \""
      << BuildInfo::EngineIncludeDir << "/Engine/Scripting/GameModuleRuntime.cpp\")\n"
      << "add_library(Game SHARED ${GAME_SOURCES})\n"
      << "target_compile_definitions(Game PRIVATE GAME_MODULE_BUILD)\n"
      << [&]() -> std::string {
             std::ostringstream dirs;
             dirs << "target_include_directories(Game PRIVATE\n";
             dirs << "    \"" << BuildInfo::EngineIncludeDir << "\"\n";
             std::string dep = BuildInfo::EngineDepsIncludeDirs;
             std::istringstream ss(dep);
             std::string tok;
             while (std::getline(ss, tok, ';'))
                 if (!tok.empty()) dirs << "    \"" << tok << "\"\n";
             dirs << ")\n";
             return dirs.str();
         }()
      << "target_link_libraries(Game PRIVATE \"" << BuildInfo::EngineLibImportLib << "\")\n"
      << [&]() -> std::string {

             const std::string root = project.ProjectRoot.generic_string();
             std::ostringstream out;
             for (const char* cfg : {"DEBUG","RELEASE","RELWITHDEBINFO","MINSIZEREL"}) {
                 out << "set_target_properties(Game PROPERTIES\n"
                     << "    RUNTIME_OUTPUT_DIRECTORY_" << cfg << " \"" << root << "\"\n"
                     << "    LIBRARY_OUTPUT_DIRECTORY_"  << cfg << " \"" << root << "\")\n";
             }
             return out.str();
         }();
}

bool ScriptBuilder::IsStale(const Project& project) {
    namespace fs = std::filesystem;
    auto dll = GameDllPath(project);
    if (!fs::exists(dll)) return true;
    auto dllTime = fs::last_write_time(dll);
    auto scriptsDir = project.ProjectRoot / "Assets/Scripts";
    if (!fs::exists(scriptsDir)) return false;
    for (const auto& e : fs::recursive_directory_iterator(scriptsDir)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension();
        if (ext != ".cpp" && ext != ".h") continue;
        if (fs::last_write_time(e) > dllTime) return true;
    }
    return false;
}

BuildResult ScriptBuilder::Build(const Project& project) {
    EnsureCMakeTree(project);
    auto buildDir = BuildDir(project);

    std::stringstream cfg;
    cfg << "\"" << BuildInfo::CMakeExecutable << "\""
        << " -S \"" << buildDir.string() << "\""
        << " -B \"" << (buildDir / "build").string() << "\""
        << " -G \"" << BuildInfo::EngineGenerator << "\""
        << " -A " << BuildInfo::EngineArch
        << " 2>&1";

    std::stringstream bld;
    bld << "\"" << BuildInfo::CMakeExecutable << "\""
        << " --build \"" << (buildDir / "build").string() << "\""
        << " --config Release --target Game --parallel"
        << " 2>&1";

    auto run = [](const std::string& cmd, std::string& log) -> int {
        FILE* p = _popen(cmd.c_str(), "r");
        if (!p) return -1;
        char buf[4096];
        while (fgets(buf, sizeof(buf), p)) log += buf;
        return _pclose(p);
    };

    BuildResult r{true, ""};
    int rc = run(cfg.str(), r.Log);
    if (rc != 0) { r.Ok = false; return r; }
    rc = run(bld.str(), r.Log);
    if (rc != 0) { r.Ok = false; return r; }
    return r;
}

}
