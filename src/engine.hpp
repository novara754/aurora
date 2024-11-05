#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>

#include <SDL3/SDL_video.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

struct GPUBuffer
{
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};
};

struct GPUImage
{
    VkExtent3D extent;
    VkFormat format;
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    VmaAllocationInfo allocation_info;
};

struct Vertex
{
    glm::vec3 position;
    float tex_coord_x;
    glm::vec3 normal;
    float tex_coord_y;
};

struct Mesh
{
    uint32_t index_count;
    GPUBuffer vertex_buffer;
    GPUBuffer index_buffer;
    VkDeviceAddress vertex_buffer_address;

    size_t material_idx;
};

struct Scene
{
    struct Object
    {
        size_t mesh_idx;
    };

    struct Material
    {
        VkDescriptorSet diffuse_set;
        GPUImage diffuse;
    };

    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Object> objects;
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

struct ForwardPushConstants
{
    glm::mat4 camera;
    glm::mat4 model;
    VkDeviceAddress vertex_buffer_address;
};

struct Camera
{
    glm::vec3 eye;
    glm::vec3 forward;
    glm::vec3 up;
    float fov_y;
    float aspect;
    float z_near;
    float z_far;

    [[nodiscard]] glm::mat4 get_matrix() const
    {
        glm::mat4 view = glm::lookAtRH(this->eye, this->eye + this->forward, this->up);
        glm::mat4 proj = glm::perspectiveRH(this->fov_y, this->aspect, this->z_near, this->z_far);
        return proj * view;
    }
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

    uint64_t m_swapchain_generation{0};
    VkExtent2D m_swapchain_extent;
    VkFormat m_swapchain_format;
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> m_swapchain_images;
    std::vector<VkImageView> m_swapchain_image_views;

    VkDescriptorPool m_descriptor_pool;

    GPUImage m_render_target;
    GPUImage m_depth_target;

    VkQueue m_graphics_queue{VK_NULL_HANDLE};
    uint32_t m_graphics_queue_family{0};

    size_t m_frame_idx{0};
    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_frames;

    DeletionQueue m_deletion_queue;
    struct
    {
        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buffer;
        VkFence fence;
    } m_immediate_commands;

    bool m_disable_render{false};

    VkDescriptorSetLayout m_forward_set_layout;
    VkPipelineLayout m_forward_pipeline_layout;
    VkPipeline m_forward_pipeline;

    Camera m_camera{
        .eye = {0.0f, 0.4f, 1.1f},
        .forward = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fov_y = 70.0f,
        .aspect = 16.0f / 9.0f,
        .z_near = 0.1f,
        .z_far = 10000.0f,
    };
    std::array<float, 3> m_background_color{0.1f, 0.1f, 0.1f};
    Scene m_scene;
    float m_scene_rotation{0.0f};

    VkSampler m_sampler;

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
    [[nodiscard]] bool refresh_swapchain();

    [[nodiscard]] bool init_forward_pipeline();
    [[nodiscard]] bool init_imgui();

    void draw_frame(VkCommandBuffer cmd_buffer);
    void draw_imgui(VkCommandBuffer cmd_buffer, VkImageView swapchain_image_view);
    [[nodiscard]] bool render_frame();

    void build_ui();

    [[nodiscard]] bool immediate_submit(std::function<void(VkCommandBuffer)> f);

    [[nodiscard]] bool create_image(
        VmaMemoryUsage memory_usage, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage,
        VkImageAspectFlags aspect_mask, GPUImage *out_image
    );
    [[nodiscard]] bool create_image_from_file(const std::string &path, GPUImage *out_image);
    void destroy_image(GPUImage *image);

    [[nodiscard]] bool create_buffer(
        VmaMemoryUsage memory_usage, VkDeviceSize size, VkBufferUsageFlags usage,
        GPUBuffer *out_buffer
    );
    void destroy_buffer(GPUBuffer *buffer);

    [[nodiscard]] bool
    create_mesh(std::span<Vertex> vertices, std::span<uint32_t> indices, Mesh *out_mesh);
    void destroy_mesh(Mesh *mesh);

    [[nodiscard]] bool
    create_material_from_file(const std::string &diffuse_path, Scene::Material *out_material);
    void destroy_material(Scene::Material *material);

    [[nodiscard]] bool create_scene_from_file(const std::string &path, Scene *out_scene);
    void destroy_scene(Scene *scene);
};
