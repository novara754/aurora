#include "forward_pass.hpp"

#include <spdlog/spdlog.h>

#include "engine.hpp"
#include "read_file.hpp"
#include "vkerr.hpp"

[[nodiscard]] bool ForwardPass::init()
{
    spdlog::trace("ForwardPass::init: initializing forward render pass");

    VkImageUsageFlags render_target_usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!m_engine.create_image(
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{
                .width = m_engine.get_swapchain().extent.width,
                .height = m_engine.get_swapchain().extent.height,
                .depth = 1,
            },
            render_target_usage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            m_render_target
        ))
    {
        spdlog::error("ForwardPass::init: failed to allocate render target image");
        return false;
    }
    m_deletion_queue.add([this] { m_engine.destroy_image(m_render_target); });
    spdlog::trace("ForwardPass::init: created render color target");

    VkImageUsageFlags depth_target_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!m_engine.create_image(

            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_FORMAT_D32_SFLOAT,
            VkExtent3D{
                .width = m_engine.get_swapchain().extent.width,
                .height = m_engine.get_swapchain().extent.height,
                .depth = 1,
            },
            depth_target_usage,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            m_depth_target
        ))
    {
        spdlog::error("ForwardPass::init: failed to allocate depth target image");
        return false;
    }
    m_deletion_queue.add([this] { m_engine.destroy_image(m_depth_target); });
    spdlog::trace("ForwardPass::init: created render depth target");

    VkDescriptorSetLayoutBinding diffuse_binding = {};
    diffuse_binding.binding = 0;
    diffuse_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    diffuse_binding.descriptorCount = 1;
    diffuse_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo set_layout_info = {};
    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &diffuse_binding;
    VKERR(
        vkCreateDescriptorSetLayout(
            m_engine.get_device(),
            &set_layout_info,
            nullptr,
            &m_set_layout
        ),
        "ForwardPass::init: failed to create descriptor set layout"
    );
    m_deletion_queue.add([this] {
        vkDestroyDescriptorSetLayout(m_engine.get_device(), m_set_layout, nullptr);
    });
    spdlog::trace("ForwardPass::init: created descriptor set layout");

    VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ForwardPushConstants),
    };
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;
    VKERR(
        vkCreatePipelineLayout(m_engine.get_device(), &layout_info, nullptr, &m_pipeline_layout),
        "ForwardPass::init: failed to create pipeline layout"
    );
    m_deletion_queue.add([this] {
        vkDestroyPipelineLayout(m_engine.get_device(), m_pipeline_layout, nullptr);
    });
    spdlog::trace("ForwardPass::init: created pipeline layout");

    std::vector<uint8_t> vertex_code = read_file("../shaders/forward.vert.bin");
    std::vector<uint8_t> fragment_code = read_file("../shaders/forward.frag.bin");
    spdlog::trace("ForwardPass::init: read vertex and fragment shader");

    VkShaderModule vertex_shader;
    VkShaderModuleCreateInfo vertex_info = {};
    vertex_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_info.codeSize = vertex_code.size();
    vertex_info.pCode = reinterpret_cast<uint32_t *>(vertex_code.data());
    VKERR(
        vkCreateShaderModule(m_engine.get_device(), &vertex_info, nullptr, &vertex_shader),
        "ForwardPass::init: failed to create vertex shader module"
    );
    m_deletion_queue.add([this, vertex_shader] {
        vkDestroyShaderModule(m_engine.get_device(), vertex_shader, nullptr);
    });
    spdlog::trace("ForwardPass::init: created vertex shader module");

    VkPipelineShaderStageCreateInfo vertex_stage = {};
    vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage.module = vertex_shader;
    vertex_stage.pName = "main";

    VkShaderModule fragment_shader;
    VkShaderModuleCreateInfo fragment_info = {};
    fragment_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_info.codeSize = fragment_code.size();
    fragment_info.pCode = reinterpret_cast<uint32_t *>(fragment_code.data());
    VKERR(
        vkCreateShaderModule(m_engine.get_device(), &fragment_info, nullptr, &fragment_shader),
        "ForwardPass::init: failed to create fragment shader module"
    );
    m_deletion_queue.add([this, fragment_shader] {
        vkDestroyShaderModule(m_engine.get_device(), fragment_shader, nullptr);
    });
    spdlog::trace("ForwardPass::init: created fragment shader module");

    VkPipelineShaderStageCreateInfo fragment_stage = {};
    fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_stage.module = fragment_shader;
    fragment_stage.pName = "main";

    std::array stages{vertex_stage, fragment_stage};

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state = {};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState color_blend_attachment_state{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &color_blend_attachment_state;

    std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = dynamic_states.size();
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineRenderingCreateInfo pipeline_rendering_info = {};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = &m_render_target.format;
    pipeline_rendering_info.depthAttachmentFormat = m_depth_target.format;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &pipeline_rendering_info;
    pipeline_info.stageCount = stages.size();
    pipeline_info.pStages = stages.data();
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly_state;
    pipeline_info.pTessellationState = nullptr;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization_state;
    pipeline_info.pMultisampleState = &multisample_state;
    pipeline_info.pDepthStencilState = &depth_stencil_state;
    pipeline_info.pColorBlendState = &color_blend_state;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = m_pipeline_layout;
    VKERR(
        vkCreateGraphicsPipelines(
            m_engine.get_device(),
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &m_pipeline
        ),
        "ForwardPass::init: failed to create pipeline"
    );
    m_deletion_queue.add([this] { vkDestroyPipeline(m_engine.get_device(), m_pipeline, nullptr); });
    spdlog::trace("ForwardPass::init: created graphics pipeline");

    spdlog::trace("ForwardPass::init: initializion complete");

    return true;
}

