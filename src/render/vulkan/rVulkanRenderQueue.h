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

#ifndef RVULKANRENDERQUEUE_H
#define RVULKANRENDERQUEUE_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "rVertex.h"
#include <vector>

class rVulkanContext;
class rVulkanPipelineManager;
class rVulkanDescriptorManager;
class vkRenderer;
struct rRenderStateKey;
enum class rRenderPhase;

//! Executes batched geometry using Vulkan command buffers.
//! Called from vkRenderer when rRenderQueue::ExecutePhase triggers rendering.
//! This replaces the GL3-specific bucket Upload/RenderTriangles path.
class rVulkanRenderQueue
{
public:
    rVulkanRenderQueue() = default;
    ~rVulkanRenderQueue() = default;

    //! Initialize with Vulkan resources
    bool Init(rVulkanContext& ctx, VkCommandPool commandPool);

    //! Draw a batch of triangles with the given state
    //! @param cmd Active command buffer (from vkRenderer::GetCurrentCommandBuffer)
    //! @param vertices CPU-side vertex data
    //! @param vertexCount Number of vertices
    //! @param state Render state (texture, blend mode, flags)
    //! @param pipelineMgr Pipeline cache for state→pipeline lookup
    //! @param descriptorMgr Descriptor cache for texture binding
    //! @param pushConstants Per-draw push constant data (MVP + texMatrix)
    void DrawTriangles(VkCommandBuffer cmd,
                       const rVertex20* vertices, size_t vertexCount,
                       const rRenderStateKey& state,
                       rVulkanPipelineManager& pipelineMgr,
                       VkDescriptorSet descriptorSet,
                       const void* pushConstants, size_t pushSize,
                       bool cullFace = false, bool frontFaceCW = true,
                       uint8_t colorWriteMask = 0xF);

    //! Draw a batch of lit triangles (rVertexLit32 with normals)
    void DrawLitTriangles(VkCommandBuffer cmd,
                          const void* vertices, size_t vertexCount,
                          const rRenderStateKey& state,
                          rVulkanPipelineManager& pipelineMgr,
                          VkDescriptorSet descriptorSet,
                          const void* pushConstants, size_t pushSize,
                          bool cullFace = false, bool frontFaceCW = true,
                          uint8_t colorWriteMask = 0xF);

    //! Draw instanced lit triangles (for cycle batching).
    //! @param vertices Geometry buffer (rVertexLit32, shared across instances)
    //! @param vertexCount Number of vertices in the geometry
    //! @param instances Per-instance data (rInstanceData: model matrix + color)
    //! @param instanceCount Number of instances
    void DrawInstancedLitTriangles(VkCommandBuffer cmd,
                                   const void* vertices, size_t vertexCount,
                                   const void* instances, size_t instanceCount,
                                   const rRenderStateKey& state,
                                   rVulkanPipelineManager& pipelineMgr,
                                   VkDescriptorSet descriptorSet,
                                   const void* pushConstants, size_t pushSize,
                                   bool cullFace = false, bool frontFaceCW = true);

    //! Draw a batch of lines
    void DrawLines(VkCommandBuffer cmd,
                   const rVertex20* vertices, size_t vertexCount,
                   const rRenderStateKey& state,
                   rVulkanPipelineManager& pipelineMgr,
                   VkDescriptorSet descriptorSet,
                   const void* pushConstants, size_t pushSize,
                   bool cullFace = false, bool frontFaceCW = true,
                   uint8_t colorWriteMask = 0xF);

    //! Release GPU resources
    void Destroy();

    // Non-copyable
    rVulkanRenderQueue(const rVulkanRenderQueue&) = delete;
    rVulkanRenderQueue& operator=(const rVulkanRenderQueue&) = delete;

private:
    //! Upload vertex data to the active frame's vertex buffer and bind it.
    bool UploadAndBind(VkCommandBuffer cmd, const rVertex20* vertices, size_t vertexCount);

    rVulkanContext* ctx_ = nullptr;
    VkCommandPool   commandPool_ = VK_NULL_HANDLE;

    // Per-frame vertex buffers — each frame in flight gets its own buffer
    // to prevent CPU writes from corrupting data the GPU is still reading.
    static constexpr uint32_t kMaxFrames = 2; // MAX_FRAMES_IN_FLIGHT
    struct FrameBuffer {
        VkBuffer      buffer     = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkDeviceSize  size       = 0;
        void*         mappedPtr  = nullptr;
        VkDeviceSize  currentOffset = 0;
    };
    FrameBuffer frameBuffers_[kMaxFrames];
    uint32_t    activeFrame_ = 0;

    // High-water mark: the largest offset seen at ResetFrameOffset() time.
    VkDeviceSize   highWaterMark_ = 0;

    // Track last bound state to skip redundant Vulkan calls
    VkPipeline       lastBoundPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet  lastBoundDescSet_  = VK_NULL_HANDLE;

    // Old buffers queued for deferred destruction, per-frame to avoid
    // destroying a buffer that the other frame slot is still using.
    struct OldBuffer { VkBuffer buffer; VmaAllocation allocation; };
    std::vector<OldBuffer> oldBuffers_[kMaxFrames];

    // Helper: ensure the active frame's buffer has capacity
    bool EnsureFrameBufferCapacity(FrameBuffer& fb, VkDeviceSize required);

public:
    //! Set which frame slot to use for vertex uploads. Call before any draws each frame.
    void SetCurrentFrame(uint32_t frame) { activeFrame_ = frame % kMaxFrames; }

    //! Invalidate cached pipeline/descriptor state. Must be called after every vkCmdBeginRenderPass
    //! so the first draw in each new render pass re-binds pipeline and descriptor sets.
    void InvalidateBindingCache()
    {
        lastBoundPipeline_ = VK_NULL_HANDLE;
        lastBoundDescSet_  = VK_NULL_HANDLE;
    }

    //! Reset the frame offset — call at the start of each frame, after GPU fence is signaled.
    //! Also updates the high-water mark used by CompactIfNeeded().
    void ResetFrameOffset();

    //! Shrink the vertex buffer to 2× the observed high-water mark if the current
    //! allocation is more than 4× larger. Safe to call only when the GPU is idle
    //! (i.e., immediately after vkDeviceWaitIdle). Typical call sites are
    //! RecreateSwapchain() and ReloadShaders().
    void CompactIfNeeded(VkDevice device);

    //! Drain deferred-destroy buffers for the given frame slot. Caller must
    //! pass the slot whose fence has *just been waited on* — that's the slot
    //! whose CB has completed, so any oldBuffers queued during its previous
    //! turn on this slot are safe to free. Using the stored `activeFrame_`
    //! here was WRONG — at BeginFrame call time `activeFrame_` is still the
    //! *previous* frame's slot (SetCurrentFrame is called later in the
    //! frame), and its CB may still be in flight, triggering
    //! VUID-vkDestroyBuffer-buffer-00922.
    void CleanupOldBuffers(uint32_t frameSlot);
};

#endif // DEDICATED
#endif // RVULKANRENDERQUEUE_H
