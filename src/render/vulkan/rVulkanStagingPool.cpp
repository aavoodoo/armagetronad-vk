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

#include "rVulkanStagingPool.h"
#include "rVulkanContext.h"
#include <algorithm>
#include <iostream>

bool rVulkanStagingPool::Init(rVulkanContext& ctx)
{
    ctx_ = &ctx;
    return true;
}

void rVulkanStagingPool::Destroy()
{
    if (!ctx_) return;
    VmaAllocator vma = ctx_->GetAllocator();
    for (auto& ring : rings_)
    {
        for (auto& ob : ring.oldBuffers)
            vmaDestroyBuffer(vma, ob.buffer, ob.allocation);
        ring.oldBuffers.clear();
        if (ring.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(vma, ring.buffer, ring.allocation);
            ring.buffer     = VK_NULL_HANDLE;
            ring.allocation = VK_NULL_HANDLE;
        }
        ring.capacity = ring.used = 0;
        ring.mapped   = nullptr;
    }
    ctx_ = nullptr;
}

void rVulkanStagingPool::Reset(uint32_t frameSlot)
{
    FrameRing& ring = rings_[frameSlot % kFrames];
    VmaAllocator vma = ctx_->GetAllocator();
    for (auto& ob : ring.oldBuffers)
        vmaDestroyBuffer(vma, ob.buffer, ob.allocation);
    ring.oldBuffers.clear();
    ring.used = 0;
}

bool rVulkanStagingPool::Acquire(uint32_t frameSlot, VkDeviceSize size, Allocation& out)
{
    FrameRing& ring = rings_[frameSlot % kFrames];
    // Align to 256 bytes — satisfies all VkBufferImageCopy bufferOffset requirements.
    const VkDeviceSize kAlign = 256;
    VkDeviceSize aligned = (ring.used + kAlign - 1) & ~(kAlign - 1);
    if (aligned + size > ring.capacity)
    {
        if (!GrowRing(ring, aligned + size))
            return false;
    }
    out.buffer = ring.buffer;
    out.offset = aligned;
    out.mapped = static_cast<char*>(ring.mapped) + aligned;
    ring.used  = aligned + size;
    return true;
}

bool rVulkanStagingPool::GrowRing(FrameRing& ring, VkDeviceSize required)
{
    VkDeviceSize newCap = std::max(kInitial, ring.capacity * 2);
    while (newCap < required) newCap *= 2;

    VmaAllocator vma = ctx_->GetAllocator();
    if (ring.buffer != VK_NULL_HANDLE)
    {
        // Defer destruction — pending uploads in this frame still reference this buffer.
        // Reset() will drain oldBuffers after the frame fence is signaled.
        ring.oldBuffers.push_back({ring.buffer, ring.allocation});
        ring.buffer     = VK_NULL_HANDLE;
        ring.allocation = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bufCI{};
    bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size        = newCap;
    bufCI.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaCI{};
    vmaCI.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo vmaInfo{};
    if (vmaCreateBuffer(vma, &bufCI, &vmaCI,
                        &ring.buffer, &ring.allocation, &vmaInfo) != VK_SUCCESS)
    {
        std::cerr << "[StagingPool] vmaCreateBuffer failed for " << newCap << " bytes\n";
        return false;
    }
    ring.capacity = newCap;
    ring.mapped   = vmaInfo.pMappedData;
    return true;
}

#endif // DEDICATED
