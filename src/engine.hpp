#pragma once

#include <array>
#include <functional>
#include <vector>

#include <SDL3/SDL_video.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include "gpu_image.hpp"

class DeletionQueue
{
    std::vector<std::function<void()>> m_queue;

  public:
    void add(std::function<void()> f)
    {
        m_queue.emplace_back(f);
    }

    void delete_all()
    {
        for (auto it = m_queue.rbegin(); it != m_queue.rend(); ++it)
        {
            (*it)();
        }
        m_queue.clear();
    }
};

struct FrameData
{
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;
    VkSemaphore render_semaphore;
    VkSemaphore present_semaphore;
    VkFence fence;

    DeletionQueue deletion_queue;
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

    VmaAllocator m_allocator;

    VkExtent2D m_swapchain_extent;
    VkFormat m_swapchain_format;
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> m_swapchain_images;
    std::vector<VkImageView> m_swapchain_image_views;

    VkQueue m_graphics_queue{VK_NULL_HANDLE};
    uint32_t m_graphics_queue_family{0};

    size_t m_frame_idx{0};
    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_frames;

    DeletionQueue m_deletion_queue;

    GPUImage m_render_target;

    VkDescriptorPool m_descriptor_pool;
    VkDescriptorSet m_gradient_set;
    VkPipelineLayout m_gradient_pipeline_layout;
    VkPipeline m_gradient_pipeline;

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

    [[nodiscard]] bool init();
    void run();

  private:
    [[nodiscard]] bool init_swapchain();
    [[nodiscard]] bool init_pipeline();
    void draw_frame(VkCommandBuffer cmd_buffer);
    [[nodiscard]] bool render_frame();
};
