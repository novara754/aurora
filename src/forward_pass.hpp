#pragma once

#include <vulkan/vulkan_core.h>

#include "deletion_queue.hpp"
#include "gpu.hpp"
#include "scene.hpp"

class Engine;

class ForwardPass
{
    DeletionQueue m_deletion_queue;

    Engine &m_engine;

    VkDescriptorSetLayout m_set_layout;
    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;

    GPUImage m_render_target;
    GPUImage m_depth_target;

    ForwardPass() = delete;
    ForwardPass(const ForwardPass &) = delete;
    ForwardPass &operator=(const ForwardPass &) = delete;
    ForwardPass(ForwardPass &&) = delete;
    ForwardPass &operator=(ForwardPass &&) = delete;

  public:
    explicit ForwardPass(Engine &engine) : m_engine(engine)
    {
    }

    ~ForwardPass()
    {
        m_deletion_queue.delete_all();
    }

    const GPUImage &get_output_image() const
    {
        return m_render_target;
    }

    VkDescriptorSetLayout get_descriptor_set_layout() const
    {
        return m_set_layout;
    }

    [[nodiscard]] bool init();

    void render(VkCommandBuffer cmd_buffer, const Scene &scene);
};