void ForwardPass::render(VkCommandBuffer cmd_buffer, const Scene &scene)
{
    transition_image(
        cmd_buffer,
        m_render_target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL
    );
    transition_image(
        cmd_buffer,
        m_depth_target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
    );

    VkClearColorValue clear_color{
        .float32 =
            {
                scene.background_color[0],
                scene.background_color[1],
                scene.background_color[2],
                1.0f,
            }
    };
    VkImageSubresourceRange clear_range = full_image_range(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(
        cmd_buffer,
        m_render_target.image,
        VK_IMAGE_LAYOUT_GENERAL,
        &clear_color,
        1,
        &clear_range
    );

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = m_render_target.view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = m_depth_target.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = VkExtent2D{
        .width = m_render_target.extent.width,
        .height = m_render_target.extent.height,
    };
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;
    vkCmdBeginRendering(cmd_buffer, &rendering_info);

    VkViewport viewport{
        .x = 0.0f,
        .y = static_cast<float>(m_render_target.extent.height),
        .width = static_cast<float>(m_render_target.extent.width),
        .height = -static_cast<float>(m_render_target.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor{
        .offset = {0, 0},
        .extent =
            VkExtent2D{
                .width = m_render_target.extent.width,
                .height = m_render_target.extent.height,
            },
    };

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    for (const Object &obj : scene.objects)
    {
        const Mesh &mesh = scene.meshes[obj.mesh_idx];

        ForwardPushConstants push_constants{
            .camera = scene.camera.get_matrix(),
            .vertex_buffer_address = mesh.vertex_buffer_address,
        };
        vkCmdBindDescriptorSets(
            cmd_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline_layout,
            0,
            1,
            &scene.materials[mesh.material_idx].diffuse_set,
            0,
            nullptr
        );
        vkCmdPushConstants(
            cmd_buffer,
            m_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(ForwardPushConstants),
            &push_constants
        );
        vkCmdBindIndexBuffer(cmd_buffer, mesh.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_buffer, mesh.index_count, 1, 0, 0, 0);
    }

    vkCmdEndRendering(cmd_buffer);
}
