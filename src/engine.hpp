#pragma once

#include <array>
#include <functional>
#include <glm/trigonometric.hpp>
#include <string>

#include <SDL3/SDL_video.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <glm/mat4x4.hpp>

#include "deletion_queue.hpp"
#include "gpu.hpp"

struct FrameData
{
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;
    VkSemaphore render_semaphore;
    VkSemaphore present_semaphore;
    VkFence fence;

    DeletionQueue deletion_queue;
};

struct ForwardPushConstants
{
    glm::mat4 camera;
    VkDeviceAddress vertex_buffer_address;
};

struct Swapchain
{
    uint64_t generation{0};
    VkExtent2D extent;
    VkFormat format;
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
};

class Engine
{
    static constexpr size_t NUM_FRAMES_IN_FLIGHT = 2;

    SDL_Window *m_window;

    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    Swapchain m_swapchain;

    VkQueue m_graphics_queue{VK_NULL_HANDLE};
    uint32_t m_graphics_queue_family{0};

    VmaAllocator m_allocator;

    VkDescriptorPool m_descriptor_pool;

    size_t m_frame_idx{0};
    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_frames;

    DeletionQueue m_deletion_queue;
    struct
    {
        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buffer;
        VkFence fence;
    } m_immediate_commands;

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

  public:
    static VkBool32 debug_message_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData
    );

    explicit Engine(SDL_Window *window) : m_window(window)
    {
    }

    ~Engine();

    SDL_Window *get_window()
    {
        return m_window;
    }

    VkInstance get_instance()
    {
        return m_instance;
    }

    VkPhysicalDevice get_physical_device()
    {
        return m_physical_device;
    }

    VkDevice get_device()
    {
        return m_device;
    }

    const Swapchain &get_swapchain()
    {
        return m_swapchain;
    }

    VkQueue get_queue()
    {
        return m_graphics_queue;
    }

    uint32_t get_queue_family()
    {
        return m_graphics_queue_family;
    }

    VkDescriptorPool get_descriptor_pool()
    {
        return m_descriptor_pool;
    }

    [[nodiscard]] bool init();

    [[nodiscard]] bool refresh_swapchain();

    [[nodiscard]] bool start_frame(VkCommandBuffer &out_cmd_buffer, uint32_t &swapchain_image_idx);
    [[nodiscard]] bool finish_frame(uint32_t swapchain_image_idx);

    [[nodiscard]] bool create_image(
        VmaMemoryUsage memory_usage, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage,
        VkImageAspectFlags aspect_mask, GPUImage &out_image
    );
    [[nodiscard]] bool create_image_from_file(const std::string &path, GPUImage &out_image);
    void destroy_image(GPUImage &image);

    [[nodiscard]] bool create_buffer(
        VmaMemoryUsage memory_usage, VkDeviceSize size, VkBufferUsageFlags usage,
        GPUBuffer &out_buffer
    );
    void destroy_buffer(GPUBuffer &buffer);

    [[nodiscard]] bool immediate_submit(std::function<void(VkCommandBuffer)> f);

  private:
    [[nodiscard]] bool init_swapchain();
};

VkImageSubresourceRange full_image_range(VkImageAspectFlags aspect_mask);

void transition_image(
    VkCommandBuffer cmd_buffer, VkImage image, VkImageLayout src_layout, VkImageLayout dst_layout
);

void blit_image(
    VkCommandBuffer cmd_buffer, VkImage src_image, VkExtent3D src_extent, VkImage dst_image,
    VkExtent3D dst_extent
);
