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

#ifndef RRENDERGRAPH_H
#define RRENDERGRAPH_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <array>

// ============================================================================
// Resource identifiers
// ============================================================================

//! Identifies well-known per-frame render resources.
//! New resources: append before Count; existing indices must not change.
enum class RGResourceId : uint32_t {
    SceneColor    = 0,  //!< offscreen scene color (written by scene pass, read by PP)
    SceneEmissive = 1,  //!< offscreen emissive (written by scene pass, read by bloom)
    SceneDepth    = 2,  //!< offscreen depth (written by scene pass, read by PP depth effects)
    ShadowMap0    = 3,  //!< shadow map for light 0 (written by shadow pass, read by scene)
    ShadowMap1    = 4,  //!< shadow map for light 1
    SwapchainOut  = 5,  //!< final swapchain image (written by PP final pass)
    Count         = 6
};

// ============================================================================
// Pass declaration
// ============================================================================

//! Declares what a pass reads and writes.
//! Passes are added in execution order; topological ordering is not performed.
struct RGPass {
    std::string name;

    //! Resources this pass samples as shader inputs (read).
    //! For each: the graph ensures the image is in the correct read layout
    //! before the pass executes, emitting an explicit pipeline barrier if needed.
    std::vector<RGResourceId> reads;

    //! Resources this pass writes (render targets, storage outputs).
    //! After the pass finishes, the graph updates the tracked layout for
    //! downstream passes. For render-pass-based writes the tracked layout
    //! is the render pass's finalLayout (declared in GetWriteLayout).
    std::vector<RGResourceId> writes;

    //! Execute lambda — called by ExecutePass() or Execute().
    //! Set to nullptr for externally-driven passes (e.g. the scene pass whose
    //! content is supplied by the game render callback between Begin/EndFrame).
    std::function<void(VkCommandBuffer)> execute;
};

// ============================================================================
// Render graph
// ============================================================================

//! Lightweight render graph.
//!
//! Per-frame usage in vkRenderer:
//!
//!   BeginFrame:
//!     1. renderGraph_.Reset()
//!     2. renderGraph_.SetResource(SceneColor/Emissive/Depth, image, layout, aspect)
//!     3. renderGraph_.SetResource(ShadowMap0/1, ...)
//!     4. renderGraph_.AddPass({"shadow", {}, {ShadowMap0,ShadowMap1}, nullptr})
//!     5. renderGraph_.AddPass({"scene", {ShadowMap0,ShadowMap1},
//!                              {SceneColor,SceneEmissive,SceneDepth}, nullptr})
//!     6. renderGraph_.AddPass({"postprocess",
//!                              {SceneColor,SceneEmissive,SceneDepth}, {SwapchainOut},
//!                              ppLambda})
//!     7. renderGraph_.Compile()
//!     8. renderGraph_.EmitBarriersForPass(cmd, "shadow")
//!     9. RenderShadowPass(cmd)          // external — render pass handles layout transitions
//!    10. renderGraph_.EmitBarriersForPass(cmd, "scene")
//!    11. vkCmdBeginRenderPass(...)
//!
//!   (game callback runs and renders into the open render pass)
//!
//!   EndFrame:
//!    12. vkCmdEndRenderPass(...)        // render pass finalLayouts transition the images
//!    13. renderGraph_.EmitBarriersForPass(cmd, "postprocess")
//!    14. postProcess_.Execute(cmd, ...)
//!
//! The graph emits explicit pipeline barriers wherever the current tracked layout
//! of a resource does not match the layout a downstream pass expects.  For passes
//! whose render pass subpass-dependency already covers the transition, the barrier
//! emitted here is technically redundant but harmless (Vulkan merges them).
//!
class rRenderGraph
{
public:
    rRenderGraph();

    //! Reset for a new frame. Clears all passes and resets resource states.
    void Reset();

    //! Register a resource's image handle and its initial layout at the start of the frame.
    //! Must be called before Compile().
    //! @param id       Resource identifier
    //! @param image    VkImage handle (VK_NULL_HANDLE = not available this frame, skip barriers)
    //! @param layout   Layout the image is in at the start of the frame
    //! @param aspect   Image aspect (color or depth)
    void SetResource(RGResourceId id,
                     VkImage image,
                     VkImageLayout layout,
                     VkImageAspectFlags aspect);

    //! Add a pass (in execution order). Must be called before Compile().
    void AddPass(RGPass pass);

    //! Compile barrier sets for every pass.
    //! Simulates resource layout changes as passes execute in declaration order,
    //! recording a pipeline barrier for each resource that is not already in the
    //! layout required by the next reader.
    //! Must be called after all SetResource() / AddPass() calls.
    void Compile();

    //! Emit the pre-execution pipeline barriers for a named pass.
    //! Use this for externally-driven passes (execute == nullptr) at the correct
    //! call site in BeginFrame / EndFrame.
    //! Returns false if the pass is not found or barriers were already emitted.
    bool EmitBarriersForPass(VkCommandBuffer cmd, const std::string& passName);

    //! Emit barriers and call the execute lambda for a named pass.
    //! Returns false if the pass is not found or has no execute lambda.
    bool ExecutePass(VkCommandBuffer cmd, const std::string& passName);

    //! Execute all passes that have a non-null execute lambda, in declaration order,
    //! emitting their barriers first.
    //! External passes (execute == nullptr) are skipped.
    void Execute(VkCommandBuffer cmd);

private:
    // -------------------------------------------------------------------------
    // Resource state
    // -------------------------------------------------------------------------

    struct ResourceState {
        VkImage            image   = VK_NULL_HANDLE;
        VkImageLayout      layout  = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    };
    std::array<ResourceState, static_cast<size_t>(RGResourceId::Count)> resources_;

    // -------------------------------------------------------------------------
    // Compiled pass data
    // -------------------------------------------------------------------------

    struct ImageBarrier {
        VkImage              image     = VK_NULL_HANDLE;
        VkImageLayout        oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout        newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags   aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
        VkPipelineStageFlags srcStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags        srcAccess = 0;
        VkAccessFlags        dstAccess = 0;
    };

    struct CompiledPass {
        RGPass                    pass;
        std::vector<ImageBarrier> barriers;  //!< barriers to emit before execute
        bool                      emitted = false;  //!< guard: emit once per frame
    };
    std::vector<CompiledPass> passes_;
    bool compiled_ = false;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    //! Find pass index by name (-1 if not found).
    [[nodiscard]] int FindPass(const std::string& name) const;

    //! Return the layout and barrier info a reader of `id` requires.
    static void GetReadLayout(RGResourceId id,
                              VkImageLayout&      outLayout,
                              VkPipelineStageFlags& outDstStage,
                              VkAccessFlags&      outDstAccess);

    //! Return the layout and barrier info a writer of `id` leaves it in.
    //! (Corresponds to the render pass finalLayout for render-pass-based writes.)
    static void GetWriteLayout(RGResourceId id,
                               VkImageLayout&      outLayout,
                               VkPipelineStageFlags& outSrcStage,
                               VkAccessFlags&      outSrcAccess);

    //! Emit a single VkImageMemoryBarrier via vkCmdPipelineBarrier.
    static void EmitBarrier(VkCommandBuffer cmd, const ImageBarrier& b);
};

#endif // DEDICATED
#endif // RRENDERGRAPH_H
