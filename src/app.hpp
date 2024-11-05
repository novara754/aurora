#pragma once

#include <span>

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_core.h>

#include "deletion_queue.hpp"
#include "engine.hpp"
#include "forward_pass.hpp"
#include "imgui_pass.hpp"

class App
{
    DeletionQueue m_deletion_queue;

    Engine m_engine;

    ForwardPass m_forward_pass;
    ImGuiPass m_imgui_pass;

    double m_last_frame_time{0.0};
    double m_delta_time{0.0};

    bool m_disable_render{false};

    Scene m_scene{
        .background_color{0.1f, 0.1f, 0.1f},
        .camera{
            .eye = {-820.0f, 145.0f, -0.0f},
            .rotation = {14.0f, 0.0f, 0.0f},
            .up = {0.0f, 1.0f, 0.0f},
            .fov_y = 70.0f,
            .aspect = 16.0f / 9.0f,
            .z_near = 0.1f,
            .z_far = 10000.0f,
        },
        .meshes{},
        .materials{},
        .objects{},
    };

    VkSampler m_sampler;

    App() = delete;
    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;

  public:
    explicit App(SDL_Window *window)
        : m_engine(window), m_forward_pass(m_engine), m_imgui_pass(m_engine)
    {
    }

    ~App()
    {
        vkDeviceWaitIdle(m_engine.get_device());
        m_deletion_queue.delete_all();
    }

  public:
    [[nodiscard]] bool init();

    void run();

  private:
    void build_ui();

    [[nodiscard]] bool render_frame();

    [[nodiscard]] bool
    create_mesh(std::span<Vertex> vertices, std::span<uint32_t> indices, Mesh &out_mesh);
    void destroy_mesh(Mesh &mesh);

    [[nodiscard]] bool create_material_from_file(
        const std::string &diffuse_path, VkDescriptorSetLayout set_layout, Material &out_material
    );
    void destroy_material(Material &material);

    [[nodiscard]] bool create_scene_from_file(const std::string &path, Scene &out_scene);
    void destroy_scene(Scene &scene);
};
