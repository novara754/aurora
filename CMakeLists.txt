cmake_minimum_required(VERSION 3.11)
project(aurora)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SDL_STATIC ON)
set(SDL_SHARED OFF)

set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_NO_EXPORT ON)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)

FetchContent_Declare(
        spdlog
        SYSTEM
        GIT_REPOSITORY "https://github.com/gabime/spdlog"
        GIT_TAG "b6da59447f165ad70a4e3ca1c575b14ea66d92c9"
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        SDL3
        SYSTEM
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
        GIT_TAG "68cc173d9293e890ba93feaed1d3dc17742aa9b3"
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
        vk-bootstrap
        SYSTEM
        GIT_REPOSITORY "https://github.com/charles-lunarg/vk-bootstrap"
        GIT_TAG "v1.3.268"
)
FetchContent_MakeAvailable(vk-bootstrap)

FetchContent_Declare(
        vma
        SYSTEM
        GIT_REPOSITORY "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"
        GIT_TAG "05973d8aeb1a4d12f59aadfb86d20decadba82d1"
)
FetchContent_MakeAvailable(vma)

FetchContent_Declare(
        imgui
        SYSTEM
        GIT_REPOSITORY "https://github.com/ocornut/imgui.git"
        GIT_TAG "71c77c081ac36841e682498229088e7678207112"
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
        glm
        SYSTEM
        GIT_REPOSITORY "https://github.com/g-truc/glm"
        GIT_TAG "1.0.1"
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
        tinyobjloader
        SYSTEM
        GIT_REPOSITORY "https://github.com/tinyobjloader/tinyobjloader"
        GIT_TAG "release"
)
FetchContent_MakeAvailable(tinyobjloader)

FetchContent_Declare(
        assimp
        SYSTEM
        GIT_REPOSITORY "https://github.com/assimp/assimp"
        GIT_TAG "v5.4.3"
)
FetchContent_MakeAvailable(assimp)

FetchContent_Declare(
        stb
        SYSTEM
        GIT_REPOSITORY "https://github.com/nothings/stb"
        GIT_TAG "2e2bef463a5b53ddf8bb788e25da6b8506314c08"
)
FetchContent_MakeAvailable(stb)

find_package(Vulkan REQUIRED)
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

function(compile_shader target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ENV;FORMAT" "SOURCES")
    foreach(source ${arg_SOURCES})
        add_custom_command(
            OUTPUT ${source}.${arg_FORMAT}
            DEPENDS ${source}
            DEPFILE ${source}.d
            COMMAND
                ${glslc_executable}
                --target-env=${arg_ENV}
                -mfmt=${arg_FORMAT}
                -MD -MF ${source}.d
                -o ${source}.${arg_FORMAT}
                ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        )
        target_sources(${target} PRIVATE ${source}.${arg_FORMAT})
    endforeach()
endfunction()

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp

        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp

        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PRIVATE SDL3::SDL3-static)
target_link_libraries(imgui PRIVATE Vulkan::Vulkan)

add_executable(aurora
        src/main.cpp
        src/app.cpp
        src/engine.cpp
        src/forward_pass.cpp
        src/imgui_pass.cpp
        src/read_file.cpp
        src/vma_impl.cpp
        src/tiny_obj_loader_impl.cpp
        src/stbi_impl.cpp
)

target_compile_options(aurora PRIVATE
        -Wall
        -Werror
        -Wextra
        -Wpedantic
)

target_compile_definitions(aurora PRIVATE
        _CRT_SECURE_NO_WARNINGS
        GLM_FORCE_EXPLICIT_CTOR
)

target_include_directories(aurora PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(aurora PRIVATE spdlog::spdlog)
target_link_libraries(aurora PRIVATE SDL3::SDL3-static)
target_link_libraries(aurora PRIVATE Vulkan::Vulkan)
target_link_libraries(aurora PRIVATE vk-bootstrap::vk-bootstrap)
target_link_libraries(aurora PRIVATE GPUOpen::VulkanMemoryAllocator)
target_link_libraries(aurora PRIVATE imgui)
target_link_libraries(aurora PRIVATE glm::glm)
target_link_libraries(aurora PRIVATE tinyobjloader)
target_link_libraries(aurora PRIVATE assimp::assimp)

compile_shader(aurora
    ENV vulkan1.3
    FORMAT bin
    SOURCES
        shaders/forward.vert
        shaders/forward.frag
)

add_custom_command(
        TARGET aurora POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_LIST_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
)
