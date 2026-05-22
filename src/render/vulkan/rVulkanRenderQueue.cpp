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

#include "rVulkanRenderQueue.h"
#include "rVulkanContext.h"
#include "rVulkanPipeline.h"
#include "rVulkanDescriptor.h"
#include "rVulkanBuffer.h"
#include "rRenderBucket.h"
#include "rVertex.h"
#include "rCycleRenderer.h"
#include <cstring>
#include <iostream>

static_assert(sizeof(rVertexLit32) == 32, "rVertexLit32 size changed — update DrawLitTriangles");
static_assert(sizeof(rInstanceData) == 80, "rInstanceData size changed — update DrawInstancedLitTriangles");

bool rVulkanRenderQueue::Init(rVulkanContext& ctx, VkCommandPool commandPool)
{
    ctx_ = &ctx;
    commandPool_ = commandPool;
    return true;
}

// Ensure the given per-frame buffer has at least `required` bytes of capacity.
bool rVulkanRenderQueue::EnsureFrameBufferCapacity(FrameBuffer& fb, VkDeviceSize required)
{
    if (required <= fb.size) return true;

    VmaAllocator vma = ctx_->GetAllocator();

    // Queue old buffer for deferred destruction (after frame completes)
    if (fb.buffer != VK_NULL_HANDLE)
    {
        fb.mappedPtr = nullptr;  // VMA manages persistent mapping lifetime
        oldBuffers_[activeFrame_].push_back({fb.buffer, fb.allocation});
        fb.buffer     = VK_NULL_HANDLE;
        fb.allocation = VK_NULL_HANDLE;
    }

    // Allocate larger buffer (at least 1MB, 2x required)
    VkDeviceSize newSize = std::max(required * 2, (VkDeviceSize)(1024 * 1024));

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = newSize;
    bufInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocCI{};
    vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                     | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo vmaAllocInfo{};
    if (vmaCreateBuffer(vma, &bufInfo, &vmaAllocCI,
                        &fb.buffer, &fb.allocation, &vmaAllocInfo) != VK_SUCCESS)
        return false;

    fb.size      = newSize;
    fb.mappedPtr = vmaAllocInfo.pMappedData;
    return fb.mappedPtr != nullptr;
}

bool rVulkanRenderQueue::UploadAndBind(VkCommandBuffer cmd,
                                       const rVertex20* vertices, size_t vertexCount)
{
    VkDeviceSize dataSize = vertexCount * sizeof(rVertex20);
    if (dataSize == 0) return false;

    FrameBuffer& fb = frameBuffers_[activeFrame_];

    const VkDeviceSize kAlignment = 32;
    VkDeviceSize offset = (fb.currentOffset + kAlignment - 1) & ~(kAlignment - 1);
    VkDeviceSize required = offset + dataSize;

    if (!EnsureFrameBufferCapacity(fb, required)) return false;

    if (!fb.mappedPtr) return false;
    memcpy(static_cast<char*>(fb.mappedPtr) + offset, vertices, static_cast<size_t>(dataSize));
    vkCmdBindVertexBuffers(cmd, 0, 1, &fb.buffer, &offset);
    fb.currentOffset = offset + dataSize;

    return true;
}

