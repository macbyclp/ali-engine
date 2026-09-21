cmake_minimum_required(VERSION 3.24)
project(ali_engine VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_Declare(entt GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.13.2)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)

FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    SOURCE_SUBDIR cmake)

# single-header libs: repos have no CMakeLists, just fetch the sources
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG f0569113c93ad095470c54bf34a17b36646bbbb5)
FetchContent_Declare(cgltf GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9-docking)
FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG ba662b119d64f9ab700bb2cd7b2781f9044f5565)
FetchContent_Declare(imnodes GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG v0.9.3)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    SOURCE_SUBDIR Build)

FetchContent_MakeAvailable(glm json entt glfw glad stb cgltf jolt miniaudio imgui imguizmo imnodes)
glad_add_library(glad_gl46 STATIC API gl:core=4.6)

# imgui-node-editor 0.9.3 predates imgui's own ImVec2 ==/!= operators (imgui.h,
# behind IMGUI_DEFINE_MATH_OPERATORS -- which node-editor turns on itself). It then
# redefines them as `inline` against imgui's `static inline`, so the TU fails to
# compile. Guard node-editor's copies behind imgui's own "implemented" marker.
function(_patch_node_editor_math file)
    file(READ "${file}" _src)
    if(NOT _src MATCHES "ALIENGINE_MATH_PATCHED")
        string(REPLACE
            "inline bool operator==(const ImVec2& lhs, const ImVec2& rhs)"
            "// ALIENGINE_MATH_PATCHED\n# ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline bool operator==(const ImVec2& lhs, const ImVec2& rhs)"
            _src "${_src}")
        string(REPLACE
            "inline ImVec2 operator*(const float lhs, const ImVec2& rhs)"
            "# endif // IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline ImVec2 operator*(const float lhs, const ImVec2& rhs)"
            _src "${_src}")
        file(WRITE "${file}" "${_src}")
    endif()
endfunction()
_patch_node_editor_math("${imnodes_SOURCE_DIR}/imgui_extra_math.h")
_patch_node_editor_math("${imnodes_SOURCE_DIR}/imgui_extra_math.inl")

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
    ${imnodes_SOURCE_DIR}/imgui_node_editor.cpp
    ${imnodes_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imnodes_SOURCE_DIR}/imgui_canvas.cpp
    ${imnodes_SOURCE_DIR}/crude_json.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR} ${imnodes_SOURCE_DIR})
target_link_libraries(imgui PUBLIC glfw)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(engine ${ENGINE_SOURCES})
target_include_directories(engine PRIVATE src ${stb_SOURCE_DIR} ${cgltf_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
target_link_libraries(engine PRIVATE
    glm::glm nlohmann_json::nlohmann_json EnTT::EnTT glfw glad_gl46 Jolt imgui)

# Linux: plugin host uses dlopen, miniaudio needs pthread/dl/m.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(engine PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

target_compile_definitions(engine PRIVATE
    ENGINE_ASSET_DIR="${CMAKE_SOURCE_DIR}"
    ALI_VERSION="${PROJECT_VERSION}"
    GLFW_INCLUDE_NONE)

# ---------------------------------------------------------------------------
# Tests (ctest). GL-free unit test, headless protocol tests (EGL / hidden window),
# and editor self-tests that need a window system (xvfb-run / $DISPLAY / Xephyr, else SKIP).
# ---------------------------------------------------------------------------
option(ALI_BUILD_TESTS "Build and register the ctest suite" ON)
if(ALI_BUILD_TESTS)
    enable_testing()
    add_executable(unit_transform tests/unit_transform.cpp)
    target_include_directories(unit_transform PRIVATE src ${stb_SOURCE_DIR} ${cgltf_SOURCE_DIR})
    target_link_libraries(unit_transform PRIVATE glm::glm nlohmann_json::nlohmann_json EnTT::EnTT glad_gl46 glfw)
    target_compile_definitions(unit_transform PRIVATE GLFW_INCLUDE_NONE)
    add_test(NAME unit_transform COMMAND unit_transform)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME protocol COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol_test.py $<TARGET_FILE:engine>)
        add_test(NAME microbench COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/microbench_check.py $<TARGET_FILE:engine> 2000)
        set_tests_properties(protocol microbench PROPERTIES TIMEOUT 240)
    endif()
    add_test(NAME cli_version COMMAND engine --version)
    set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "ali-engine ${PROJECT_VERSION}")
    add_test(NAME cli_bad_arg COMMAND engine --definitely-not-a-flag)
    set_tests_properties(cli_bad_arg PROPERTIES WILL_FAIL TRUE)

    set(_wd ${CMAKE_SOURCE_DIR}/tests/with_display.sh)
    add_test(NAME blueprint_selftest COMMAND ${_wd} $<TARGET_FILE:engine> --editor)
    set_tests_properties(blueprint_selftest PROPERTIES ENVIRONMENT "ALI_BLUEPRINT_SELFTEST=1"
        PASS_REGULAR_EXPRESSION "ALL PASS" SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME playstop_selftest COMMAND ${_wd} $<TARGET_FILE:engine> --editor --scene ${CMAKE_SOURCE_DIR}/tests/scenes/playstop.json)
    set_tests_properties(playstop_selftest PROPERTIES ENVIRONMENT "ALI_PLAYSTOP_SELFTEST=1"
        PASS_REGULAR_EXPRESSION "PLAYSTOP ALL PASS" FAIL_REGULAR_EXPRESSION "PLAYSTOP FAILED|: FAIL"
        SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME editor_selftest COMMAND ${_wd} $<TARGET_FILE:engine> --editor --scene ${CMAKE_SOURCE_DIR}/scenes/demo.json)
    set_tests_properties(editor_selftest PROPERTIES ENVIRONMENT "ALI_EDITOR_SELFTEST=1"
        PASS_REGULAR_EXPRESSION "SELFTEST done" FAIL_REGULAR_EXPRESSION "=> BROKEN"
        SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# ---------------------------------------------------------------------------
# install(): binary + the runtime data the docs point at (scenes, tools, assets, docs).
#   cmake --install build --prefix /opt/ali-engine --component runtime
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
# Component "runtime" = just our files (the FetchContent dependencies bring their own
# install rules; `--component runtime` leaves them out).
install(TARGETS engine RUNTIME DESTINATION . COMPONENT runtime)
install(DIRECTORY scenes tools assets games clients docs DESTINATION . COMPONENT runtime
        PATTERN "node_modules" EXCLUDE PATTERN "__pycache__" EXCLUDE)
install(FILES README.md LICENSE DESTINATION . COMPONENT runtime)
install(FILES packaging/RELEASE-linux.txt DESTINATION . RENAME RELEASE.txt COMPONENT runtime)
install(DIRECTORY DESTINATION plugins COMPONENT runtime)
