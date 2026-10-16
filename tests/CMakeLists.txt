add_executable(engine_visibility visibility.cpp)
target_link_libraries(engine_visibility PRIVATE glm::glm)
add_test(NAME engine_visibility COMMAND engine_visibility)

add_executable(engine_world_ui vr_world_ui.cpp)
target_link_libraries(engine_world_ui PRIVATE glm::glm)
add_test(NAME engine_world_ui COMMAND engine_world_ui)

option(ENGINE_TEST_OPENGL "Run graphics integration tests (requires a desktop OpenGL driver)" OFF)
if(ENGINE_TEST_OPENGL)
    add_executable(engine_world_ui_gl vr_world_ui_gl.cpp ../src/Engine/Renderer/Buffer.cpp)
    # The runtime and editor DLLs share a filename but have different builds.
    # Keep this test's runtime dependencies away from the editor output folder.
    set_target_properties(engine_world_ui_gl PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
    target_compile_definitions(engine_world_ui_gl PRIVATE "IMGUI_API=__declspec(dllimport)")
    target_link_libraries(engine_world_ui_gl PRIVATE EngineRuntime)
    add_custom_command(TARGET engine_world_ui_gl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:EngineRuntime> $<TARGET_FILE_DIR:engine_world_ui_gl>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:engine_world_ui_gl>)
    add_test(NAME engine_world_ui_gl COMMAND engine_world_ui_gl)
    set_tests_properties(engine_world_ui_gl PROPERTIES LABELS "graphics")
endif()

# Assertions are part of these checks, including in Release builds.
foreach(test_target engine_visibility engine_world_ui engine_world_ui_gl)
    if(TARGET ${test_target})
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
    endif()
endforeach()
