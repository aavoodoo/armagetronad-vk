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

#ifndef RVULKANSTAGINGPOOL_H
#define RVULKANSTAGINGPOOL_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <cstdint>
#include <vector>

class rVulkanContext;

//! Per-frame staging ring buffer for texture uploads during frame recording.
//!
//! Eliminates vkQueueWaitIdle during frame rendering: texture sub-image updates
//! that arrive while frameStarted_ is true are staged into this pool and their
//! transfer commands are deferred to EndFrame (after vkCmdEndRenderPass), where
//! the main command buffer is still recording but no render pass is active.
//!
//! Two rings (one per frame-in-flight slot) are reset after their fence is
//! signaled in BeginFrame — the same point the vertex ring buffer is reset.
//! Suballocation aligns to 256 bytes to satisfy all VkBufferImageCopy constraints.
class rVulkanStagingPool
{
public:
    //! Suballocated region returned by Acquire().
    struct Allocation {
        VkBuffer     buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void*        mapped = nullptr;
    };

    rVulkanStagingPool()  = default;
    ~rVulkanStagingPool() = default;

    //! One-time init. Does NOT allocate GPU memory — rings grow lazily on first Acquire.
    bool Init(rVulkanContext& ctx);

    //! Free all GPU memory. Safe to call before Init().
    void Destroy();

    //! Reset a frame slot's ring to empty. Must be called after the fence for that slot
    //! is signaled (i.e., immediately after vkWaitForFences in BeginFrame).
    void Reset(uint32_t frameSlot);

    //! Suballocate `size` bytes from the ring for the given frame slot, growing the
    //! backing buffer if necessary. Returns false on VMA allocation failure.
    bool Acquire(uint32_t frameSlot, VkDeviceSize size, Allocation& out);

    rVulkanStagingPool(const rVulkanStagingPool&)            = delete;
    rVulkanStagingPool& operator=(const rVulkanStagingPool&) = delete;

private:
    struct FrameRing {
        VkBuffer      buffer     = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkDeviceSize  capacity   = 0;
        VkDeviceSize  used       = 0;
        void*         mapped     = nullptr;
        struct OldBuf { VkBuffer buffer; VmaAllocation allocation; };
        std::vector<OldBuf> oldBuffers; // deferred destroy, drained in Reset()
    };

    bool GrowRing(FrameRing& ring, VkDeviceSize required);

    static constexpr uint32_t    kFrames  = 2;
    static constexpr VkDeviceSize kInitial = 1 * 1024 * 1024; // 1 MB starting size

    FrameRing       rings_[kFrames];
    rVulkanContext* ctx_ = nullptr;
};

#endif // DEDICATED
#endif // RVULKANSTAGINGPOOL_H
