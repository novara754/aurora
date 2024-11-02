#pragma once

#include <array>
#include <functional>

#include <SDL3/SDL_video.h>

#include <span>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include "gpu_buffer.hpp"
#include "gpu_image.hpp"

struct Vertex
{
    glm::vec3 position;
    float padding{0.0f};
};

struct Mesh
{
    GPUBuffer vertex_buffer;
    VkDeviceAddress vertex_buffer_address;
};

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

struct TrianglePushConstants
{
    VkDeviceAddress vertex_buffer_address;
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

    struct
    {
        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buffer;
        VkFence fence;
    } m_immediate_commands;

    VkDescriptorPool m_descriptor_pool;
    VkDescriptorSet m_gradient_set;
    VkPipelineLayout m_gradient_pipeline_layout;
    VkPipeline m_gradient_pipeline;

    VkPipelineLayout m_triangle_pipeline_layout;
    VkPipeline m_triangle_pipeline;

    std::array<float, 3> m_color{1.0f, 0.5f, 0.1f};
    Mesh m_triangle_mesh;

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
    [[nodiscard]] bool init_gradient_pipeline();
    [[nodiscard]] bool init_triangle_pipeline();
    [[nodiscard]] bool init_imgui();
    void draw_frame(VkCommandBuffer cmd_buffer);
    void draw_imgui(VkCommandBuffer cmd_buffer, VkImageView swapchain_image_view);
    [[nodiscard]] bool render_frame();
    void build_ui();
    [[nodiscard]] bool immediate_submit(std::function<void(VkCommandBuffer)> f);

    [[nodiscard]] bool create_mesh(std::span<Vertex> vertices, Mesh *out_mesh);
    void destroy_mesh(Mesh *out_mesh);
};
