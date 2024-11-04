#include "engine.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <glm/trigonometric.hpp>
#include <limits>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>

#include <spdlog/spdlog.h>

#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <tiny_obj_loader.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/type_ptr.hpp>

#include "read_file.hpp"
#include "vkerr.hpp"

VkImageSubresourceRange full_image_range(VkImageAspectFlags aspect_mask);

void transition_image(
    VkCommandBuffer cmd_buffer, VkImage image, VkImageLayout src_layout, VkImageLayout dst_layout
);

void blit_image(
    VkCommandBuffer cmd_buffer, VkImage src_image, VkExtent3D src_extent, VkImage dst_image,
    VkExtent3D dst_extent
);

bool Engine::init()
{
    vkb::InstanceBuilder instance_builder;
    auto instance_builder_ret = instance_builder.set_app_name("Aurora")
                                    .set_app_version(0, 1)
                                    .set_engine_name("Aurora")
                                    .set_engine_version(0, 1)
                                    .require_api_version(1, 3)
                                    .request_validation_layers()
                                    .set_debug_callback(Engine::debug_message_callback)
                                    .build();
    if (!instance_builder_ret)
    {
        spdlog::error(
            "Engine::init: failed to create vulkan instance: {}",
            instance_builder_ret.error().message()
        );
        return false;
    }

    vkb::Instance vkb_instance = instance_builder_ret.value();
    m_instance = vkb_instance.instance;
    m_debug_messenger = vkb_instance.debug_messenger;
    m_deletion_queue.add([&] {
        vkb::destroy_debug_utils_messenger(m_instance, m_debug_messenger, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    });
    spdlog::trace("Engine::init: created vulkan instance");

    if (!SDL_Vulkan_CreateSurface(m_window, vkb_instance.instance, nullptr, &m_surface))
    {
        spdlog::error("Engine::init: failed to create surface from sdl window: {}", SDL_GetError());
        return false;
    }
    m_deletion_queue.add([&] { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); });
    spdlog::trace("Engine::init: created vulkan surface from sdl window");

    VkPhysicalDeviceVulkan13Features features_1_3 = {};
    features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features_1_3.dynamicRendering = true;
    features_1_3.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features_1_2 = {};
    features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features_1_2.bufferDeviceAddress = true;
    features_1_2.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector(vkb_instance);
    auto selector_ret = selector.set_surface(m_surface)
                            .set_minimum_version(1, 3)
                            .set_required_features_13(features_1_3)
                            .set_required_features_12(features_1_2)
                            .select();
    if (!selector_ret)
    {
        spdlog::error(
            "Engine::init: failed to select vulkan phyiscal device: {}",
            selector_ret.error().message()
        );
        return false;
    }
    vkb::PhysicalDevice vkb_physical_device = selector_ret.value();
    m_physical_device = vkb_physical_device.physical_device;
    spdlog::trace("Engine::init: selected vulkan physical device");
    spdlog::info("Engine::init: selected physical device: {}", vkb_physical_device.name);

    vkb::DeviceBuilder device_builder{vkb_physical_device};
    auto device_ret = device_builder.build();
    if (!device_ret)
    {
        spdlog::error(
            "Engine::init: failed to create vulkan device: {}",
            device_ret.error().message()
        );
        return false;
    }
    vkb::Device vkb_device = device_ret.value();
    m_device = vkb_device.device;
    m_deletion_queue.add([&] { vkDestroyDevice(m_device, nullptr); });
    spdlog::trace("Engine::init: selected vulkan device");

    VmaAllocatorCreateInfo vma_info = {};
    vma_info.physicalDevice = m_physical_device;
    vma_info.device = m_device;
    vma_info.instance = m_instance;
    vma_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&vma_info, &m_allocator);
    m_deletion_queue.add([&]() { vmaDestroyAllocator(m_allocator); });
    spdlog::trace("Engine::init: created vma allocator");

    if (!init_swapchain())
    {
        spdlog::error("Engine::init: failed to initialize swapchain");
        return false;
    }
    spdlog::trace("Engine::init: initialized swapchain");

    VkImageUsageFlags render_target_usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!create_image(

            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{
                .width = m_swapchain_extent.width,
                .height = m_swapchain_extent.height,
                .depth = 1,
            },
            render_target_usage,
            VK_IMAGE_ASPECT_COLOR_BIT,
            &m_render_target
        ))
    {
        spdlog::error("Engine::init: failed to allocate render target image");
        return false;
    }
    m_deletion_queue.add([&] { destroy_image(&m_render_target); });

    VkImageUsageFlags depth_target_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!create_image(

            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_FORMAT_D32_SFLOAT,
            VkExtent3D{
                .width = m_swapchain_extent.width,
                .height = m_swapchain_extent.height,
                .depth = 1,
            },
            depth_target_usage,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            &m_depth_target
        ))
    {
        spdlog::error("Engine::init: failed to allocate depth target image");
        return false;
    }
    m_deletion_queue.add([&] { destroy_image(&m_depth_target); });

    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret)
    {
        spdlog::error(
            "Engine::init: failed to get vulkan graphics queue: {}",
            graphics_queue_ret.error().message()
        );
        return false;
    }
    m_graphics_queue = graphics_queue_ret.value();
    m_graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();
    spdlog::trace("Engine::init: acquired graphics queue");

    if (!init_forward_pipeline())
    {
        spdlog::error("Engine::init: failed to initialize forward pipeline");
        return false;
    }

    for (auto &frame : m_frames)
    {
        VkCommandPoolCreateInfo cmd_pool_info = {};
        cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmd_pool_info.queueFamilyIndex = m_graphics_queue_family;
        VKERR(
            vkCreateCommandPool(m_device, &cmd_pool_info, nullptr, &frame.cmd_pool),
            "Engine::init: failed to create frame command pool"
        );
        m_deletion_queue.add([&] { vkDestroyCommandPool(m_device, frame.cmd_pool, nullptr); });

        VkCommandBufferAllocateInfo cmd_buffer_info = {};
        cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_info.commandPool = frame.cmd_pool;
        cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_info.commandBufferCount = 1;
        VKERR(
            vkAllocateCommandBuffers(m_device, &cmd_buffer_info, &frame.cmd_buffer),
            "Engine::init: failed to create frame command buffer"
        );
        m_deletion_queue.add([&] {
            vkFreeCommandBuffers(m_device, frame.cmd_pool, 1, &frame.cmd_buffer);
        });

        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VKERR(
            vkCreateSemaphore(m_device, &semaphore_info, nullptr, &frame.present_semaphore),
            "Engine::init: failed to create frame present semaphore"
        );
        m_deletion_queue.add([&] { vkDestroySemaphore(m_device, frame.present_semaphore, nullptr); }
        );
        VKERR(
            vkCreateSemaphore(m_device, &semaphore_info, nullptr, &frame.render_semaphore),
            "Engine::init: failed to create frame render semaphore"
        );
        m_deletion_queue.add([&] { vkDestroySemaphore(m_device, frame.render_semaphore, nullptr); }
        );

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKERR(
            vkCreateFence(m_device, &fence_info, nullptr, &frame.fence),
            "Engine::init: failed to create frame fence"
        );
        m_deletion_queue.add([&] { vkDestroyFence(m_device, frame.fence, nullptr); });
    }
    spdlog::trace("Engine::init: created per-frame objects");

    {
        VkCommandPoolCreateInfo cmd_pool_info = {};
        cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmd_pool_info.queueFamilyIndex = m_graphics_queue_family;
        VKERR(
            vkCreateCommandPool(m_device, &cmd_pool_info, nullptr, &m_immediate_commands.cmd_pool),
            "Engine::init: failed to create immediate submit command pool"
        );
        m_deletion_queue.add([&] {
            vkDestroyCommandPool(m_device, m_immediate_commands.cmd_pool, nullptr);
        });

        VkCommandBufferAllocateInfo cmd_buffer_info = {};
        cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_info.commandPool = m_immediate_commands.cmd_pool;
        cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_info.commandBufferCount = 1;
        VKERR(
            vkAllocateCommandBuffers(m_device, &cmd_buffer_info, &m_immediate_commands.cmd_buffer),
            "Engine::init: failed to allocate immediate submit command buffer"
        );
        m_deletion_queue.add([&] {
            vkFreeCommandBuffers(
                m_device,
                m_immediate_commands.cmd_pool,
                1,
                &m_immediate_commands.cmd_buffer
            );
        });

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VKERR(
            vkCreateFence(m_device, &fence_info, nullptr, &m_immediate_commands.fence),
            "Engine::init: failed to create immediate submit fence"
        );
        m_deletion_queue.add([&] { vkDestroyFence(m_device, m_immediate_commands.fence, nullptr); }
        );
    }

    {
        if (!create_scene_from_file("../meshes/flight_helmet/FlightHelmet.gltf", &m_scene))
        {
            destroy_scene(&m_scene);
            spdlog::error("Engine::init: failed to load flight helmet scene");
            return false;
        }
        m_deletion_queue.add([&] { destroy_scene(&m_scene); });
    }

    if (!init_imgui())
    {
        spdlog::error("Engine::init: failed to initialize ImGui");
        return false;
    }

    return true;
}

