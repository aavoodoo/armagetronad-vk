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

#include "rRenderGraph.h"

#ifndef DEDICATED

#include <cassert>
#include <iostream>
#include <string>
#include <utility>

// ============================================================================
// Construction / Reset
// ============================================================================

rRenderGraph::rRenderGraph()
{
    Reset();
}

void rRenderGraph::Reset()
{
    passes_.clear();
    compiled_ = false;

    for (auto& r : resources_)
    {
        r.image  = VK_NULL_HANDLE;
        r.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        r.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    // Depth resources: default aspect = DEPTH_BIT
    resources_[static_cast<size_t>(RGResourceId::SceneDepth)].aspect  = VK_IMAGE_ASPECT_DEPTH_BIT;
    resources_[static_cast<size_t>(RGResourceId::ShadowMap0)].aspect  = VK_IMAGE_ASPECT_DEPTH_BIT;
    resources_[static_cast<size_t>(RGResourceId::ShadowMap1)].aspect  = VK_IMAGE_ASPECT_DEPTH_BIT;
}

// ============================================================================
// Resource registration
// ============================================================================

void rRenderGraph::SetResource(RGResourceId id,
                                VkImage image,
                                VkImageLayout layout,
                                VkImageAspectFlags aspect)
{
    auto& r  = resources_[static_cast<size_t>(id)];
    r.image  = image;
    r.layout = layout;
    r.aspect = aspect;
}

// ============================================================================
// Pass registration
// ============================================================================

void rRenderGraph::AddPass(RGPass pass)
{
    compiled_ = false;
    passes_.push_back({std::move(pass), {}, false});
}

// ============================================================================
// Layout policy helpers
// ============================================================================

//! What layout does a READER of this resource need, and what barrier to get there?
void rRenderGraph::GetReadLayout(RGResourceId id,
                                  VkImageLayout&       outLayout,
                                  VkPipelineStageFlags& outDstStage,
                                  VkAccessFlags&       outDstAccess)
{
    switch (id)
    {
    case RGResourceId::SceneDepth:
    case RGResourceId::ShadowMap0:
    case RGResourceId::ShadowMap1:
        outLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        outDstStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        outDstAccess = VK_ACCESS_SHADER_READ_BIT;
        break;

    default:  // color resources (SceneColor, SceneEmissive, SwapchainOut)
        outLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outDstStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        outDstAccess = VK_ACCESS_SHADER_READ_BIT;
        break;
    }
}

//! What layout does a WRITER of this resource leave it in (render pass finalLayout)?
void rRenderGraph::GetWriteLayout(RGResourceId id,
                                   VkImageLayout&       outLayout,
                                   VkPipelineStageFlags& outSrcStage,
                                   VkAccessFlags&       outSrcAccess)
{
    switch (id)
    {
    case RGResourceId::ShadowMap0:
    case RGResourceId::ShadowMap1:
        // Shadow render pass finalLayout = DEPTH_STENCIL_READ_ONLY_OPTIMAL
        outLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        outSrcStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        outSrcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case RGResourceId::SceneDepth:
        // Scene render pass finalLayout = DEPTH_STENCIL_READ_ONLY_OPTIMAL
        outLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        outSrcStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        outSrcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case RGResourceId::SceneColor:
    case RGResourceId::SceneEmissive:
        // Scene render pass finalLayout = SHADER_READ_ONLY_OPTIMAL
        outLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outSrcStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        outSrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case RGResourceId::SwapchainOut:
        outLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        outSrcStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        outSrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    default:
        outLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outSrcStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        outSrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    }
}

// ============================================================================
// Compile
// ============================================================================

void rRenderGraph::Compile()
{
    // Simulate resource layout changes as passes execute in order.
    // For each pass: record any barriers needed to bring read resources into
    // the expected layout, then advance write resources to their final layout.

    // Working copy of tracked states (Compile does not modify resources_ directly)
    std::array<ResourceState, static_cast<size_t>(RGResourceId::Count)> simState = resources_;

    for (auto& cp : passes_)
    {
        cp.barriers.clear();
        cp.emitted = false;

        // --- Reads: ensure each read resource is in the correct layout ---
        for (RGResourceId rid : cp.pass.reads)
        {
            auto& state = simState[static_cast<size_t>(rid)];
            if (state.image == VK_NULL_HANDLE) continue;

            VkImageLayout        readLayout;
            VkPipelineStageFlags dstStage;
            VkAccessFlags        dstAccess;
            GetReadLayout(rid, readLayout, dstStage, dstAccess);

            if (state.layout == readLayout) continue;  // already in correct layout

            // Determine source stage/access from current (pre-pass) layout
            VkPipelineStageFlags srcStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags        srcAccess = 0;

            if (state.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                state.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            {
                srcStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            }
            else if (state.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                     state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                srcStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }

            ImageBarrier b{};
            b.image     = state.image;
            b.oldLayout = state.layout;
            b.newLayout = readLayout;
            b.aspect    = state.aspect;
            b.srcStage  = srcStage;
            b.dstStage  = dstStage;
            b.srcAccess = srcAccess;
            b.dstAccess = dstAccess;
            cp.barriers.push_back(b);

            // Advance simulated layout so downstream passes see the updated state
            state.layout = readLayout;
        }

        // --- Writes: advance write resources to their post-pass (finalLayout) layout ---
        for (RGResourceId rid : cp.pass.writes)
        {
            auto& state = simState[static_cast<size_t>(rid)];
            if (state.image == VK_NULL_HANDLE) continue;

            VkImageLayout        writeLayout;
            VkPipelineStageFlags srcStage;
            VkAccessFlags        srcAccess;
            GetWriteLayout(rid, writeLayout, srcStage, srcAccess);
            state.layout = writeLayout;
        }
    }

    compiled_ = true;
}

// ============================================================================
// Barrier emission
// ============================================================================

void rRenderGraph::EmitBarrier(VkCommandBuffer cmd, const ImageBarrier& b)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = b.oldLayout;
    barrier.newLayout           = b.newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = b.image;
    barrier.subresourceRange    = { b.aspect, 0, 1, 0, 1 };
    barrier.srcAccessMask       = b.srcAccess;
    barrier.dstAccessMask       = b.dstAccess;

    vkCmdPipelineBarrier(cmd,
                         b.srcStage, b.dstStage,
                         0,       // no per-region dependency
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

// ============================================================================
// Execute helpers
// ============================================================================

int rRenderGraph::FindPass(const std::string& name) const
{
    for (int i = 0; i < static_cast<int>(passes_.size()); ++i)
        if (passes_[i].pass.name == name) return i;
    return -1;
}

bool rRenderGraph::EmitBarriersForPass(VkCommandBuffer cmd, const std::string& passName)
{
    assert(compiled_ && "rRenderGraph: call Compile() before EmitBarriersForPass()");
    int idx = FindPass(passName);
    if (idx < 0)
    {
        std::cerr << "[RenderGraph] EmitBarriersForPass: pass '" << passName << "' not found\n";
        return false;
    }

    auto& cp = passes_[idx];
    if (cp.emitted) return true;  // barriers already emitted this frame

    for (const auto& b : cp.barriers)
        EmitBarrier(cmd, b);

    cp.emitted = true;
    return true;
}

bool rRenderGraph::ExecutePass(VkCommandBuffer cmd, const std::string& passName)
{
    assert(compiled_ && "rRenderGraph: call Compile() before ExecutePass()");
    int idx = FindPass(passName);
    if (idx < 0)
    {
        std::cerr << "[RenderGraph] ExecutePass: pass '" << passName << "' not found\n";
        return false;
    }

    auto& cp = passes_[idx];
    if (!cp.pass.execute) return false;  // external pass — caller drives it

    EmitBarriersForPass(cmd, passName);
    cp.pass.execute(cmd);
    return true;
}

void rRenderGraph::Execute(VkCommandBuffer cmd)
{
    assert(compiled_ && "rRenderGraph: call Compile() before Execute()");
    for (auto& cp : passes_)
    {
        if (!cp.pass.execute) continue;  // external pass — skip
        EmitBarriersForPass(cmd, cp.pass.name);
        cp.pass.execute(cmd);
    }
}

#endif // DEDICATED
