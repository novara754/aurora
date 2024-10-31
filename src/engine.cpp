#include "engine.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_vulkan.h>

#include <limits>
#include <spdlog/spdlog.h>

#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>

#define VKERR(x, msg)                                                                              \
    if (VkResult result = (x); result != VK_SUCCESS)                                               \
    {                                                                                              \
        spdlog::error(msg ": result = {}", static_cast<int>(result));                              \
        return false;                                                                              \
    }

VkImageSubresourceRange full_image_range(VkImageAspectFlags aspect_mask);

void transition_image(
    VkCommandBuffer cmd_buffer, VkImage image, VkImageLayout src_layout, VkImageLayout dst_layout
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
    spdlog::trace("Engine::init: created vulkan instance");

    if (!SDL_Vulkan_CreateSurface(m_window, vkb_instance.instance, nullptr, &m_surface))
    {
        spdlog::error("Engine::init: failed to create surface from sdl window: {}", SDL_GetError());
        return false;
    }
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
    spdlog::trace("Engine::init: selected vulkan device");

    vkb::SwapchainBuilder swapchain_builder(m_physical_device, m_device, m_surface);
    auto swapchain_ret =
        swapchain_builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT).build();
    if (!swapchain_ret)
    {
        spdlog::error(
            "Engine::init: failed to create vulkan swapchain: {}",
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
    spdlog::trace("Engine::init: created vulkan swapchain");
    spdlog::info(
        "Engine::init: swapchain: format = {}, present_mode = {}, extent = ({}, {}), image_count = "
        "{}",
        static_cast<int>(vkb_swapchain.image_format),
        static_cast<int>(vkb_swapchain.present_mode),
        vkb_swapchain.extent.width,
        vkb_swapchain.extent.width,
        vkb_swapchain.image_count
    );

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

        VkCommandBufferAllocateInfo cmd_buffer_info = {};
        cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_info.commandPool = frame.cmd_pool;
        cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_info.commandBufferCount = 1;

        VKERR(
            vkAllocateCommandBuffers(m_device, &cmd_buffer_info, &frame.cmd_buffer),
            "Engine::init: failed to create frame command buffer"
        );

        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VKERR(
            vkCreateSemaphore(m_device, &semaphore_info, nullptr, &frame.present_semaphore),
            "Engine::init: failed to create frame present semaphore"
        );
        VKERR(
            vkCreateSemaphore(m_device, &semaphore_info, nullptr, &frame.render_semaphore),
            "Engine::init: failed to create frame render semaphore"
        );

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKERR(
            vkCreateFence(m_device, &fence_info, nullptr, &frame.fence),
            "Engine::init: failed to create frame fence"
        );
    }
    spdlog::trace("Engine::init: created per-frame objects");

    return true;
}

void Engine::release()
{
    vkDeviceWaitIdle(m_device);

    for (const auto &frame : m_frames)
    {
        vkDestroySemaphore(m_device, frame.render_semaphore, nullptr);
        vkDestroySemaphore(m_device, frame.present_semaphore, nullptr);
        vkDestroyFence(m_device, frame.fence, nullptr);
        vkFreeCommandBuffers(m_device, frame.cmd_pool, 1, &frame.cmd_buffer);
        vkDestroyCommandPool(m_device, frame.cmd_pool, nullptr);
    }
    for (const auto view : m_swapchain_image_views)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    spdlog::trace("Engine::release: destroyed vulkan swapchain");
    vkDestroyDevice(m_device, nullptr);
    spdlog::trace("Engine::release: destroyed vulkan device");
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    spdlog::trace("Engine::release: destroyed vulkan surface");
    vkb::destroy_debug_utils_messenger(m_instance, m_debug_messenger);
    spdlog::trace("Engine::release: destroyed vulkan debug messenger");
    vkDestroyInstance(m_instance, nullptr);
    spdlog::trace("Engine::release: destroyed vulkan instance");
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
    VkClearColorValue clear_color{
        .float32 = {0.5f, 1.0f, 0.2f, 1.0f},
    };
    transition_image(
        frame.cmd_buffer,
        m_swapchain_images[image_idx],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL
    );
    VkImageSubresourceRange clear_range = full_image_range(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(
        frame.cmd_buffer,
        m_swapchain_images[image_idx],
        VK_IMAGE_LAYOUT_GENERAL,
        &clear_color,
        1,
        &clear_range
    );
    transition_image(
        frame.cmd_buffer,
        m_swapchain_images[image_idx],
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
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
    VKERR(
        vkQueuePresentKHR(m_graphics_queue, &present_info),
        "Engine::render_frame: failed to present"
    );

    return true;
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
        }

        if (!render_frame())
        {
            spdlog::error("Engine::run: failed to render frame");
            return;
        }
    }
    spdlog::trace("Engine::run: exitted main loop");
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
    VkImageAspectFlags aspect_mask = (src_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
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