bool Engine::init_swapchain()
{
    vkb::SwapchainBuilder swapchain_builder(m_physical_device, m_device, m_surface);
    auto swapchain_ret =
        swapchain_builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT).build();
    if (!swapchain_ret)
    {
        spdlog::error(
            "Engine::init_swapchain: failed to create vulkan swapchain: {}",
            swapchain_ret.error().message()
        );
        return false;
    }
    vkb::Swapchain vkb_swapchain = swapchain_ret.value();
    m_swapchain_format = vkb_swapchain.image_format;
    m_swapchain_extent = vkb_swapchain.extent;
    m_swapchain = vkb_swapchain.swapchain;
    m_swapchain_images = vkb_swapchain.get_images().value();
    m_swapchain_image_views = vkb_swapchain.get_image_views().value();
    uint64_t this_generation = m_swapchain_generation;
    m_deletion_queue.add([&, this_generation] {
        if (m_swapchain_generation == this_generation)
        {
            for (const auto view : m_swapchain_image_views)
            {
                vkDestroyImageView(m_device, view, nullptr);
            }
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        }
    });
    spdlog::trace("Engine::init_swapchain: created vulkan swapchain");
    spdlog::info(
        "Engine::init_swapchain: swapchain: format = {}, present_mode = {}, extent = ({}, {}), "
        "image_count = {}",
        static_cast<int>(vkb_swapchain.image_format),
        static_cast<int>(vkb_swapchain.present_mode),
        vkb_swapchain.extent.width,
        vkb_swapchain.extent.height,
        vkb_swapchain.image_count
    );

    return true;
}

