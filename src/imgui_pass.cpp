#include "imgui_pass.hpp"

#include <vulkan/vulkan_core.h>

#include <spdlog/spdlog.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "engine.hpp"
#include "vkerr.hpp"

[[nodiscard]] bool ImGuiPass::init()
{
    spdlog::trace("ImGuiPass::init: initializing imgui render pass");

    std::array pool_sizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };

    VkDescriptorPool descriptor_pool;
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = pool_sizes.size();
    pool_info.pPoolSizes = pool_sizes.data();
    VKERR(
        vkCreateDescriptorPool(m_engine.get_device(), &pool_info, nullptr, &descriptor_pool),
        "ImGuiPass::init: failed to create descriptor pool"
    );
    spdlog::trace("ImGuiPass::init: created descriptor pool");

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplSDL3_InitForVulkan(m_engine.get_window());
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_engine.get_instance();
    init_info.PhysicalDevice = m_engine.get_physical_device();
    init_info.Device = m_engine.get_device();
    init_info.QueueFamily = m_engine.get_queue_family();
    init_info.Queue = m_engine.get_queue();
    init_info.DescriptorPool = descriptor_pool;
    init_info.MinImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.ImageCount = 2;
    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats =
        &m_engine.get_swapchain().format;

    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();

    m_deletion_queue.add([this, descriptor_pool] {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(m_engine.get_device(), descriptor_pool, nullptr);
    });

    spdlog::trace("ImGuiPass::init: initialization complete");
    return true;
}

void ImGuiPass::render(VkCommandBuffer cmd_buffer, uint32_t swapchain_image_idx)
{
    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = m_engine.get_swapchain().image_views[swapchain_image_idx];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = m_engine.get_swapchain().extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    vkCmdBeginRendering(cmd_buffer, &rendering_info);

    ImDrawData *draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd_buffer);

    vkCmdEndRendering(cmd_buffer);
}