void rVulkanRenderQueue::DrawTriangles(VkCommandBuffer cmd,
                                       const rVertex20* vertices, size_t vertexCount,
                                       const rRenderStateKey& state,
                                       rVulkanPipelineManager& pipelineMgr,
                                       VkDescriptorSet descriptorSet,
                                       const void* pushConstants, size_t pushSize,
                                       bool cullFace, bool frontFaceCW,
                                       uint8_t colorWriteMask)
{
    if (!cmd || vertexCount == 0) return;

    if (!UploadAndBind(cmd, vertices, vertexCount))
        return;

    rVulkanPipelineKey pipeKey{};
    pipeKey.blendMode = static_cast<uint8_t>(state.blendMode);
    pipeKey.depthTest = (state.flags & rRenderStateKey::DepthTest) != 0;
    pipeKey.depthWrite = (state.flags & rRenderStateKey::DepthWrite) != 0;
    pipeKey.cullFace = cullFace;
    pipeKey.frontFaceCW = frontFaceCW;
    pipeKey.useLines = false;
    pipeKey.colorWriteMask = colorWriteMask;

    VkPipeline pipeline = pipelineMgr.GetPipeline(pipeKey);
    if (pipeline == VK_NULL_HANDLE) return;

    // Skip redundant state binds
    if (pipeline != lastBoundPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        lastBoundPipeline_ = pipeline;
    }
    if (descriptorSet != lastBoundDescSet_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineMgr.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        lastBoundDescSet_ = descriptorSet;
    }

    if (pushConstants && pushSize > 0)
    {
        vkCmdPushConstants(cmd, pipelineMgr.GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, static_cast<uint32_t>(pushSize), pushConstants);
    }

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

void rVulkanRenderQueue::DrawLitTriangles(VkCommandBuffer cmd,
                                           const void* vertices, size_t vertexCount,
                                           const rRenderStateKey& state,
                                           rVulkanPipelineManager& pipelineMgr,
                                           VkDescriptorSet descriptorSet,
                                           const void* pushConstants, size_t pushSize,
                                           bool cullFace, bool frontFaceCW,
                                           uint8_t colorWriteMask)
{
    if (!cmd || vertexCount == 0) return;

    FrameBuffer& fb = frameBuffers_[activeFrame_];

    // Upload rVertexLit32 data (32 bytes per vertex instead of 20)
    VkDeviceSize dataSize = vertexCount * 32; // sizeof(rVertexLit32)
    const VkDeviceSize kAlignment = 32;
    VkDeviceSize offset = (fb.currentOffset + kAlignment - 1) & ~(kAlignment - 1);
    VkDeviceSize required = offset + dataSize;

    if (!EnsureFrameBufferCapacity(fb, required)) return;

    if (!fb.mappedPtr) return;
    memcpy(static_cast<char*>(fb.mappedPtr) + offset, vertices, static_cast<size_t>(dataSize));
    vkCmdBindVertexBuffers(cmd, 0, 1, &fb.buffer, &offset);
    fb.currentOffset = offset + dataSize;

    rVulkanPipelineKey pipeKey{};
    pipeKey.blendMode = static_cast<uint8_t>(state.blendMode);
    pipeKey.depthTest = (state.flags & rRenderStateKey::DepthTest) != 0;
    pipeKey.depthWrite = (state.flags & rRenderStateKey::DepthWrite) != 0;
    pipeKey.cullFace = cullFace;
    pipeKey.frontFaceCW = frontFaceCW;
    pipeKey.useLines = false;
    pipeKey.litVertex = true;
    pipeKey.colorWriteMask = colorWriteMask;

    VkPipeline pipeline = pipelineMgr.GetPipeline(pipeKey);
    if (pipeline == VK_NULL_HANDLE) return;

    if (pipeline != lastBoundPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        lastBoundPipeline_ = pipeline;
    }
    if (descriptorSet != lastBoundDescSet_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineMgr.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        lastBoundDescSet_ = descriptorSet;
    }
    if (pushConstants && pushSize > 0)
        vkCmdPushConstants(cmd, pipelineMgr.GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, static_cast<uint32_t>(pushSize), pushConstants);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

void rVulkanRenderQueue::DrawInstancedLitTriangles(
    VkCommandBuffer cmd,
    const void* vertices, size_t vertexCount,
    const void* instances, size_t instanceCount,
    const rRenderStateKey& state,
    rVulkanPipelineManager& pipelineMgr,
    VkDescriptorSet descriptorSet,
    const void* pushConstants, size_t pushSize,
    bool cullFace, bool frontFaceCW)
{
    if (!cmd || vertexCount == 0 || instanceCount == 0) return;

    FrameBuffer& fb = frameBuffers_[activeFrame_];

    // Upload geometry (rVertexLit32, 32 bytes per vertex)
    VkDeviceSize geomSize = vertexCount * 32;
    const VkDeviceSize kAlignment = 32;
    VkDeviceSize geomOffset = (fb.currentOffset + kAlignment - 1) & ~(kAlignment - 1);
    VkDeviceSize afterGeom = geomOffset + geomSize;

    // Upload instance data (rInstanceData, 80 bytes per instance)
    VkDeviceSize instSize = instanceCount * 80;
    VkDeviceSize instOffset = (afterGeom + kAlignment - 1) & ~(kAlignment - 1);
    VkDeviceSize required = instOffset + instSize;

    if (!EnsureFrameBufferCapacity(fb, required)) return;
    if (!fb.mappedPtr) return;

    memcpy(static_cast<char*>(fb.mappedPtr) + geomOffset, vertices, static_cast<size_t>(geomSize));
    memcpy(static_cast<char*>(fb.mappedPtr) + instOffset, instances, static_cast<size_t>(instSize));
    fb.currentOffset = instOffset + instSize;

    // Bind both vertex buffers: binding 0 = geometry, binding 1 = instances
    VkBuffer buffers[2] = {fb.buffer, fb.buffer};
    VkDeviceSize offsets[2] = {geomOffset, instOffset};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);

    rVulkanPipelineKey pipeKey{};
    pipeKey.blendMode = static_cast<uint8_t>(state.blendMode);
    pipeKey.depthTest = (state.flags & rRenderStateKey::DepthTest) != 0;
    pipeKey.depthWrite = (state.flags & rRenderStateKey::DepthWrite) != 0;
    pipeKey.cullFace = cullFace;
    pipeKey.frontFaceCW = frontFaceCW;
    pipeKey.useLines = false;
    pipeKey.litVertex = true;
    pipeKey.instanced = true;
    pipeKey.colorWriteMask = 0xF;

    VkPipeline pipeline = pipelineMgr.GetPipeline(pipeKey);
    if (pipeline == VK_NULL_HANDLE) return;

    if (pipeline != lastBoundPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        lastBoundPipeline_ = pipeline;
    }
    if (descriptorSet != lastBoundDescSet_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineMgr.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        lastBoundDescSet_ = descriptorSet;
    }
    if (pushConstants && pushSize > 0)
        vkCmdPushConstants(cmd, pipelineMgr.GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, static_cast<uint32_t>(pushSize), pushConstants);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount),
              static_cast<uint32_t>(instanceCount), 0, 0);
}

