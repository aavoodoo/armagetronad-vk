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

#ifndef RVULKANBUFFER_H
#define RVULKANBUFFER_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>

class rVulkanContext;

//! RAII wrapper for a VkBuffer + VmaAllocation pair
struct rVulkanBuffer
{
    VkBuffer       buffer     = VK_NULL_HANDLE;
    VmaAllocation  allocation = VK_NULL_HANDLE;
    VkDeviceSize   size       = 0;
    void*          mapped     = nullptr;  // Non-null if persistently mapped

    bool IsValid() const { return buffer != VK_NULL_HANDLE; }
};

//! Vulkan buffer creation and management utilities
class rVulkanBufferManager
{
public:
    //! Create a buffer with the given usage and memory properties
    static bool CreateBuffer(rVulkanContext& ctx,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags memProps,
                             rVulkanBuffer& outBuffer);

    //! Destroy a buffer and free its allocation
    static void DestroyBuffer(VmaAllocator allocator, rVulkanBuffer& buffer);

    //! Create a staging buffer (host visible + coherent) with data
    static bool CreateStagingBuffer(rVulkanContext& ctx,
                                    const void* data, VkDeviceSize size,
                                    rVulkanBuffer& outBuffer);

    //! Create a device-local buffer and upload data via staging
    static bool CreateDeviceBuffer(rVulkanContext& ctx,
                                   VkCommandPool cmdPool,
                                   const void* data, VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   rVulkanBuffer& outBuffer);

    //! Upload data to a host-visible buffer (map → memcpy → unmap, or direct if persistently mapped)
    static bool UploadToBuffer(VmaAllocator allocator, rVulkanBuffer& buffer,
                               const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    //! Execute a one-shot command buffer synchronously (blocks until GPU is done).
    //! Use only at init time or when GPU idle is already required.
    static VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool pool);
    static void EndSingleTimeCommands(VkDevice device, VkCommandPool pool,
                                      VkQueue queue, VkCommandBuffer cmd);

    //! Submit a one-shot command buffer asynchronously — does NOT wait for the GPU.
    //! The provided fence (must be unsignaled) will be signaled when the GPU finishes.
    //! The caller is responsible for waiting on the fence, freeing the command buffer,
    //! and destroying the fence when done.
    static void EndSingleTimeCommandsAsync(VkDevice device, VkCommandPool pool,
                                           VkQueue queue, VkCommandBuffer cmd,
                                           VkFence fence);
};

#endif // DEDICATED
#endif // RVULKANBUFFER_H
