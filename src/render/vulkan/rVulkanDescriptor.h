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

#ifndef RVULKANDESCRIPTOR_H
#define RVULKANDESCRIPTOR_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>

//! Manages descriptor set layout, pool, and per-texture descriptor sets.
//! Layout: set 0 = UBO (per-frame uniforms), set 1 = texture sampler.
//! Push constants are used for per-draw data (MVP, flags).
class rVulkanDescriptorManager
{
public:
    rVulkanDescriptorManager() = default;
    ~rVulkanDescriptorManager() = default;

    //! Create descriptor set layout and pool
    bool Init(VkDevice device, uint32_t maxSets = 256);

    //! Get or create a descriptor set for a texture
    //! @param imageView The texture's VkImageView
    //! @param sampler The VkSampler to use
    //! @return VkDescriptorSet bound to this texture, or VK_NULL_HANDLE on failure
    VkDescriptorSet GetOrCreateTextureSet(VkImageView imageView, VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    //! Get the descriptor set layout (for pipeline creation — set 0, texture only)
    VkDescriptorSetLayout GetLayout() const { return layout_; }

    //! Invalidate cached descriptor set for a specific imageView. The
    //! actual vkFreeDescriptorSets is DEFERRED — the freed sets are
    //! stashed and actually released on the next call to DrainDeferred
    //! (called from the renderer's BeginFrame after the current slot's
    //! fence wait). This prevents freeing descriptor sets that are still
    //! referenced by in-flight command buffers.
    //! Call when sampler changes for an existing texture, or when the
    //! imageView is about to be destroyed.
    void InvalidateCache(VkImageView imageView);

    //! Release deferred descriptor sets. Called from the renderer's
    //! BeginFrame after waiting on this slot's in-flight fence. Safe
    //! because the fence guarantees the previous submission to this slot
    //! is complete, and descriptor sets queued MAX_FRAMES_IN_FLIGHT frames
    //! ago have had all in-flight slots cycle past them by now.
    //!
    //! `slot` is the current frame-in-flight slot; the queue for that
    //! slot — populated MAX_FRAMES_IN_FLIGHT frames ago — is drained.
    void DrainDeferred(uint32_t slot);

    //! Reset all descriptor sets (call at frame start or on pool exhaustion)
    void Reset();

    //! Destroy all resources
    void Destroy();

    //! Number of live descriptor pools (for monitoring; should stabilize after warmup)
    uint32_t GetPoolCount() const { return static_cast<uint32_t>(pools_.size()); }

    //! Current slot index used by InvalidateCache. Matches the slot that
    //! was last passed to DrainDeferred. Use this (not the renderer's
    //! currentFrame_) when queuing deferred-delete textures alongside
    //! InvalidateCache — they must land in the same slot so the descriptor
    //! free happens before the image-view destroy.
    uint32_t GetCurrentSlot() const { return currentSlot_; }

    //! Free ALL descriptor sets immediately — both the live cache and every
    //! deferred-free slot. Call only after vkDeviceWaitIdle; safe because
    //! no command buffer can still reference any of these sets.
    //! Used before RecreateSwapchain destroys image views that are still
    //! formally referenced by cached or deferred descriptor sets.
    void FlushAllDeferred();

    //! Number of deferred-free slots. MUST equal MAX_FRAMES_IN_FLIGHT in rVulkanRender.h.
    //! A static_assert in rVulkanRender.cpp enforces this.
    static constexpr uint32_t PP_MAX_FRAMES = 2;

    // Non-copyable
    rVulkanDescriptorManager(const rVulkanDescriptorManager&) = delete;
    rVulkanDescriptorManager& operator=(const rVulkanDescriptorManager&) = delete;

private:
    VkDevice              device_       = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_       = VK_NULL_HANDLE;
    uint32_t              poolCapacity_ = 256;

    // Pool chain: new pools are appended on exhaustion.
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is set on every pool so
    // individual sets can be freed in InvalidateCache without resetting the whole pool.
    std::vector<VkDescriptorPool> pools_;

    //! Allocate a fresh pool of poolCapacity_ sets and append it to pools_.
    bool AllocatePool();

    //! Per-pool active set count. Incremented on successful vkAllocateDescriptorSets,
    //! decremented in DrainDeferred after vkFreeDescriptorSets. When a pool's count
    //! reaches 0 and it is not the only remaining pool, it is destroyed immediately.
    std::unordered_map<VkDescriptorPool, uint32_t> poolAllocCounts_;

    // Cache: (imageView, sampler) → (owning pool, descriptor set)
    // Both key fields are needed: handles can be reused after deletion+reallocation,
    // and sampler changes (via TexParameter) must produce a new descriptor set.
    struct CacheKey
    {
        VkImageView view;
        VkSampler   sampler;
        bool operator==(const CacheKey& o) const { return view == o.view && sampler == o.sampler; }
    };
    struct CacheKeyHash
    {
        size_t operator()(const CacheKey& k) const
        {
            size_t h = std::hash<VkImageView>{}(k.view);
            h ^= std::hash<VkSampler>{}(k.sampler) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct CacheEntry
    {
        VkDescriptorPool pool;
        VkDescriptorSet  set;
    };
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;

    // --- Deferred free queue ---
    // Descriptor sets freed by InvalidateCache are pushed to a per-slot
    // queue rather than immediately released. DrainDeferred is called at
    // BeginFrame AFTER waiting on that slot's fence, guaranteeing the
    // previous submission using this slot has fully completed.
    //
    // The queue for slot S is populated now and drained when slot S comes
    // around again, which is MAX_FRAMES_IN_FLIGHT frames later. By that
    // time all frame slots have cycled past the Invalidate call, so no
    // in-flight command buffer can still reference the descriptor sets.
    //
    std::vector<CacheEntry> deferredFree_[PP_MAX_FRAMES];

    // The slot that subsequent InvalidateCache calls should push to.
    // Updated by DrainDeferred each BeginFrame so the deferral naturally
    // follows the renderer's frame-in-flight cycling.
    uint32_t currentSlot_ = 0;
};

#endif // DEDICATED
#endif // RVULKANDESCRIPTOR_H
