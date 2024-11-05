#pragma once

#include <vulkan/vulkan_core.h>

#include "deletion_queue.hpp"

class Engine;

class ImGuiPass
{
    DeletionQueue m_deletion_queue;
    Engine &m_engine;

  public:
    explicit ImGuiPass(Engine &engine) : m_engine(engine)
    {
    }

    ~ImGuiPass()
    {
        m_deletion_queue.delete_all();
    }

    [[nodiscard]] bool init();

    void render(VkCommandBuffer cmd_buffer, uint32_t swapchain_image_idx);
};
