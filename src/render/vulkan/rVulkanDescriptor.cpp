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

#include "rVulkanDescriptor.h"
#include <iostream>

bool rVulkanDescriptorManager::Init(VkDevice device, uint32_t maxSets)
{
    device_       = device;
    poolCapacity_ = maxSets;

    // Set 0 layout: binding 0 = texture sampler only.
    // The lighting UBO lives in set 1 (managed separately by the renderer, not here).
    VkDescriptorSetLayoutBinding bindings[1] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create descriptor set layout" << std::endl;
        return false;
    }

    return AllocatePool();
}

bool rVulkanDescriptorManager::AllocatePool()
{
    VkDescriptorPoolSize poolSizes[1] = {};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = poolCapacity_;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = poolCapacity_;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create descriptor pool (chain len=" << pools_.size() << ")" << std::endl;
        return false;
    }

    pools_.push_back(pool);
    poolAllocCounts_[pool] = 0;
    return true;
}

VkDescriptorSet rVulkanDescriptorManager::GetOrCreateTextureSet(VkImageView imageView, VkSampler sampler, VkImageLayout layout)
{
    CacheKey key{imageView, sampler};
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.set;

    // Allocate from the latest pool only. If exhausted, grow.
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool owningPool = VK_NULL_HANDLE;

    if (!pools_.empty())
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = pools_.back();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &layout_;

        VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &set);
        if (result == VK_SUCCESS)
        {
            owningPool = pools_.back();
            poolAllocCounts_[owningPool]++;
        }
        else if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
        {
            std::cerr << "[Vulkan] Failed to allocate descriptor set (vkResult=" << result << ")" << std::endl;
            return VK_NULL_HANDLE;
        }
    }

    if (owningPool == VK_NULL_HANDLE)
    {
        // Current pool exhausted; grow the chain.
        if (!AllocatePool())
            return VK_NULL_HANDLE;

        owningPool = pools_.back();

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = owningPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &layout_;

        if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to allocate descriptor set from new pool" << std::endl;
            return VK_NULL_HANDLE;
        }
        poolAllocCounts_[owningPool]++;
    }

    // Update the descriptor set: binding 0 = texture sampler only.
    // The lighting UBO is in set 1, managed per-frame by the renderer.
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = layout;
    imageInfo.imageView   = imageView;
    imageInfo.sampler     = sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imageInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    cache_[key] = CacheEntry{owningPool, set};
    return set;
}

void rVulkanDescriptorManager::InvalidateCache(VkImageView imageView)
{
    // Defer the actual free until MAX_FRAMES_IN_FLIGHT frames from now.
    // The GPU may still be reading these descriptor sets from command
    // buffers submitted by frame slots that haven't had their fences
    // waited yet — freeing immediately triggers validation errors and
    // can cause GPU hangs on drivers that strictly enforce sync.
    //
    // We push to currentSlot_, which is the slot currently in BeginFrame
    // (set by DrainDeferred). The queue for that slot will be drained the
    // next time this slot cycles around (MAX_FRAMES_IN_FLIGHT frames),
    // at which point all other slots have also cycled past and nothing
    // can still be using the descriptor sets.
    for (auto it = cache_.begin(); it != cache_.end(); )
    {
        if (it->first.view == imageView)
        {
            deferredFree_[currentSlot_].push_back(it->second);
            it = cache_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void rVulkanDescriptorManager::DrainDeferred(uint32_t slot)
{
    // Called from the renderer's BeginFrame after waiting on this slot's
    // in-flight fence. Safe to free the descriptor sets queued when this
    // slot was previously active — all subsequent frames on other slots
    // have also submitted-and-waited, so no command buffer still
    // references these descriptors.
    if (slot >= PP_MAX_FRAMES) return;

    if (!deferredFree_[slot].empty())
    {
        for (const auto& entry : deferredFree_[slot])
        {
            vkFreeDescriptorSets(device_, entry.pool, 1, &entry.set);
            auto it = poolAllocCounts_.find(entry.pool);
            if (it != poolAllocCounts_.end() && it->second > 0)
                it->second--;
        }
        deferredFree_[slot].clear();

        // Destroy any pool that became empty — but never the last one.
        for (auto it = pools_.begin(); it != pools_.end() && pools_.size() > 1; )
        {
            auto countIt = poolAllocCounts_.find(*it);
            if (countIt != poolAllocCounts_.end() && countIt->second == 0)
            {
                vkDestroyDescriptorPool(device_, *it, nullptr);
                poolAllocCounts_.erase(countIt);
                it = pools_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Subsequent InvalidateCache calls (during this frame's draws) queue
    // into this slot — they'll be drained the next time this slot comes
    // around, which is after all other slots have cycled through.
    currentSlot_ = slot;
}

void rVulkanDescriptorManager::Reset()
{
    cache_.clear();
    for (VkDescriptorPool pool : pools_)
        vkResetDescriptorPool(device_, pool, 0);
    // Keep pools allocated — Reset is called between frames, not at shutdown.
}

void rVulkanDescriptorManager::Destroy()
{
    cache_.clear();

    // Clear deferred-free queues — no need to vkFreeDescriptorSets since
    // we're about to destroy the pools that own them.
    for (uint32_t i = 0; i < PP_MAX_FRAMES; ++i)
        deferredFree_[i].clear();

    for (VkDescriptorPool pool : pools_)
        vkDestroyDescriptorPool(device_, pool, nullptr);
    pools_.clear();
    poolAllocCounts_.clear();

    if (layout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

#endif // DEDICATED