bool Engine::refresh_swapchain()
{
    vkDeviceWaitIdle(m_device);

    for (const auto view : m_swapchain_image_views)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

    m_swapchain_generation += 1;

    return init_swapchain();
}

[[nodiscard]] bool Engine::init_forward_pipeline()
{
    VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ForwardPushConstants),
    };
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 0;
    layout_info.pSetLayouts = nullptr;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;
    VKERR(
        vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_forward_pipeline_layout),
        "Engine::init_forward_pipeline: failed to create pipeline layout"
    );
    m_deletion_queue.add([&] {
        vkDestroyPipelineLayout(m_device, m_forward_pipeline_layout, nullptr);
    });

    std::vector<uint8_t> vertex_code = read_file("../shaders/forward.vert.bin");
    std::vector<uint8_t> fragment_code = read_file("../shaders/forward.frag.bin");

    VkShaderModule vertex_shader;
    VkShaderModuleCreateInfo vertex_info = {};
    vertex_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_info.codeSize = vertex_code.size();
    vertex_info.pCode = reinterpret_cast<uint32_t *>(vertex_code.data());
    VKERR(
        vkCreateShaderModule(m_device, &vertex_info, nullptr, &vertex_shader),
        "Engine::init_forward_pipeline: failed to create vertex shader module"
    );
    m_deletion_queue.add([this, vertex_shader] {
        vkDestroyShaderModule(m_device, vertex_shader, nullptr);
    });

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
        vkCreateShaderModule(m_device, &fragment_info, nullptr, &fragment_shader),
        "Engine::init_forward_pipeline: failed to create fragment shader module"
    );
    m_deletion_queue.add([this, fragment_shader] {
        vkDestroyShaderModule(m_device, fragment_shader, nullptr);
    });

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
    rasterization_state.cullMode = VK_CULL_MODE_NONE;
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
    pipeline_info.layout = m_forward_pipeline_layout;
    VKERR(
        vkCreateGraphicsPipelines(
            m_device,
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &m_forward_pipeline
        ),
        "Engine::init_forward_pipeline: failed to create pipeline"
    );
    m_deletion_queue.add([&] { vkDestroyPipeline(m_device, m_forward_pipeline, nullptr); });
    return true;
}

