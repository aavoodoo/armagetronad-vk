/*
 * rVulkanHelpers.h — Small utility macros for Vulkan resource management.
 */

#ifndef RVULKANHELPERS_H
#define RVULKANHELPERS_H

#include <vulkan/vulkan.h>

// Safely destroy a Vulkan handle and reset it to VK_NULL_HANDLE.
// Per Vulkan spec, destroying VK_NULL_HANDLE is a no-op, so the guard
// is technically unnecessary — but it avoids the function call overhead
// and makes intent explicit.
#define VK_DESTROY(func, dev, handle) do { \
    if ((handle) != VK_NULL_HANDLE) { \
        func((dev), (handle), nullptr); \
        (handle) = VK_NULL_HANDLE; \
    } \
} while(0)

// Variant for vkFreeMemory (same signature as vkDestroy*)
#define VK_FREE_MEMORY(dev, handle) VK_DESTROY(vkFreeMemory, dev, handle)

#endif // RVULKANHELPERS_H
