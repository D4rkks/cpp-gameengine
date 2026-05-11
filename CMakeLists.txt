cmake_minimum_required(VERSION 3.20)
project(MyEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)

# -----------------------------------------------------------------------------
# SDL2
# -----------------------------------------------------------------------------
FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/archive/refs/heads/SDL2.zip
)
FetchContent_MakeAvailable(SDL2)

# -----------------------------------------------------------------------------
# GLM
# -----------------------------------------------------------------------------
FetchContent_Declare(
    glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip
)
FetchContent_MakeAvailable(glm)

# -----------------------------------------------------------------------------
# Dear ImGui
# -----------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/heads/docking.zip
)
FetchContent_MakeAvailable(imgui)

# -----------------------------------------------------------------------------
# ImGuizmo
# -----------------------------------------------------------------------------
FetchContent_Declare(
    imguizmo
    URL https://github.com/CedricGuillemet/ImGuizmo/archive/refs/heads/master.zip
    PATCH_COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_SOURCE_DIR}/cmake/patch_imguizmo.ps1
)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

# -----------------------------------------------------------------------------
# TinyObjLoader
# -----------------------------------------------------------------------------
FetchContent_Declare(
    tinyobjloader
    URL https://github.com/tinyobjloader/tinyobjloader/archive/refs/tags/v2.0.0rc10.zip
)
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
    FetchContent_Populate(tinyobjloader)
endif()
# FetchContent_MakeAvailable(tinyobjloader) # Skip building it
# target_include_directories(Engine PUBLIC ${tinyobjloader_SOURCE_DIR}) # Moved below

