#pragma once

#include <vector>

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan_core.h>

class Engine
{
    SDL_Window *m_window;

    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};

    VkExtent2D m_swapchain_extent;
    VkFormat m_swapchain_format;
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> m_swapchain_images;
    std::vector<VkImageView> m_swapchain_image_views;

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

  public:
    static VkBool32 debug_message_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData
    );

    explicit Engine(SDL_Window *window) : m_window(window)
    {
    }

    [[nodiscard]] bool init();
    void release();
    void run();
};