[[nodiscard]] bool Engine::init_imgui()
{
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
        vkCreateDescriptorPool(m_device, &pool_info, nullptr, &descriptor_pool),
        "Engine::init_imgui: failed to create imgui descriptor pool"
    );

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplSDL3_InitForVulkan(m_window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physical_device;
    init_info.Device = m_device;
    init_info.QueueFamily = m_graphics_queue_family;
    init_info.Queue = m_graphics_queue;
    init_info.DescriptorPool = descriptor_pool;
    init_info.MinImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.ImageCount = 2;
    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_swapchain_format;

    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();

    m_deletion_queue.add([this, descriptor_pool] {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(m_device, descriptor_pool, nullptr);
    });

    return true;
}

Engine::~Engine()
{
    vkDeviceWaitIdle(m_device);

    for (auto &frame : m_frames)
    {
        frame.deletion_queue.delete_all();
    }

    m_deletion_queue.delete_all();
}

void Engine::draw_frame(VkCommandBuffer cmd_buffer)
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
        .float32 = {m_background_color[0], m_background_color[1], m_background_color[2], 1.0f}
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

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forward_pipeline);
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    for (const Scene::Object &obj : m_scene.objects)
    {
        const Mesh &mesh = m_scene.meshes[obj.mesh_idx];
        ForwardPushConstants push_constants{
            .camera = m_camera.get_matrix(),
            .model = glm::rotate(
                glm::mat4(1.0f),
                glm::radians(m_scene_rotation),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            .vertex_buffer_address = mesh.vertex_buffer_address,
        };
        vkCmdPushConstants(
            cmd_buffer,
            m_forward_pipeline_layout,
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

void Engine::draw_imgui(VkCommandBuffer cmd_buffer, VkImageView swapchain_image_view)
{
    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = swapchain_image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = m_swapchain_extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    vkCmdBeginRendering(cmd_buffer, &rendering_info);

    ImDrawData *draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd_buffer);

    vkCmdEndRendering(cmd_buffer);
}

[[nodiscard]] bool Engine::render_frame()
{
    FrameData &frame = m_frames[m_frame_idx];
    m_frame_idx = (m_frame_idx + 1) % NUM_FRAMES_IN_FLIGHT;

    VKERR(
        vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
        "Engine::render_frame: failed to wait on frame fence"
    );
    VKERR(
        vkResetFences(m_device, 1, &frame.fence),
        "Engine::render_frame: failed to reset frame fence"
    );

    frame.deletion_queue.delete_all();

    uint32_t image_idx;
    VKERR(
        vkAcquireNextImageKHR(
            m_device,
            m_swapchain,
            std::numeric_limits<uint64_t>::max(),
            frame.render_semaphore,
            VK_NULL_HANDLE,
            &image_idx
        ),
        "Engine::render_frame: failed to acquire next swapchain image"
    );

    VKERR(
        vkResetCommandBuffer(frame.cmd_buffer, 0),
        "Engine::render_frame: failed to reset command buffer"
    );

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VKERR(
        vkBeginCommandBuffer(frame.cmd_buffer, &begin_info),
        "Engine::render_frame: failed to begin command buffer"
    );
    {
        draw_frame(frame.cmd_buffer);

        transition_image(
            frame.cmd_buffer,
            m_render_target.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        transition_image(
            frame.cmd_buffer,
            m_swapchain_images[image_idx],
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        blit_image(
            frame.cmd_buffer,
            m_render_target.image,
            m_render_target.extent,
            m_swapchain_images[image_idx],
            VkExtent3D{
                .width = m_swapchain_extent.width,
                .height = m_swapchain_extent.height,
                .depth = 1,
            }
        );

        draw_imgui(frame.cmd_buffer, m_swapchain_image_views[image_idx]);

        transition_image(
            frame.cmd_buffer,
            m_swapchain_images[image_idx],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        );
    }
    VKERR(
        vkEndCommandBuffer(frame.cmd_buffer),
        "Engine::render_frame: failed to end command buffer"
    );

    VkCommandBufferSubmitInfo cmd_buffer_submit_info = {};
    cmd_buffer_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buffer_submit_info.commandBuffer = frame.cmd_buffer;

    VkSemaphoreSubmitInfo render_semaphore_submit_info = {};
    render_semaphore_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    render_semaphore_submit_info.semaphore = frame.render_semaphore;
    render_semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;

    VkSemaphoreSubmitInfo present_semaphore_submit_info = {};
    present_semaphore_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    present_semaphore_submit_info.semaphore = frame.present_semaphore;
    present_semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &render_semaphore_submit_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_buffer_submit_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &present_semaphore_submit_info;
    VKERR(
        vkQueueSubmit2(m_graphics_queue, 1, &submit_info, frame.fence),
        "Engine::render_frame: failed to submit render commands"
    );

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &frame.present_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &m_swapchain;
    present_info.pImageIndices = &image_idx;

    VkResult present_res = vkQueuePresentKHR(m_graphics_queue, &present_info);
    if (present_res != VK_SUCCESS)
    {
        if (!refresh_swapchain())
        {
            spdlog::error("Engine::run: failed to recreate swapchain for resize");
            return false;
        }
    }

    return true;
}

void Engine::build_ui()
{
    ImGui::Begin(
        "Settings",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize
    );
    {
        ImGui::SeparatorText("General");
        ImGui::ColorEdit3("Color", m_background_color.data());
        ImGui::SeparatorText("Scene");
        ImGui::SliderFloat("Rotation", &m_scene_rotation, -180.0f, 180.0f);
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat3("Position", glm::value_ptr(m_camera.eye), 0.1f);
        ImGui::DragFloat3("Forward", glm::value_ptr(m_camera.forward), 0.1f);
    }
    ImGui::End();
}

void Engine::run()
{
    spdlog::trace("Engine::run: entering main loop");
    while (true)
    {
        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                spdlog::trace("Engine::run: got quit event");
                return;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                spdlog::trace("Engine::run: got resize event");
                if (!refresh_swapchain())
                {
                    spdlog::error("Engine::run: failed to recreate swapchain for resize");
                    break;
                }
            }
            else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                m_disable_render = true;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESTORED)
            {
                m_disable_render = false;
            }

            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (m_disable_render)
        {
            SDL_Delay(100);
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        build_ui();

        ImGui::Render();

        if (!render_frame())
        {
            spdlog::error("Engine::run: failed to render frame");
            return;
        }
    }
    spdlog::trace("Engine::run: exitted main loop");
}

[[nodiscard]] bool Engine::immediate_submit(std::function<void(VkCommandBuffer)> f)
{
    VKERR(
        vkResetFences(m_device, 1, &m_immediate_commands.fence),
        "Engine::immediate_submit: failed to reset fence"
    );

    VKERR(
        vkResetCommandBuffer(m_immediate_commands.cmd_buffer, 0),
        "Engine::immediate_submit: failed to reset command buffer"
    );

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VKERR(
        vkBeginCommandBuffer(m_immediate_commands.cmd_buffer, &begin_info),
        "Engine::immediate_submit: failed to begin command buffer"
    );

    f(m_immediate_commands.cmd_buffer);

    VKERR(
        vkEndCommandBuffer(m_immediate_commands.cmd_buffer),
        "Engine::immediate_submit: failed to end command buffer"
    );

    VkCommandBufferSubmitInfo cmd_buffer_info = {};
    cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buffer_info.commandBuffer = m_immediate_commands.cmd_buffer;

    VkSubmitInfo2 submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_buffer_info;
    VKERR(
        vkQueueSubmit2(m_graphics_queue, 1, &submit_info, m_immediate_commands.fence),
        "Engine::immediate_submit: failed to submit command buffer"
    );

    VKERR(
        vkWaitForFences(
            m_device,
            1,
            &m_immediate_commands.fence,
            VK_TRUE,
            std::numeric_limits<uint64_t>::max()
        ),
        "Engine::immediate_submit: failed to wait for fence"
    );

    return true;
}

[[nodiscard]] bool Engine::create_image(
    VmaMemoryUsage memory_usage, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage,
    VkImageAspectFlags aspect_mask, GPUImage *out_image
)
{
    out_image->format = format;
    out_image->extent = extent;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = extent;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage = memory_usage;
    allocation_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VKERR(
        vmaCreateImage(
            m_allocator,
            &image_info,
            &allocation_info,
            &out_image->image,
            &out_image->allocation,
            &out_image->allocation_info
        ),
        "Engine::create_image: failed to create image"
    );

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_mask;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (VkResult res = vkCreateImageView(m_device, &view_info, nullptr, &out_image->view);
        res != VK_SUCCESS)
    {
        vmaDestroyImage(m_allocator, out_image->image, out_image->allocation);
        spdlog::error(
            "Engine::create_image: failed to create image view: result = {}",
            static_cast<int>(res)
        );
        return false;
    }

    return true;
}

void Engine::destroy_image(GPUImage *image)
{

    vkDestroyImageView(m_device, image->view, nullptr);
    vmaDestroyImage(m_allocator, image->image, image->allocation);
}

[[nodiscard]] bool Engine::create_buffer(
    VmaMemoryUsage memory_usage, VkDeviceSize size, VkBufferUsageFlags usage, GPUBuffer *out_buffer
)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = memory_usage;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VKERR(
        vmaCreateBuffer(
            m_allocator,
            &buffer_info,
            &alloc_info,
            &out_buffer->buffer,
            &out_buffer->allocation,
            &out_buffer->allocation_info
        ),
        "Engine::create_buffer: failed to create buffer"
    );

    return true;
}

void Engine::destroy_buffer(GPUBuffer *buffer)
{
    vmaDestroyBuffer(m_allocator, buffer->buffer, buffer->allocation);
}

[[nodiscard]] bool
Engine::create_mesh(std::span<Vertex> vertices, std::span<uint32_t> indices, Mesh *out_mesh)
{
    VkDeviceSize vertex_buffer_size = vertices.size() * sizeof(Vertex);
    VkDeviceSize index_buffer_size = indices.size() * sizeof(uint32_t);

    GPUBuffer transfer_buffer;
    if (!create_buffer(
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            vertex_buffer_size + index_buffer_size,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            &transfer_buffer
        ))
    {
        spdlog::error("Engine::create_mesh: failed to allocate transfer buffer");
        return false;
    }

    if (!create_buffer(
            VMA_MEMORY_USAGE_GPU_ONLY,
            vertex_buffer_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            &out_mesh->vertex_buffer
        ))
    {
        destroy_buffer(&transfer_buffer);
        spdlog::error("Engine::create_mesh: failed to allocate vertex buffer");
        return false;
    }

    if (!create_buffer(
            VMA_MEMORY_USAGE_GPU_ONLY,
            index_buffer_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &out_mesh->index_buffer
        ))
    {
        destroy_buffer(&transfer_buffer);
        destroy_buffer(&out_mesh->vertex_buffer);
        spdlog::error("Engine::create_mesh: failed to allocate index buffer");
        return false;
    }

    std::memcpy(transfer_buffer.allocation_info.pMappedData, vertices.data(), vertex_buffer_size);
    std::memcpy(
        reinterpret_cast<uint8_t *>(transfer_buffer.allocation_info.pMappedData) +
            vertex_buffer_size,
        indices.data(),
        index_buffer_size
    );

    if (!immediate_submit([&](VkCommandBuffer cmd_buffer) {
            VkBufferCopy copy_vertex_region{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = vertex_buffer_size,
            };
            vkCmdCopyBuffer(
                cmd_buffer,
                transfer_buffer.buffer,
                out_mesh->vertex_buffer.buffer,
                1,
                &copy_vertex_region
            );

            VkBufferCopy copy_index_region{
                .srcOffset = vertex_buffer_size,
                .dstOffset = 0,
                .size = index_buffer_size,
            };
            vkCmdCopyBuffer(
                cmd_buffer,
                transfer_buffer.buffer,
                out_mesh->index_buffer.buffer,
                1,
                &copy_index_region
            );
        }))
    {
        destroy_buffer(&transfer_buffer);
        destroy_buffer(&out_mesh->vertex_buffer);
        destroy_buffer(&out_mesh->index_buffer);

        spdlog::error("Engine::create_mesh: failed to copy data to vertex buffer");
        return false;
    }

    VkBufferDeviceAddressInfo address_info = {};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = out_mesh->vertex_buffer.buffer;
    out_mesh->vertex_buffer_address = vkGetBufferDeviceAddress(m_device, &address_info);

    out_mesh->index_count = indices.size();

    destroy_buffer(&transfer_buffer);

    return true;
}

void Engine::destroy_mesh(Mesh *mesh)
{
    destroy_buffer(&mesh->vertex_buffer);
    destroy_buffer(&mesh->index_buffer);
}

[[nodiscard]] bool Engine::create_scene_from_file(
    [[maybe_unused]] const std::string &path, [[maybe_unused]] Scene *out_scene
)
{
    Assimp::Importer importer;

    const aiScene *scene =
        importer.ReadFile(path.c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (scene == nullptr)
    {
        spdlog::error("Engine::create_scene_from_file: failed to load file");
        return false;
    }

    if (scene->mRootNode == nullptr)
    {
        spdlog::error("Engine::create_scene_from_file: file has no root node");
        return false;
    }

    for (size_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const aiMesh *ai_mesh = scene->mMeshes[mesh_idx];
        for (size_t vertex_idx = 0; vertex_idx < ai_mesh->mNumVertices; ++vertex_idx)
        {
            vertices.emplace_back(Vertex{
                .position =
                    {
                        ai_mesh->mVertices[vertex_idx].x,
                        ai_mesh->mVertices[vertex_idx].y,
                        ai_mesh->mVertices[vertex_idx].z,
                    },
                .tex_coord_x = 0.0f, // ai_mesh->mTextureCoords[vertex_idx]->x,
                .normal =
                    {
                        ai_mesh->mNormals[vertex_idx].x,
                        ai_mesh->mNormals[vertex_idx].y,
                        ai_mesh->mNormals[vertex_idx].z,
                    },
                .tex_coord_y = 0.0f, // ai_mesh->mTextureCoords[vertex_idx]->y,
            });
        }

        for (size_t face_idx = 0; face_idx < ai_mesh->mNumFaces; ++face_idx)
        {
            const aiFace *face = &ai_mesh->mFaces[face_idx];
            for (size_t index_idx = 0; index_idx < face->mNumIndices; ++index_idx)
            {
                indices.emplace_back(static_cast<uint32_t>(face->mIndices[index_idx]));
            }
        }

        Mesh mesh;
        if (!create_mesh(vertices, indices, &mesh))
        {
            spdlog::error("Engine::create_scene_from_file: failed to create mesh");
            return false;
        }
        out_scene->meshes.emplace_back(mesh);
    }

    std::vector nodes_to_process{scene->mRootNode};
    while (!nodes_to_process.empty())
    {
        const aiNode *node = nodes_to_process.back();
        nodes_to_process.pop_back();

        for (size_t i = 0; i < node->mNumChildren; ++i)
        {
            nodes_to_process.emplace_back(node->mChildren[i]);
        }

        if (node->mNumMeshes == 0)
        {
            continue;
        }

        out_scene->objects.emplace_back(Scene::Object{
            .mesh_idx = node->mMeshes[0],
        });
    }

    return true;
}

void Engine::destroy_scene(Scene *scene)
{
    for (auto &mesh : scene->meshes)
    {
        destroy_mesh(&mesh);
    }

    scene->meshes.clear();
    scene->objects.clear();
}

VkBool32 Engine::debug_message_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *cb_data, [[maybe_unused]] void *user_data
)
{
    const char *type_str = nullptr;
    switch (type)
    {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
            type_str = "GNRL";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
            type_str = "VALI";
            break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
            type_str = "PERF";
            break;
        default:
            break;
    }

    switch (severity)
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            spdlog::info("[{}] {}", type_str, cb_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            spdlog::warn("[{}] {}", type_str, cb_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            spdlog::error("[{}] {}", type_str, cb_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            spdlog::debug("[{}] {}", type_str, cb_data->pMessage);
            break;
        default:
            break;
    }

    return VK_FALSE;
}

void transition_image(
    VkCommandBuffer cmd_buffer, VkImage image, VkImageLayout src_layout, VkImageLayout dst_layout
)
{
    VkImageAspectFlags aspect_mask = (dst_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                         ? VK_IMAGE_ASPECT_DEPTH_BIT
                                         : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier2 image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.image = image;
    image_barrier.subresourceRange = full_image_range(aspect_mask);

    VkDependencyInfo dep_info = {};
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &image_barrier;

    vkCmdPipelineBarrier2(cmd_buffer, &dep_info);
}

VkImageSubresourceRange full_image_range(VkImageAspectFlags aspect_mask)
{
    return VkImageSubresourceRange{
        .aspectMask = aspect_mask,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
}

void blit_image(
    VkCommandBuffer cmd_buffer, VkImage src_image, VkExtent3D src_extent, VkImage dst_image,
    VkExtent3D dst_extent
)
{
    VkImageBlit2 blit = {};
    blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blit.srcSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    // blit.srcOffsets[0] is zeroed
    blit.srcOffsets[1] = {
        .x = static_cast<int32_t>(src_extent.width),
        .y = static_cast<int32_t>(src_extent.height),
        .z = 1,
    };
    blit.dstSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    // blit.dstOffsets[0] is zeroed
    blit.dstOffsets[1] = {
        .x = static_cast<int32_t>(dst_extent.width),
        .y = static_cast<int32_t>(dst_extent.height),
        .z = 1,
    };

    VkBlitImageInfo2 blit_info = {};
    blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blit_info.srcImage = src_image;
    blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit_info.dstImage = dst_image;
    blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit_info.regionCount = 1;
    blit_info.pRegions = &blit;
    blit_info.filter = VK_FILTER_LINEAR;

    vkCmdBlitImage2(cmd_buffer, &blit_info);
}
