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

#ifndef RVULKANPIPELINE_H
#define RVULKANPIPELINE_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <cstdint>
#include <string>

//! Pipeline state key — determines which VkPipeline to use.
//! Immutable render state is baked into the pipeline; dynamic state
//! (viewport, scissor) is set per-draw.
struct rVulkanPipelineKey
{
    uint8_t blendMode;       // rBlendMode value
    bool    depthTest;
    bool    depthWrite;
    bool    cullFace;
    bool    frontFaceCW;     // true = CW, false = CCW (for mirror views)
    bool    useLines;        // GL_LINES vs GL_TRIANGLES topology
    bool    litVertex;       // true = rVertexLit32 with normals, false = rVertex20
    bool    instanced;       // true = per-instance model matrix + color (uber_instanced.vert)
    uint8_t colorWriteMask;  // VK_COLOR_COMPONENT_R/G/B/A bits, 0xF = all

    bool operator==(const rVulkanPipelineKey& o) const
    {
        return blendMode == o.blendMode &&
               depthTest == o.depthTest &&
               depthWrite == o.depthWrite &&
               cullFace == o.cullFace &&
               frontFaceCW == o.frontFaceCW &&
               useLines == o.useLines &&
               litVertex == o.litVertex &&
               instanced == o.instanced &&
               colorWriteMask == o.colorWriteMask;
    }
};

struct rVulkanPipelineKeyHash
{
    size_t operator()(const rVulkanPipelineKey& k) const
    {
        size_t h = k.blendMode;
        h = h * 31 + k.depthTest;
        h = h * 31 + k.depthWrite;
        h = h * 31 + k.cullFace;
        h = h * 31 + k.frontFaceCW;
        h = h * 31 + k.useLines;
        h = h * 31 + k.litVertex;
        h = h * 31 + k.instanced;
        h = h * 31 + k.colorWriteMask;
        return h;
    }
};

//! Manages VkPipeline creation and caching.
//! Pipelines are created on-demand based on render state and cached.
class rVulkanPipelineManager
{
public:
    rVulkanPipelineManager() = default;
    ~rVulkanPipelineManager() = default;

    //! Initialize with device, render pass, and shader modules.
    //! setLayouts[0] = set 0 (texture sampler), setLayouts[1] = set 1 (lighting UBO).
    //! `fragShader` is the "no emissive output" variant used for render passes
    //! with a single color attachment (swapchain pass). `fragShaderEmissive`
    //! is the 2-attachment variant used for the post-process offscreen pass.
    //! If `fragShaderEmissive` is VK_NULL_HANDLE, the plain variant is used
    //! for every pass (post-process disabled, fallback).
    [[nodiscard]] bool Init(VkDevice device, VkRenderPass renderPass,
              VkShaderModule vertShader, VkShaderModule fragShader,
              VkShaderModule fragShaderEmissive,
              const VkDescriptorSetLayout* setLayouts, uint32_t setLayoutCount,
              VkPipelineLayout* outLayout,
              const VkPhysicalDeviceProperties* deviceProps = nullptr);

    //! Set instanced vertex shader (for cycle batching). Optional.
    void SetInstancedVertShader(VkShaderModule s) { vertShaderInstanced_ = s; }

    //! Get or create a pipeline for the given state
    VkPipeline GetPipeline(const rVulkanPipelineKey& key);

    //! Get the pipeline layout (shared across all pipelines)
    VkPipelineLayout GetLayout() const { return layout_; }

    //! Switch active render pass (cheap — just updates pointer, cache survives)
    void SetRenderPass(VkRenderPass rp);

    //! Declare a render pass's color attachment count. Required for any
    //! render pass with more than 1 color attachment (e.g. the post-process
    //! offscreen target, which has both scene color AND scene emissive).
    //! The pipeline color blend state emits one VkPipelineColorBlendAttachmentState
    //! per color attachment, so the count must match. Render passes not
    //! registered default to 1 color attachment.
    void RegisterRenderPass(VkRenderPass rp, uint32_t colorAttachmentCount);

    //! Invalidate pipelines for a specific destroyed render pass
    void InvalidateRenderPass(VkRenderPass rp);

    //! Eagerly create all pipeline variants the game is known to use.
    //! Called once at init after shaders are loaded — eliminates per-frame
    //! stutter from on-demand JIT compilation. Creates ~100 pipelines, takes
    //! ~200ms on a warm driver cache, ~2s on a cold cache. Subsequent runs
    //! hit the disk pipeline cache so this is near-instant.
    //! Pipelines are created for the CURRENT renderPass_ — call after
    //! SetRenderPass() if you want to pre-warm for a specific pass.
    void PrewarmCommonPipelines();

    //! Destroy all pipelines and layout
    void Destroy();

private:
    VkPipeline CreatePipeline(const rVulkanPipelineKey& key);

    VkPhysicalDeviceProperties deviceProps_ = {};

    VkDevice         device_        = VK_NULL_HANDLE;
    VkRenderPass     renderPass_    = VK_NULL_HANDLE;
    VkPipelineLayout layout_        = VK_NULL_HANDLE;
    VkPipelineCache  pipelineCache_ = VK_NULL_HANDLE;
    VkShaderModule   vertShader_         = VK_NULL_HANDLE;
    VkShaderModule   vertShaderInstanced_ = VK_NULL_HANDLE; // uber_instanced.vert
    VkShaderModule   fragShader_         = VK_NULL_HANDLE; // 1-attachment variant
    VkShaderModule   fragShaderEmissive_ = VK_NULL_HANDLE; // 2-attachment variant

    // Pipeline cache keyed by (renderPass, pipelineKey) — survives render pass switches
    struct FullPipelineKey {
        VkRenderPass renderPass;
        rVulkanPipelineKey key;
        bool operator==(const FullPipelineKey& o) const {
            return renderPass == o.renderPass && key == o.key;
        }
    };
    struct FullPipelineKeyHash {
        size_t operator()(const FullPipelineKey& k) const {
            size_t h = reinterpret_cast<size_t>(k.renderPass);
            h ^= rVulkanPipelineKeyHash{}(k.key) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<FullPipelineKey, VkPipeline, FullPipelineKeyHash> cache_;

    // Render pass → color attachment count. Defaults to 1 if not in the map.
    // Populated via RegisterRenderPass(); used by CreatePipeline to emit the
    // correct number of color blend attachment entries.
    std::unordered_map<VkRenderPass, uint32_t> renderPassColorCounts_;
};

#endif // DEDICATED
#endif // RVULKANPIPELINE_H
