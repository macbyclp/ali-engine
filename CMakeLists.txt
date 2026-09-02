cmake_minimum_required(VERSION 3.20)
project(ali_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---- GLM (header-only math) ----
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1)
FetchContent_MakeAvailable(glm)

set(ENGINE_SOURCES
    src/engine/window.cpp
    src/engine/shader.cpp
    src/engine/mesh.cpp
    src/engine/app.cpp
    src/game/game.cpp
    src/main.cpp)

add_executable(game ${ENGINE_SOURCES})
target_include_directories(game PRIVATE src)
target_link_libraries(game PRIVATE glm::glm)

if(EMSCRIPTEN)
    # Web build: GLFW + WebGL2 come from Emscripten itself.
    target_compile_definitions(game PRIVATE ENGINE_WEB=1)
    set_target_properties(game PROPERTIES SUFFIX ".html")
    target_link_options(game PRIVATE
        -sUSE_GLFW=3 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets)
else()
    # Native build: fetch GLFW and glad.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4)
    FetchContent_MakeAvailable(glfw)

    FetchContent_Declare(glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG v2.0.6
        SOURCE_SUBDIR cmake)
    FetchContent_MakeAvailable(glad)
    glad_add_library(glad_gl33 STATIC API gl:core=3.3)

    target_link_libraries(game PRIVATE glfw glad_gl33)
endif()
