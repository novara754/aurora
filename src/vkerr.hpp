#pragma once

#define VKERR(x, msg)                                                                              \
    if (VkResult result = (x); result != VK_SUCCESS)                                               \
    {                                                                                              \
        spdlog::error(msg ": result = {}", static_cast<int>(result));                              \
        return false;                                                                              \
    }
