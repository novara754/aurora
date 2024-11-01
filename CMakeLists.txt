cmake_minimum_required(VERSION 3.11)
project(aurora)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SDL_STATIC ON)
set(SDL_SHARED OFF)

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

find_package(Vulkan REQUIRED)

add_executable(aurora
        src/main.cpp
        src/engine.cpp
        src/vma_impl.cpp
)
target_compile_options(aurora PRIVATE
        -Wall
        -Werror
        -Wextra
        -Wpedantic
)

target_link_libraries(aurora PRIVATE spdlog::spdlog)
target_link_libraries(aurora PRIVATE SDL3::SDL3-static)
target_link_libraries(aurora PRIVATE Vulkan::Vulkan)
target_link_libraries(aurora PRIVATE vk-bootstrap::vk-bootstrap)
target_link_libraries(aurora PRIVATE GPUOpen::VulkanMemoryAllocator)
