/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#ifndef RVULKANCONTEXT_H
#define RVULKANCONTEXT_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <functional>

struct SDL_Window;

//! Vulkan context — manages VkInstance, VkDevice, VkSurface, and queues.
//! This is the foundation all other Vulkan objects depend on.
class rVulkanContext
{
public:
    rVulkanContext();
    ~rVulkanContext();

    //! Initialize Vulkan: create instance, pick device, create logical device
    //! @param window SDL window (needed for surface creation)
    //! @param enableValidation Enable Vulkan validation layers (debug builds)
    [[nodiscard]] bool Init(SDL_Window* window, bool enableValidation = false);

    //! Shut down and release all Vulkan resources
    void Shutdown();

    bool IsValid() const { return device_ != VK_NULL_HANDLE; }

    // Accessors
    VkInstance       GetInstance()       const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }
    VkDevice         GetDevice()         const { return device_; }
    VkSurfaceKHR     GetSurface()        const { return surface_; }
    VkQueue          GetGraphicsQueue()  const { return graphicsQueue_; }
    VkQueue          GetPresentQueue()   const { return presentQueue_; }
    uint32_t         GetGraphicsFamily() const { return graphicsFamily_; }
    uint32_t         GetPresentFamily()  const { return presentFamily_; }

    //! Get physical device properties (name, limits, etc.)
    const VkPhysicalDeviceProperties& GetDeviceProperties() const { return deviceProperties_; }

    //! Get physical device memory properties
    const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return memoryProperties_; }

    //! Find a memory type index matching requirements
    //! @param typeFilter Bitmask of acceptable memory types
    //! @param properties Required memory property flags
    //! @return Memory type index, or UINT32_MAX on failure
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    //! Get renderer string (device name)
    const char* GetDeviceName() const { return deviceProperties_.deviceName; }

    //! Whether VkPhysicalDeviceFeatures::depthBiasClamp was enabled at device creation.
    //! When true, vkCmdSetDepthBias may use a non-zero clamp to limit slope-based bias.
    bool HasDepthBiasClamp() const { return depthBiasClampSupported_; }

    //! VulkanMemoryAllocator handle — use for all image and buffer allocations.
    VmaAllocator GetAllocator() const { return allocator_; }

    // Non-copyable
    rVulkanContext(const rVulkanContext&) = delete;
    rVulkanContext& operator=(const rVulkanContext&) = delete;

private:
    bool CreateInstance(bool enableValidation);
    bool CreateSurface(SDL_Window* window);
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();

    VkInstance               instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_ = VK_NULL_HANDLE;
    VkDevice                 device_         = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_        = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue_  = VK_NULL_HANDLE;
    VkQueue                  presentQueue_   = VK_NULL_HANDLE;
    uint32_t                 graphicsFamily_ = UINT32_MAX;
    uint32_t                 presentFamily_  = UINT32_MAX;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties       deviceProperties_{};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};

    VmaAllocator allocator_ = VK_NULL_HANDLE;

    bool validationEnabled_ = false;
    bool depthBiasClampSupported_ = false;
};

//! Global Vulkan context accessor
rVulkanContext& rGetVulkanContext();

#endif // DEDICATED
#endif // RVULKANCONTEXT_H
