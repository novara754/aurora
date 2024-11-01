#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <spdlog/spdlog.h>

#include "vkerr.hpp"

struct GPUImage
{
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkExtent3D extent{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};

    bool allocate(
        VkDevice device, VmaAllocator allocator, VmaMemoryUsage memory_usage, VkFormat format,
        VkExtent3D extent, VkImageUsageFlags usage, VkImageAspectFlags aspect_mask
    )
    {
        this->format = format;
        this->extent = extent;

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
                allocator,
                &image_info,
                &allocation_info,
                &this->image,
                &this->allocation,
                &this->allocation_info
            ),
            "GPUImage::create: failed to create image"
        );

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = this->image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = aspect_mask;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        VKERR(
            vkCreateImageView(device, &view_info, nullptr, &this->view),
            "GPUImage::create: failed to create image view"
        );

        return true;
    }

    void release(VkDevice device, VmaAllocator allocator)
    {
        vkDestroyImageView(device, this->view, nullptr);
        vmaDestroyImage(allocator, this->image, this->allocation);
        // vkDestroyImage(device, this->image, nullptr);
        // vmaFreeMemory(allocator, this->allocation);
    }
};
