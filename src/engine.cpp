#include "engine.hpp"

#include <array>
#include <cstdint>
#include <cstring>
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

#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include <stb_image.h>

#include "vkerr.hpp"

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
    m_deletion_queue.add([&]() {
        char *stats;
        vmaBuildStatsString(m_allocator, &stats, VK_FALSE);
        spdlog::debug("stats = {}", stats);
        vmaDestroyAllocator(m_allocator);
    });
    spdlog::trace("Engine::init: created vma allocator");

    if (!init_swapchain())
    {
        spdlog::error("Engine::init: failed to initialize swapchain");
        return false;
    }
    spdlog::trace("Engine::init: initialized swapchain");

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

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 100;
        pool_info.poolSizeCount = pool_sizes.size();
        pool_info.pPoolSizes = pool_sizes.data();
        VKERR(
            vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptor_pool),
            "Engine::init: failed to create descriptor pool"
        );
        m_deletion_queue.add([&] { vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr); }
        );
    }

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
    m_swapchain.format = vkb_swapchain.image_format;
    m_swapchain.extent = vkb_swapchain.extent;
    m_swapchain.swapchain = vkb_swapchain.swapchain;
    m_swapchain.images = vkb_swapchain.get_images().value();
    m_swapchain.image_views = vkb_swapchain.get_image_views().value();
    uint64_t this_generation = m_swapchain.generation;
    m_deletion_queue.add([&, this_generation] {
        if (m_swapchain.generation == this_generation)
        {
            for (const auto view : m_swapchain.image_views)
            {
                vkDestroyImageView(m_device, view, nullptr);
            }
            vkDestroySwapchainKHR(m_device, m_swapchain.swapchain, nullptr);
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

    for (const auto view : m_swapchain.image_views)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swapchain.swapchain, nullptr);

    m_swapchain.generation += 1;

    return init_swapchain();
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

[[nodiscard]] bool
Engine::start_frame(VkCommandBuffer &out_cmd_buffer, uint32_t &swapchain_image_idx)
{
    FrameData &frame = m_frames[m_frame_idx];

    VKERR(
        vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
        "Engine::start_frame: failed to wait on frame fence"
    );
    VKERR(
        vkResetFences(m_device, 1, &frame.fence),
        "Engine::start_frame: failed to reset frame fence"
    );

    frame.deletion_queue.delete_all();

    VKERR(
        vkAcquireNextImageKHR(
            m_device,
            m_swapchain.swapchain,
            std::numeric_limits<uint64_t>::max(),
            frame.render_semaphore,
            VK_NULL_HANDLE,
            &swapchain_image_idx
        ),
        "Engine::start_frame: failed to acquire next swapchain image"
    );

    VKERR(
        vkResetCommandBuffer(frame.cmd_buffer, 0),
        "Engine::start_frame: failed to reset command buffer"
    );

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VKERR(
        vkBeginCommandBuffer(frame.cmd_buffer, &begin_info),
        "Engine::start_frame: failed to begin command buffer"
    );

    out_cmd_buffer = frame.cmd_buffer;

    return true;
}

[[nodiscard]] bool Engine::finish_frame(uint32_t swapchain_image_idx)
{
    FrameData &frame = m_frames[m_frame_idx];

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
    present_info.pSwapchains = &m_swapchain.swapchain;
    present_info.pImageIndices = &swapchain_image_idx;

    VkResult present_res = vkQueuePresentKHR(m_graphics_queue, &present_info);
    if (present_res != VK_SUCCESS)
    {
        if (!refresh_swapchain())
        {
            spdlog::error("Engine::run: failed to recreate swapchain for resize");
            return false;
        }
    }

    m_frame_idx = (m_frame_idx + 1) % NUM_FRAMES_IN_FLIGHT;

    return true;
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
    VkImageAspectFlags aspect_mask, GPUImage &out_image
)
{
    out_image.format = format;
    out_image.extent = extent;

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
            &out_image.image,
            &out_image.allocation,
            &out_image.allocation_info
        ),
        "Engine::create_image: failed to create image"
    );

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_mask;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (VkResult res = vkCreateImageView(m_device, &view_info, nullptr, &out_image.view);
        res != VK_SUCCESS)
    {
        vmaDestroyImage(m_allocator, out_image.image, out_image.allocation);
        spdlog::error(
            "Engine::create_image: failed to create image view: result = {}",
            static_cast<int>(res)
        );
        return false;
    }

    return true;
}

[[nodiscard]] bool Engine::create_image_from_file(
    [[maybe_unused]] const std::string &path, [[maybe_unused]] GPUImage &out_image
)
{
    int channels = 4;
    int width, height;
    stbi_uc *image_data = stbi_load(path.c_str(), &width, &height, nullptr, channels);
    if (image_data == nullptr)
    {
        spdlog::error("Engine::create_image_from_file: failed to load image data from file");
        return false;
    }
    VkExtent3D extent{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .depth = 1,
    };

    if (!create_image(
            VMA_MEMORY_USAGE_GPU_ONLY,
            VK_FORMAT_R8G8B8A8_SRGB,
            extent,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            out_image
        ))
    {
        spdlog::error("Engine::create_image_from_file: failed to create gpu image");
        stbi_image_free(image_data);
        return false;
    }

    GPUBuffer transfer_buffer;
    if (!create_buffer(
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            width * height * channels,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            transfer_buffer
        ))
    {
        stbi_image_free(image_data);
        destroy_image(out_image);
        spdlog::error("Engine::create_image: failed to allocate transfer buffer");
        return false;
    }

    std::memcpy(transfer_buffer.allocation_info.pMappedData, image_data, width * height * channels);

    bool upload_success = immediate_submit([&](VkCommandBuffer cmd_buffer) {
        transition_image(
            cmd_buffer,
            out_image.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        VkBufferImageCopy region = {};
        region.imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        region.imageExtent = extent;
        vkCmdCopyBufferToImage(
            cmd_buffer,
            transfer_buffer.buffer,
            out_image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );
        transition_image(
            cmd_buffer,
            out_image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
        );
    });
    if (!upload_success)
    {
        destroy_image(out_image);
        destroy_buffer(transfer_buffer);
        stbi_image_free(image_data);

        spdlog::error("Engine::create_image_from_file: failed to transfer image data");
        return false;
    }

    destroy_buffer(transfer_buffer);
    stbi_image_free(image_data);

    return true;
}

void Engine::destroy_image(GPUImage &image)
{
    vkDestroyImageView(m_device, image.view, nullptr);
    vmaDestroyImage(m_allocator, image.image, image.allocation);
}

[[nodiscard]] bool Engine::create_buffer(
    VmaMemoryUsage memory_usage, VkDeviceSize size, VkBufferUsageFlags usage, GPUBuffer &out_buffer
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
            &out_buffer.buffer,
            &out_buffer.allocation,
            &out_buffer.allocation_info
        ),
        "Engine::create_buffer: failed to create buffer"
    );

    return true;
}

void Engine::destroy_buffer(GPUBuffer &buffer)
{
    vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
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
