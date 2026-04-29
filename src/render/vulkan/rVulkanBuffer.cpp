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

#include "aa_config.h"

#ifndef DEDICATED

#include "rVulkanBuffer.h"
#include "rVulkanContext.h"
#include <cstring>
#include <iostream>

bool rVulkanBufferManager::CreateBuffer(rVulkanContext& ctx,
                                        VkDeviceSize size,
                                        VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags memProps,
                                        rVulkanBuffer& outBuffer)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocCI{};
    if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
        vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    if (vmaCreateBuffer(ctx.GetAllocator(), &bufferInfo, &vmaAllocCI,
                        &outBuffer.buffer, &outBuffer.allocation, nullptr) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] VMA: failed to create buffer\n";
        return false;
    }

    outBuffer.size = size;
    return true;
}

void rVulkanBufferManager::DestroyBuffer(VmaAllocator allocator, rVulkanBuffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
    {
        // VMA handles unmap internally — no need to call vmaUnmapMemory for non-persistent mappings.
        // For persistent mappings (mapped != nullptr), VMA also handles cleanup on destroy.
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
        buffer.buffer     = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
    }
    buffer.mapped = nullptr;
    buffer.size   = 0;
}

bool rVulkanBufferManager::CreateStagingBuffer(rVulkanContext& ctx,
                                               const void* data, VkDeviceSize size,
                                               rVulkanBuffer& outBuffer)
{
    if (!CreateBuffer(ctx, size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      outBuffer))
        return false;

    if (data)
    {
        if (vmaCopyMemoryToAllocation(ctx.GetAllocator(), data,
                                      outBuffer.allocation, 0, size) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] VMA: failed to copy to staging buffer\n";
            DestroyBuffer(ctx.GetAllocator(), outBuffer);
            return false;
        }
    }

    return true;
}

bool rVulkanBufferManager::CreateDeviceBuffer(rVulkanContext& ctx,
                                              VkCommandPool cmdPool,
                                              const void* data, VkDeviceSize size,
                                              VkBufferUsageFlags usage,
                                              rVulkanBuffer& outBuffer)
{
    // Create staging buffer
    rVulkanBuffer staging;
    if (!CreateStagingBuffer(ctx, data, size, staging))
        return false;

    // Create device-local buffer
    if (!CreateBuffer(ctx, size,
                      usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      outBuffer))
    {
        DestroyBuffer(ctx.GetAllocator(), staging);
        return false;
    }

    // Copy via command buffer
    VkCommandBuffer cmd = BeginSingleTimeCommands(ctx.GetDevice(), cmdPool);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer, outBuffer.buffer, 1, &copyRegion);

    EndSingleTimeCommands(ctx.GetDevice(), cmdPool, ctx.GetGraphicsQueue(), cmd);

    DestroyBuffer(ctx.GetAllocator(), staging);
    return true;
}

bool rVulkanBufferManager::UploadToBuffer(VmaAllocator allocator, rVulkanBuffer& buffer,
                                          const void* data, VkDeviceSize size,
                                          VkDeviceSize offset)
{
    if (buffer.mapped)
    {
        memcpy(static_cast<char*>(buffer.mapped) + offset, data, static_cast<size_t>(size));
        return true;
    }
    void* mapped;
    if (vmaMapMemory(allocator, buffer.allocation, &mapped) != VK_SUCCESS)
        return false;
    memcpy(static_cast<char*>(mapped) + offset, data, static_cast<size_t>(size));
    vmaUnmapMemory(allocator, buffer.allocation);
    return true;
}

VkCommandBuffer rVulkanBufferManager::BeginSingleTimeCommands(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

void rVulkanBufferManager::EndSingleTimeCommands(VkDevice device, VkCommandPool pool,
                                                 VkQueue queue, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

void rVulkanBufferManager::EndSingleTimeCommandsAsync(VkDevice device, VkCommandPool pool,
                                                      VkQueue queue, VkCommandBuffer cmd,
                                                      VkFence fence)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // Submit with fence — caller polls/waits on the fence, then frees cmd + fence.
    vkQueueSubmit(queue, 1, &submitInfo, fence);
    // No vkQueueWaitIdle — caller manages lifetime via fence.
}

#endif // DEDICATED
