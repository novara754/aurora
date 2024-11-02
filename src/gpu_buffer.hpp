#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <spdlog/spdlog.h>

#include "vkerr.hpp"

struct GPUBuffer
{
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};

    bool allocate(
        VmaAllocator allocator, VmaMemoryUsage memory_usage, VkDeviceSize size,
        VkBufferUsageFlags usage
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
                allocator,
                &buffer_info,
                &alloc_info,
                &this->buffer,
                &this->allocation,
                &this->allocation_info
            ),
            "GPUBuffer::allocate: failed to create buffer"
        );

        return true;
    }

    void release(VmaAllocator allocator)
    {
        vmaDestroyBuffer(allocator, this->buffer, this->allocation);
    }
};
