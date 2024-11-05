#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

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