# -----------------------------------------------------------------------------
# Bullet Physics
# -----------------------------------------------------------------------------
set(BULLET2_MULTITHREADING_OS_WIN32 ON CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    bullet
    URL https://github.com/bulletphysics/bullet3/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(bullet)
message(STATUS "DEBUG: bullet_SOURCE_DIR = ${bullet_SOURCE_DIR}")


# -----------------------------------------------------------------------------
# yaml-cpp
# -----------------------------------------------------------------------------
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(yaml-cpp)

# -----------------------------------------------------------------------------
# OpenXR SDK (VR support — headers + loader)
# -----------------------------------------------------------------------------
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(OPENXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    openxr
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.41.zip
)
FetchContent_MakeAvailable(openxr)
message(STATUS "DEBUG: openxr_SOURCE_DIR = ${openxr_SOURCE_DIR}")

# -----------------------------------------------------------------------------
# Engine Library (Core)
# -----------------------------------------------------------------------------
# CMake GLOB_RECURSE should pick up the new files automatically if we just touch this file or re-configure.
# Forced update for ContentBrowserPanel
# But forcing a re-scan is safer.
file(GLOB_RECURSE ENGINE_SOURCES 
    # Forced update for Font.cpp
    "src/Engine/*.cpp"
    "src/Engine/*.h"
    # AudioEngine is now included via GLOB_RECURSE
    "src/Engine/Core/UUID.cpp"
)
list(REMOVE_ITEM ENGINE_SOURCES "${CMAKE_SOURCE_DIR}/src/Engine/Scripting/GameModuleRuntime.cpp")

add_library(EngineLib SHARED ${ENGINE_SOURCES})
target_compile_definitions(EngineLib PRIVATE ENGINELIB_BUILD)
set_target_properties(EngineLib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_include_directories(EngineLib PUBLIC
    include
    ${SDL2_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
    ${bullet_SOURCE_DIR}/src
    ${tinyobjloader_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/glew-src/include
    ${openxr_SOURCE_DIR}/include
)
target_compile_definitions(EngineLib PUBLIC GLEW_STATIC GLM_ENABLE_EXPERIMENTAL)
# find_package(GLEW REQUIRED)
FetchContent_Declare(
    glew
    URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip
    SOURCE_SUBDIR build/cmake
    PATCH_COMMAND powershell -Command "(Get-Content build/cmake/CMakeLists.txt).Replace('VERSION 2.8.12', 'VERSION 3.20') | Set-Content build/cmake/CMakeLists.txt -Encoding Ascii"
)
FetchContent_MakeAvailable(glew)
message(STATUS "DEBUG: glew_SOURCE_DIR = ${glew_SOURCE_DIR}")
target_link_libraries(EngineLib PUBLIC SDL2::SDL2main SDL2::SDL2 opengl32 glm::glm glew_s BulletDynamics BulletCollision LinearMath yaml-cpp openxr_loader)

# ImGui needs backend implementation files
target_sources(EngineLib PRIVATE 
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

# -----------------------------------------------------------------------------
# Engine Runtime Library (No Editor)
# -----------------------------------------------------------------------------
add_library(EngineRuntime SHARED ${ENGINE_SOURCES})
target_compile_definitions(EngineRuntime PRIVATE ENGINELIB_BUILD)
set_target_properties(EngineRuntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_compile_definitions(EngineRuntime PUBLIC GAME_MODE_RUNTIME GLEW_STATIC GLM_ENABLE_EXPERIMENTAL)
target_include_directories(EngineRuntime PUBLIC
    include
    ${SDL2_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
    ${bullet_SOURCE_DIR}/src
    ${tinyobjloader_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/glew-src/include
    ${openxr_SOURCE_DIR}/include
)
target_link_libraries(EngineRuntime PUBLIC SDL2::SDL2main SDL2::SDL2 opengl32 glm::glm glew_s BulletDynamics BulletCollision LinearMath yaml-cpp openxr_loader)
# Runtime also needs ImGui sources if we use ImGui in Application (even if hidden or stripped logic still references types)
# Our conditional code in Application.cpp REMOVES ImGui calls via #ifndef GAME_MODE_RUNTIME
# But if Application.h includes ImGui headers... checking Application.h...
# Application.h includes <imgui.h> ? No, usually just forward decl or in cpp.
# Let's check Application.h.
# If we strip ImGui calls, we might not need to link ImGui to EngineRuntime?
# But Application.cpp has #include "imgui.h".
# So we need include dirs. Do we need sources? only if we use symbols.
# #ifndef GAME_MODE_RUNTIME wraps majority.
# But "ImGui::NewFrame()" logic is wrapped.
# "ImGui::GetIO()" is wrapped.
# So effectively we might not need ImGui implementation.
# BUT, we might have leftover references or includes.
# Safer to include ImGui sources but they will be dead code stripped if not used. 
# Or define IMGUI_DISABLE ?
# For safety, I'll link ImGui sources to EngineRuntime too, just in case.

target_sources(EngineRuntime PRIVATE 
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

# -----------------------------------------------------------------------------
# Executable (Editor / Default)
# -----------------------------------------------------------------------------
add_executable(Engine src/main.cpp)
target_link_libraries(Engine PRIVATE EngineLib)

# Write the build info header via a 2-pass configure so both @VAR@ and $<TARGET_FILE:...>
# generator expressions resolve correctly.
configure_file(
    ${CMAKE_SOURCE_DIR}/src/Engine/Core/EngineBuildInfo.h.in
    ${CMAKE_BINARY_DIR}/generated/EngineBuildInfo.prelim.h
    @ONLY)

file(GENERATE
    OUTPUT ${CMAKE_BINARY_DIR}/generated/$<CONFIG>/Engine/Core/EngineBuildInfo.h
    INPUT  ${CMAKE_BINARY_DIR}/generated/EngineBuildInfo.prelim.h)

target_include_directories(Engine PRIVATE ${CMAKE_BINARY_DIR}/generated/$<CONFIG>)
target_include_directories(EngineLib PRIVATE ${CMAKE_BINARY_DIR}/generated/$<CONFIG>)
target_include_directories(EngineRuntime PRIVATE ${CMAKE_BINARY_DIR}/generated/$<CONFIG>)

# -----------------------------------------------------------------------------
# Dictionary (Runtime)
# -----------------------------------------------------------------------------
add_executable(GameRuntime src/main.cpp)
target_link_libraries(GameRuntime PRIVATE EngineRuntime)
# target_compile_definitions(GameRuntime PRIVATE GAME_MODE_RUNTIME) # Already defined in EngineRuntime PUBLIC

# Copy SDL2.dll and EngineLib.dll to output directory on Windows
if(WIN32)
    add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:Engine>
    )
    add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:EngineLib>
        $<TARGET_FILE_DIR:Engine>
    )
    # Editor icons — ContentBrowserPanel loads "resources/editor/textures/*" relative to CWD.
    add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:Engine>/resources
    )
    add_custom_command(TARGET GameRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:GameRuntime>
    )
    add_custom_command(TARGET GameRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:EngineRuntime>
        $<TARGET_FILE_DIR:GameRuntime>
    )
    # Copy Assets folder to Runtime directory
    add_custom_command(TARGET GameRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:GameRuntime>/assets
    )
endif()

# -----------------------------------------------------------------------------
# GoogleTest (tests)
# -----------------------------------------------------------------------------
enable_testing()
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(tests)