void rVulkanRenderQueue::DrawLines(VkCommandBuffer cmd,
                                   const rVertex20* vertices, size_t vertexCount,
                                   const rRenderStateKey& state,
                                   rVulkanPipelineManager& pipelineMgr,
                                   VkDescriptorSet descriptorSet,
                                   const void* pushConstants, size_t pushSize,
                                   bool cullFace, bool frontFaceCW,
                                   uint8_t colorWriteMask)
{
    if (!cmd || vertexCount == 0) return;

    if (!UploadAndBind(cmd, vertices, vertexCount))
        return;

    rVulkanPipelineKey pipeKey{};
    pipeKey.blendMode = static_cast<uint8_t>(state.blendMode);
    pipeKey.depthTest = (state.flags & rRenderStateKey::DepthTest) != 0;
    pipeKey.depthWrite = (state.flags & rRenderStateKey::DepthWrite) != 0;
    pipeKey.cullFace = cullFace;
    pipeKey.frontFaceCW = frontFaceCW;
    pipeKey.colorWriteMask = colorWriteMask;
    pipeKey.useLines = true;

    VkPipeline pipeline = pipelineMgr.GetPipeline(pipeKey);
    if (pipeline == VK_NULL_HANDLE) return;

    if (pipeline != lastBoundPipeline_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        lastBoundPipeline_ = pipeline;
    }
    if (descriptorSet != lastBoundDescSet_) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineMgr.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        lastBoundDescSet_ = descriptorSet;
    }

    if (pushConstants && pushSize > 0)
    {
        vkCmdPushConstants(cmd, pipelineMgr.GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, static_cast<uint32_t>(pushSize), pushConstants);
    }

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

void rVulkanRenderQueue::ResetFrameOffset()
{
    FrameBuffer& fb = frameBuffers_[activeFrame_];

    // Update the high-water mark with this frame's peak usage before resetting.
    if (fb.currentOffset > highWaterMark_)
        highWaterMark_ = fb.currentOffset;

    fb.currentOffset    = 0;
    lastBoundPipeline_  = VK_NULL_HANDLE;
    lastBoundDescSet_   = VK_NULL_HANDLE;
}

void rVulkanRenderQueue::CompactIfNeeded(VkDevice /*device*/)
{
    const VkDeviceSize kMinSize = 1024 * 1024;
    if (!ctx_ || highWaterMark_ == 0) return;

    VmaAllocator vma = ctx_->GetAllocator();

    for (uint32_t i = 0; i < kMaxFrames; ++i)
    {
        FrameBuffer& fb = frameBuffers_[i];
        if (fb.size == 0 || fb.size <= highWaterMark_ * 4) continue;

        VkDeviceSize targetSize = std::max(highWaterMark_ * 2, kMinSize);

        vmaDestroyBuffer(vma, fb.buffer, fb.allocation);
        fb.buffer     = VK_NULL_HANDLE;
        fb.allocation = VK_NULL_HANDLE;
        fb.mappedPtr  = nullptr;
        fb.size       = 0;

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = targetSize;
        bufInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
        vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                         | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo vmaAllocInfo{};
        if (vmaCreateBuffer(vma, &bufInfo, &vmaAllocCI,
                            &fb.buffer, &fb.allocation, &vmaAllocInfo) != VK_SUCCESS)
            continue;

        fb.size      = targetSize;
        fb.mappedPtr = vmaAllocInfo.pMappedData;
    }
}

void rVulkanRenderQueue::CleanupOldBuffers(uint32_t frameSlot)
{
    if (!ctx_) return;
    VmaAllocator vma = ctx_->GetAllocator();
    // Free old buffers for the slot whose fence the caller just waited on
    // (= the slot whose CB has completed). NOT activeFrame_, which at
    // BeginFrame-call time is still the previous frame's slot and whose
    // CB may still be in flight (its fence wasn't waited).
    auto& obs = oldBuffers_[frameSlot % kMaxFrames];
    for (auto& ob : obs)
        vmaDestroyBuffer(vma, ob.buffer, ob.allocation);
    obs.clear();
}

void rVulkanRenderQueue::Destroy()
{
    if (!ctx_) return;
    VmaAllocator vma = ctx_->GetAllocator();

    for (uint32_t i = 0; i < kMaxFrames; ++i)
    {
        FrameBuffer& fb = frameBuffers_[i];
        if (fb.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(vma, fb.buffer, fb.allocation);
            fb.buffer     = VK_NULL_HANDLE;
            fb.allocation = VK_NULL_HANDLE;
            fb.mappedPtr  = nullptr;
            fb.size       = 0;
        }
        for (auto& ob : oldBuffers_[i])
            vmaDestroyBuffer(vma, ob.buffer, ob.allocation);
        oldBuffers_[i].clear();
    }
}

#endif // DEDICATED
