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
    VkDevice device = ctx.GetDevice();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer.buffer) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create buffer" << std::endl;
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, outBuffer.buffer, &memReqs);

    uint32_t memType = ctx.FindMemoryType(memReqs.memoryTypeBits, memProps);
    if (memType == UINT32_MAX)
    {
        std::cerr << "[Vulkan] Failed to find suitable memory type" << std::endl;
        vkDestroyBuffer(device, outBuffer.buffer, nullptr);
        outBuffer.buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outBuffer.memory) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to allocate buffer memory" << std::endl;
        vkDestroyBuffer(device, outBuffer.buffer, nullptr);
        outBuffer.buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device, outBuffer.buffer, outBuffer.memory, 0);
    outBuffer.size = size;
    return true;
}

void rVulkanBufferManager::DestroyBuffer(VkDevice device, rVulkanBuffer& buffer)
{
    if (buffer.mapped)
    {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (buffer.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
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
        void* mapped;
        vkMapMemory(ctx.GetDevice(), outBuffer.memory, 0, size, 0, &mapped);
        memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(ctx.GetDevice(), outBuffer.memory);
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
        DestroyBuffer(ctx.GetDevice(), staging);
        return false;
    }

    // Copy via command buffer
    VkCommandBuffer cmd = BeginSingleTimeCommands(ctx.GetDevice(), cmdPool);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer, outBuffer.buffer, 1, &copyRegion);

    EndSingleTimeCommands(ctx.GetDevice(), cmdPool, ctx.GetGraphicsQueue(), cmd);

    DestroyBuffer(ctx.GetDevice(), staging);
    return true;
}

bool rVulkanBufferManager::UploadToBuffer(VkDevice device, rVulkanBuffer& buffer,
                                          const void* data, VkDeviceSize size,
                                          VkDeviceSize offset)
{
    void* mapped;
    if (vkMapMemory(device, buffer.memory, offset, size, 0, &mapped) != VK_SUCCESS)
        return false;
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, buffer.memory);
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
