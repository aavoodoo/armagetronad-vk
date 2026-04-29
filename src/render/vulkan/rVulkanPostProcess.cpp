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

#include <SDL3/SDL.h>
#include "rVulkanPostProcess.h"
#include "rVulkanContext.h"
#include "rVulkanShader.h"
#include "rVulkanPipeline.h"
#include "tDirectories.h"
#include "tString.h"
#include "tConfiguration.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cctype>

// File-scope pointer so the static MVP change callback can reach the
// singleton rVulkanPostProcess instance. There's only ever one in the
// game. Defined here so early functions (Destroy) can see it; used by the
// MVP registry code near the bottom of the file.
static rVulkanPostProcess* s_mvpOwner = nullptr;

// Push constant layout matching passthrough.frag.
// Must stay < 128 bytes (Vulkan minimum guaranteed push constant size).
struct PostProcessPushConstants
{
    float resolution[2]; // uResolution
    float time;          // uTime
    float _pad;
    float arenaBBox[4];  // uArenaBBox
};
static_assert(sizeof(PostProcessPushConstants) == 32, "post-process push constants must be 32 bytes");

bool rVulkanPostProcess::Init(rVulkanContext& ctx, VkFormat swapchainFormat, VkFormat depthFormat,
                              VkRenderPass swapchainRenderPass)
{
    ctx_ = &ctx;
    device_ = ctx.GetDevice();
    colorFormat_ = swapchainFormat;
    depthFormat_ = depthFormat;
    swapchainRenderPass_ = swapchainRenderPass;
    return BuildPipeline(ctx, swapchainRenderPass);
}

void rVulkanPostProcess::Destroy()
{
    // Save any pending MVP values to the per-moviepack cfg file, then
    // destroy the dynamic tSettingItems so they're removed from the
    // global config registry before the post-process object dies.
    if (!mvpRegistry_.empty())
        SaveMvpConfigFile(mvpMoviepackName_);
    UnregisterAllMvp();
    if (s_mvpOwner == this) s_mvpOwner = nullptr;

    DestroyOffscreen();
    DestroyPipeline();
    ctx_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

void rVulkanPostProcess::DestroyDescriptorPools()
{
    if (device_ == VK_NULL_HANDLE) return;
    // Destroy the global descriptor pool (passthrough + offscreen descriptors)
    if (globalDescPool_) { vkDestroyDescriptorPool(device_, globalDescPool_, nullptr); globalDescPool_ = VK_NULL_HANDLE; }
    // Destroy per-effect descriptor pools
    for (auto& [name, ef] : effects_)
    {
        if (ef.descriptorPool) { vkDestroyDescriptorPool(device_, ef.descriptorPool, nullptr); ef.descriptorPool = VK_NULL_HANDLE; }
    }
}

void rVulkanPostProcess::SetEnabled(bool enabled)
{
    if (enabled == enabled_) return;
    enabled_ = enabled;

    if (enabled_ && !offscreenBuilt_ && ctx_ && width_ > 0 && height_ > 0)
    {
        BuildOffscreen(*ctx_, width_, height_);
        // If SetActiveEffect was called before the offscreen was built, the
        // pending effect name hasn't actually been loaded yet (because
        // EnsureEffectLoaded needs offscreenColorView_ to populate its
        // sampler descriptor bindings). Load it now.
        if (activeEffectPtr_ == nullptr && !activeEffect_.empty())
        {
            Effect* ef = EnsureEffectLoaded(activeEffect_);
            if (ef)
            {
                activeEffectPtr_ = ef;
                SeedEffectDefaults(*ef);
            }
        }
    }
}

void rVulkanPostProcess::ClearRenderPassRefs(VkRenderPass rp)
{
    if (rp == VK_NULL_HANDLE) return;
    if (swapchainRenderPass_ == rp) swapchainRenderPass_ = VK_NULL_HANDLE;
    for (auto& [name, ef] : effects_)
        for (auto& pass : ef.passes)
            if (pass.renderPass == rp) pass.renderPass = VK_NULL_HANDLE;
}

bool rVulkanPostProcess::OnSwapchainResized(rVulkanContext& ctx, uint32_t width, uint32_t height,
                                            VkRenderPass swapchainRenderPass,
                                            VkRenderPass oldSwapchainRP)
{
    ctx_ = &ctx;
    width_ = width;
    height_ = height;
    // Save old handles BEFORE updating — effect passes may hold stale copies
    // of the old swapchain/offscreen render passes (already destroyed by caller).
    VkRenderPass oldSwapRP = (oldSwapchainRP != VK_NULL_HANDLE) ? oldSwapchainRP : swapchainRenderPass_;
    VkRenderPass oldSceneRP = offscreenRenderPass_;
    swapchainRenderPass_ = swapchainRenderPass;

    if (!enabled_) return true;

    // Clear cached effects so intermediate render targets (bloom half-res, etc.)
    // are rebuilt at the new resolution on next use.
    for (auto& pair : effects_)
    {
        VkDevice dev = ctx.GetDevice();
        for (auto& pass : pair.second.passes)
        {
            if (pass.pipeline)   vkDestroyPipeline(dev, pass.pipeline, nullptr);
            if (pass.fragShader) vkDestroyShaderModule(dev, pass.fragShader, nullptr);
            if (pass.descSet[0]) vkFreeDescriptorSets(dev, pair.second.descriptorPool, 1, &pass.descSet[0]);
            if (pass.framebuffer) vkDestroyFramebuffer(dev, pass.framebuffer, nullptr);
            // Don't destroy render passes that belong to the swapchain or offscreen
            // (they're destroyed separately by framebuffer_.Destroy / DestroyOffscreen)
            if (pass.renderPass && pass.renderPass != oldSwapRP && pass.renderPass != oldSceneRP)
                vkDestroyRenderPass(dev, pass.renderPass, nullptr);
        }
        pair.second.pool.Destroy(ctx.GetAllocator(), dev);
        if (pair.second.descriptorPool) vkDestroyDescriptorPool(dev, pair.second.descriptorPool, nullptr);
    }
    effects_.clear();
    activeEffectPtr_ = nullptr;

    DestroyOffscreen();
    return BuildOffscreen(ctx, width, height);
}

void rVulkanPostProcess::SetActiveEffect(const std::string& name)
{
    std::string target = name.empty() ? "passthrough" : name;

    // Record the requested effect name up front. If the offscreen target
    // hasn't been built yet (POST_PROCESS_ENABLED still false), we defer the
    // actual load — SetEnabled(true) will pick it up from activeEffect_ once
    // the offscreen views exist. This lets user.cfg set EFFECT and ENABLED
    // in either order without causing null-view descriptor writes.
    activeEffect_ = target;

    if (!offscreenBuilt_)
    {
        activeEffectPtr_ = nullptr;
        return;
    }

    Effect* ef = EnsureEffectLoaded(target);
    if (!ef)
    {
        std::cerr << "[PostProcess] Effect '" << target << "' not found";
        // Try falling back to passthrough (which should always exist).
        if (target != "passthrough")
        {
            std::cerr << ", falling back to passthrough";
            ef = EnsureEffectLoaded("passthrough");
            if (ef) activeEffect_ = "passthrough";
        }
        std::cerr << "\n";
        if (!ef)
        {
            activeEffectPtr_ = nullptr;
            return;
        }
    }

    activeEffectPtr_ = ef;
    SeedEffectDefaults(*ef);

    // Overlay any user-tuned MVP values for this effect on top of the
    // defaults. The registry may already contain entries from previous
    // effect loads, a config file load, or live console edits.
    ApplyMvpToCurrentParams();
}

void rVulkanPostProcess::SeedEffectDefaults(const Effect& ef)
{
    // Seed parameter values from the declared defaults. Slot numbers are
    // linear (0..31 for floats, 0..15 for ints); we map them to the vec4/
    // ivec4 rows of the std140-compatible UBO via slot/4 + slot%4.
    std::memset(&currentParams_, 0, sizeof(currentParams_));
    for (const auto& p : ef.params)
    {
        if (p.type == rPostProcessParam::Int)
        {
            if (p.slot >= 0 && p.slot < 16)
                currentParams_.iparams[p.slot / 4][p.slot % 4] = static_cast<int>(p.defaults[0]);
        }
        else if (p.type == rPostProcessParam::Vec4)
        {
            // Vec4 slots are linear scalar slots [0..31] but MUST be
            // divisible by 4 — the vec4 occupies 4 consecutive float slots
            // and std140 alignment requires that to fall on a vec4 row.
            if (p.slot >= 0 && p.slot + 3 < 32 && (p.slot % 4) == 0)
                for (int i = 0; i < 4; ++i)
                    currentParams_.fparams[p.slot / 4][i] = p.defaults[i];
        }
        else // Float
        {
            if (p.slot >= 0 && p.slot < 32)
                currentParams_.fparams[p.slot / 4][p.slot % 4] = p.defaults[0];
        }
    }
}

const std::vector<rPostProcessParam>& rVulkanPostProcess::GetParams() const
{
    static const std::vector<rPostProcessParam> empty;
    return activeEffectPtr_ ? activeEffectPtr_->params : empty;
}

// ============================================================================
// Pipeline resources (swapchain-independent, created once)
// ============================================================================

bool rVulkanPostProcess::BuildPipeline(rVulkanContext& ctx, VkRenderPass swapchainRenderPass)
{
    VkDevice device = ctx.GetDevice();

    // --- Descriptor set layout 0: N combined image samplers ---
    // PP_MAX_SAMPLERS_PER_PASS bindings so any pass can declare up to that
    // many samplers in its .pipeline file. Passes that declare fewer still
    // populate the unused bindings with a placeholder view.
    VkDescriptorSetLayoutBinding samplerBindings[PP_MAX_SAMPLERS_PER_PASS]{};
    for (int i = 0; i < PP_MAX_SAMPLERS_PER_PASS; ++i)
    {
        samplerBindings[i].binding = i;
        samplerBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBindings[i].descriptorCount = 1;
        samplerBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo samplerLayoutInfo{};
    samplerLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    samplerLayoutInfo.bindingCount = PP_MAX_SAMPLERS_PER_PASS;
    samplerLayoutInfo.pBindings = samplerBindings;
    if (vkCreateDescriptorSetLayout(device, &samplerLayoutInfo, nullptr, &samplerSetLayout_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create sampler descriptor set layout\n";
        return false;
    }

    // --- Descriptor set layout 1: params UBO ---
    VkDescriptorSetLayoutBinding paramsBinding{};
    paramsBinding.binding = 0;
    paramsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    paramsBinding.descriptorCount = 1;
    paramsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo paramsLayoutInfo{};
    paramsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    paramsLayoutInfo.bindingCount = 1;
    paramsLayoutInfo.pBindings = &paramsBinding;
    if (vkCreateDescriptorSetLayout(device, &paramsLayoutInfo, nullptr, &paramsSetLayout_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create params descriptor set layout\n";
        return false;
    }

    // --- Pipeline layout (shared across all passes) ---
    VkDescriptorSetLayout setLayouts[2] = {samplerSetLayout_, paramsSetLayout_};
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PostProcessPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create pipeline layout\n";
        return false;
    }

    // --- Global descriptor pool: only for the shared params UBO sets ---
    // Per-pass sampler descriptor sets live in per-effect pools owned by the
    // Effect struct. This separation means loading/unloading an effect
    // doesn't touch the shared params descriptors.
    VkDescriptorPoolSize globalPoolSize{};
    globalPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalPoolSize.descriptorCount = 2; // one per frame-in-flight

    VkDescriptorPoolCreateInfo globalPoolInfo{};
    globalPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    globalPoolInfo.maxSets = 2;
    globalPoolInfo.poolSizeCount = 1;
    globalPoolInfo.pPoolSizes = &globalPoolSize;
    if (vkCreateDescriptorPool(device, &globalPoolInfo, nullptr, &globalDescPool_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create global descriptor pool\n";
        return false;
    }

    // --- Per-frame-in-flight params UBO buffers + descriptor sets ---
    for (int i = 0; i < 2; ++i)
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = sizeof(ParamsUBO);
        bufInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
        vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                         | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo vmaAllocInfo{};
        if (vmaCreateBuffer(ctx.GetAllocator(), &bufInfo, &vmaAllocCI,
                            &paramsBuffer_[i], &paramsAllocation_[i], &vmaAllocInfo) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] Failed to create params UBO buffer\n";
            return false;
        }
        paramsMapped_[i] = vmaAllocInfo.pMappedData;

        VkDescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = globalDescPool_;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &paramsSetLayout_;
        if (vkAllocateDescriptorSets(device, &dsAllocInfo, &paramsDescSet_[i]) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] Failed to allocate params descriptor set\n";
            return false;
        }

        VkDescriptorBufferInfo bufDesc{};
        bufDesc.buffer = paramsBuffer_[i];
        bufDesc.offset = 0;
        bufDesc.range = sizeof(ParamsUBO);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = paramsDescSet_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufDesc;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // --- Load the shared fullscreen vertex shader ---
    // Every post-process pass uses the same vertex shader (gl_VertexIndex-based
    // fullscreen triangle), so we load it once and reuse it across all passes.
#ifdef HAVE_SHADERC_SHADERC_HPP
    {
        tString fullscreenPath = tDirectories::Data().GetReadPath("shaders/postprocess/fullscreen.vert");
        if (fullscreenPath.Len() <= 1)
        {
            std::cerr << "[PostProcess] Cannot find shaders/postprocess/fullscreen.vert\n";
            return false;
        }
        tString shadersBase = tDirectories::Data().GetReadPath("shaders/postprocess");
        std::vector<std::string> includePaths;
        if (shadersBase.Len() > 1)
            includePaths.push_back(static_cast<const char*>(shadersBase));
        std::string err;
        fullscreenVert_ = rVulkanShader::CompileFromFile(
            device, static_cast<const char*>(fullscreenPath),
            rVulkanShader::Stage::Vertex, includePaths, &err);
        if (fullscreenVert_ == VK_NULL_HANDLE)
        {
            std::cerr << "[PostProcess] Failed to compile fullscreen vertex shader:\n" << err << "\n";
            return false;
        }
    }
#else
    // No shaderc (Android): load pre-compiled SPIR-V from APK assets.
    fullscreenVert_ = rVulkanShader::LoadFromFile(device, "shaders/postprocess/fullscreen.vert.spv");
    if (fullscreenVert_ == VK_NULL_HANDLE)
    {
        SDL_Log("[PostProcess] Failed to load shaders/postprocess/fullscreen.vert.spv");
        return false;
    }
#endif

    return true;
}

void rVulkanPostProcess::DestroyPipeline()
{
    if (device_ == VK_NULL_HANDLE) return;

    // Destroy all lazily-loaded effects.
    for (auto& kv : effects_)
    {
        DestroyEffect(kv.second);
    }
    effects_.clear();
    activeEffectPtr_ = nullptr;

    // Free per-frame params UBO memory.
    for (int i = 0; i < 2; ++i)
    {
        if (paramsBuffer_[i] != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx_->GetAllocator(), paramsBuffer_[i], paramsAllocation_[i]);
            paramsBuffer_[i]     = VK_NULL_HANDLE;
            paramsAllocation_[i] = VK_NULL_HANDLE;
            paramsMapped_[i]     = nullptr;
        }
        paramsDescSet_[i] = VK_NULL_HANDLE; // freed with the pool
    }

    if (fullscreenVert_)   { vkDestroyShaderModule(device_, fullscreenVert_, nullptr); fullscreenVert_ = VK_NULL_HANDLE; }
    if (globalDescPool_)   { vkDestroyDescriptorPool(device_, globalDescPool_, nullptr); globalDescPool_ = VK_NULL_HANDLE; }
    if (pipelineLayout_)   { vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (paramsSetLayout_)  { vkDestroyDescriptorSetLayout(device_, paramsSetLayout_, nullptr); paramsSetLayout_ = VK_NULL_HANDLE; }
    if (samplerSetLayout_) { vkDestroyDescriptorSetLayout(device_, samplerSetLayout_, nullptr); samplerSetLayout_ = VK_NULL_HANDLE; }
}

// ============================================================================
// Offscreen target (swapchain-dependent, rebuilt on resize)
// ============================================================================

bool rVulkanPostProcess::BuildOffscreen(rVulkanContext& ctx, uint32_t width, uint32_t height)
{
    if (!pipelineManager_)
    {
        std::cerr << "[PostProcess] BuildOffscreen called before SetPipelineManager — "
                     "cannot register 2-attachment render pass; post-processing disabled\n";
        return false;
    }

    VkDevice device = ctx.GetDevice();
    width_ = width;
    height_ = height;

    // --- Offscreen color image ---
    VkImageCreateInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.format = colorFormat_;
    colorInfo.extent.width = width;
    colorInfo.extent.height = height;
    colorInfo.extent.depth = 1;
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    {
        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(ctx.GetAllocator(), &colorInfo, &vmaAllocCI,
                           &offscreenColorImage_, &offscreenColorAllocation_, nullptr) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] Failed to create offscreen color image\n";
            return false;
        }
    }

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = offscreenColorImage_;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = colorFormat_;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &colorViewInfo, nullptr, &offscreenColorView_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen color view\n";
        return false;
    }

    // --- Offscreen emissive image ---
    // Second color attachment for per-component glow contribution.
    // Written by the uber shader's emissive hooks. Bloom samples this.
    VkImageCreateInfo emissiveInfo = colorInfo;
    // Same format as color so the pipeline's blend state is uniform.
    {
        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(ctx.GetAllocator(), &emissiveInfo, &vmaAllocCI,
                           &offscreenEmissiveImage_, &offscreenEmissiveAllocation_, nullptr) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] Failed to create offscreen emissive image\n";
            return false;
        }
    }

    VkImageViewCreateInfo emissiveViewInfo{};
    emissiveViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    emissiveViewInfo.image = offscreenEmissiveImage_;
    emissiveViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    emissiveViewInfo.format = colorFormat_;
    emissiveViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    emissiveViewInfo.subresourceRange.levelCount = 1;
    emissiveViewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &emissiveViewInfo, nullptr, &offscreenEmissiveView_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen emissive view\n";
        return false;
    }

    // --- Offscreen depth image ---
    VkImageCreateInfo depthInfo = colorInfo;
    depthInfo.format = depthFormat_;
    // SAMPLED_BIT so post-process shaders like cel-shading can read the
    // depth buffer via uSceneDepth sampler.
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;

    {
        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(ctx.GetAllocator(), &depthInfo, &vmaAllocCI,
                           &offscreenDepthImage_, &offscreenDepthAllocation_, nullptr) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] Failed to create offscreen depth image\n";
            return false;
        }
    }

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = offscreenDepthImage_;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat_;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &depthViewInfo, nullptr, &offscreenDepthView_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen depth view\n";
        return false;
    }

    // --- Sampler for reading the offscreen color texture in the post-process pass ---
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &offscreenSampler_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen sampler\n";
        return false;
    }

    // --- Offscreen render pass (scene renders into this) ---
    // 3 attachments: color (0), emissive (1), depth (2)
    // Both color attachments transition to SHADER_READ_ONLY_OPTIMAL at the
    // end of the pass so post-process effects can sample them directly.
    VkAttachmentDescription attachments[3]{};
    // Color (location = 0)
    attachments[0].format = colorFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Emissive (location = 1) — cleared to black. Hooks accumulate into it.
    attachments[1].format = colorFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Depth (index 2)
    attachments[2].format = depthFormat_;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // STORE the depth buffer so post-process shaders can sample it.
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRefs[2] = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    VkAttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2;
    subpass.pColorAttachments = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependency: the fragment shader in the subsequent post-process pass
    // must wait for BOTH color attachments and depth writes in this pass to
    // complete before sampling them.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 3;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;
    if (vkCreateRenderPass(device, &rpInfo, nullptr, &offscreenRenderPass_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen render pass\n";
        return false;
    }

    // Tell the scene pipeline manager this render pass has 2 color
    // attachments so it emits the correct color blend state. Without this,
    // scene pipelines targeting the offscreen pass get a single-attachment
    // blend state and Vulkan rejects them at creation time.
    if (pipelineManager_)
    {
        pipelineManager_->RegisterRenderPass(offscreenRenderPass_, 2);
    }

    // --- Offscreen framebuffer (3 attachments) ---
    VkImageView fbAttachments[3] = {
        offscreenColorView_,
        offscreenEmissiveView_,
        offscreenDepthView_,
    };
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = offscreenRenderPass_;
    fbInfo.attachmentCount = 3;
    fbInfo.pAttachments = fbAttachments;
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &offscreenFramebuffer_) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create offscreen framebuffer\n";
        return false;
    }

    offscreenBuilt_ = true;
    return true;
}

void rVulkanPostProcess::DestroyOffscreen()
{
    if (device_ == VK_NULL_HANDLE) return;
    if (offscreenFramebuffer_) { vkDestroyFramebuffer(device_, offscreenFramebuffer_, nullptr); offscreenFramebuffer_ = VK_NULL_HANDLE; }
    // Invalidate cached scene pipelines that reference the old offscreen
    // render pass before destroying it.
    if (offscreenRenderPass_ && pipelineManager_)
    {
        pipelineManager_->InvalidateRenderPass(offscreenRenderPass_);
    }
    if (offscreenRenderPass_)  { vkDestroyRenderPass(device_, offscreenRenderPass_, nullptr);   offscreenRenderPass_ = VK_NULL_HANDLE; }
    if (offscreenSampler_)     { vkDestroySampler(device_, offscreenSampler_, nullptr);         offscreenSampler_ = VK_NULL_HANDLE; }
    if (offscreenDepthView_) { vkDestroyImageView(device_, offscreenDepthView_, nullptr); offscreenDepthView_ = VK_NULL_HANDLE; }
    if (offscreenDepthImage_ != VK_NULL_HANDLE)
    {
        vmaDestroyImage(ctx_->GetAllocator(), offscreenDepthImage_, offscreenDepthAllocation_);
        offscreenDepthImage_ = VK_NULL_HANDLE; offscreenDepthAllocation_ = VK_NULL_HANDLE;
    }
    if (offscreenEmissiveView_) { vkDestroyImageView(device_, offscreenEmissiveView_, nullptr); offscreenEmissiveView_ = VK_NULL_HANDLE; }
    if (offscreenEmissiveImage_ != VK_NULL_HANDLE)
    {
        vmaDestroyImage(ctx_->GetAllocator(), offscreenEmissiveImage_, offscreenEmissiveAllocation_);
        offscreenEmissiveImage_ = VK_NULL_HANDLE; offscreenEmissiveAllocation_ = VK_NULL_HANDLE;
    }
    if (offscreenColorView_) { vkDestroyImageView(device_, offscreenColorView_, nullptr); offscreenColorView_ = VK_NULL_HANDLE; }
    if (offscreenColorImage_ != VK_NULL_HANDLE)
    {
        vmaDestroyImage(ctx_->GetAllocator(), offscreenColorImage_, offscreenColorAllocation_);
        offscreenColorImage_ = VK_NULL_HANDLE; offscreenColorAllocation_ = VK_NULL_HANDLE;
    }
    offscreenBuilt_ = false;
}

// ============================================================================
// Frame execution
// ============================================================================

// Arena bounds used by the post-process push constant block. Written by
// the renderer (which owns the per-camera arena extent). Static file-scope
// so the Execute() call doesn't need to thread it through a bunch of layers.
static float s_postProcessArenaBBox[4] = {0, 0, 0, 0};
void sr_vkPostProcessSetArenaBounds(float minX, float minY, float maxX, float maxY)
{
    s_postProcessArenaBBox[0] = minX;
    s_postProcessArenaBBox[1] = minY;
    s_postProcessArenaBBox[2] = maxX;
    s_postProcessArenaBBox[3] = maxY;
}

void rVulkanPostProcess::Execute(VkCommandBuffer cmd,
                                 uint32_t frameInFlight,
                                 VkFramebuffer swapchainFramebuffer,
                                 VkExtent2D extent,
                                 float time)
{
    if (!IsEnabled()) return;
    if (!activeEffectPtr_ || activeEffectPtr_->passes.empty()) return;

    // Pick up any pending MVP parameter changes from the config system
    // thread. This is a cheap flag check; when set, it re-applies the
    // MVP registry values onto currentParams_ for the active effect.
    if (mvpDirty_) ApplyMvpToCurrentParams();

    // Copy current parameter values into this frame's mapped UBO slot. The
    // UBO is HOST_VISIBLE | HOST_COHERENT so the write is visible without an
    // explicit flush. This write is shared across all passes in the effect.
    uint32_t frameSlot = frameInFlight % 2;
    if (paramsMapped_[frameSlot])
    {
        std::memcpy(paramsMapped_[frameSlot], &currentParams_, sizeof(ParamsUBO));
    }

    // Iterate the effect's passes in declaration order. Each pass samples
    // the outputs of earlier passes (via its descriptor set resolved at load
    // time) and writes to its declared target. The final pass (usually
    // targeting SWAPCHAIN) produces the output that lands on the presented
    // swapchain image.
    for (const auto& pass : activeEffectPtr_->passes)
    {
        // Resolve the framebuffer — SWAPCHAIN targets use the per-image
        // swapchain framebuffer provided by the caller, intermediate targets
        // use the framebuffer pre-built at effect load time.
        VkFramebuffer fb = pass.targetsSwapchain ? swapchainFramebuffer : pass.framebuffer;
        VkExtent2D    passExtent = pass.targetsSwapchain ? extent : pass.extent;

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = pass.renderPass;
        rpInfo.framebuffer = fb;
        rpInfo.renderArea.extent = passExtent;

        // The swapchain render pass has 2 attachments (color + depth), our
        // intermediate passes have 1. Provide clear values for both; unused
        // entries are ignored by Vulkan.
        VkClearValue clearValues[2]{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = pass.targetsSwapchain ? 2 : 1;
        rpInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width = static_cast<float>(passExtent.width);
        vp.height = static_cast<float>(passExtent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.extent = passExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push constants — resolution is per-pass (the pass's own target
        // extent, not the swapchain extent). Shaders doing kernel offsets
        // in texel units rely on this being the actual render target size.
        PostProcessPushConstants pc{};
        pc.resolution[0] = vp.width;
        pc.resolution[1] = vp.height;
        pc.time = time;
        pc.arenaBBox[0] = s_postProcessArenaBBox[0];
        pc.arenaBBox[1] = s_postProcessArenaBBox[1];
        pc.arenaBBox[2] = s_postProcessArenaBBox[2];
        pc.arenaBBox[3] = s_postProcessArenaBBox[3];
        vkCmdPushConstants(cmd, pass.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);

        // Set 0 — pass-specific sampler set resolved at effect load time.
        // Set 1 — shared params UBO for this frame-in-flight slot.
        VkDescriptorSet sets[2] = {pass.descSet[0], paramsDescSet_[frameSlot]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipelineLayout,
                                0, 2, sets, 0, nullptr);

        // Fullscreen triangle — 3 vertices, no vertex buffer.
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
}

// ============================================================================
// Effect loading — framegraph-lite: parse .pipeline, build passes, resolve
// sampler bindings against the scene attachments and intermediate pool.
// ============================================================================

// Try moviepack path first, then system path, for PP effect files.
static tString sg_PPGetReadPath(const std::string& sub)
{
    std::string mvSub = "moviepack/" + sub;
    tString path = tDirectories::Data().GetReadPath(mvSub.c_str());
    if (path.Len() > 1) return path;
    return tDirectories::Data().GetReadPath(sub.c_str());
}

rVulkanPostProcess::Effect* rVulkanPostProcess::EnsureEffectLoaded(const std::string& name)
{
    auto it = effects_.find(name);
    if (it != effects_.end()) return &it->second;

    Effect ef;

    // --- 1. Parse .pipeline file (or synthesize a default 1-pass descriptor) ---
    std::string pipelineSub = "shaders/postprocess/" + name + "/" + name + ".pipeline";
    tString pipelinePath = sg_PPGetReadPath(pipelineSub);
    if (pipelinePath.Len() > 1)
    {
        std::string err;
        if (!ParsePipelineFile(std::string(static_cast<const char*>(pipelinePath)), ef.desc, err))
        {
            std::cerr << "[PostProcess] " << err << "\n";
            return nullptr;
        }
    }
    else
    {
        // Default: single pass reading SCENE_COLOR+SCENE_DEPTH, writing to
        // SWAPCHAIN, using a fragment shader named <effect>.frag.spv. This
        // keeps single-pass effects trivial — no .pipeline file needed.
        rPostProcessPassDecl defaultPass;
        defaultPass.shader = name;
        defaultPass.target = "SWAPCHAIN";
        defaultPass.samplers.push_back({0, "SCENE_COLOR"});
        defaultPass.samplers.push_back({1, "SCENE_DEPTH"});
        ef.desc.passes.push_back(std::move(defaultPass));
    }

    // --- 2. Allocate the effect's intermediate render targets ---
    if (!ef.desc.resources.empty())
    {
        if (!ef.pool.Allocate(*ctx_, ef.desc.resources, {width_, height_}))
        {
            std::cerr << "[PostProcess] Failed to allocate resources for '" << name << "'\n";
            return nullptr;
        }
    }

    // --- 3. Create a per-effect descriptor pool for pass sampler sets ---
    uint32_t numPasses = static_cast<uint32_t>(ef.desc.passes.size());
    VkDescriptorPoolSize dpSize{};
    dpSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dpSize.descriptorCount = numPasses * PP_MAX_SAMPLERS_PER_PASS;

    VkDescriptorPoolCreateInfo dpCI{};
    dpCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpCI.maxSets = numPasses;
    dpCI.poolSizeCount = 1;
    dpCI.pPoolSizes = &dpSize;
    if (vkCreateDescriptorPool(device_, &dpCI, nullptr, &ef.descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create descriptor pool for '" << name << "'\n";
        ef.pool.Destroy(ctx_->GetAllocator(), device_);
        return nullptr;
    }

    // --- 4. Build each declared pass ---
    ef.passes.reserve(numPasses);
    for (const auto& passDecl : ef.desc.passes)
    {
        rPostProcessPass pass;
        if (!BuildPass(name, passDecl, ef.pool, ef.descriptorPool, pass))
        {
            std::cerr << "[PostProcess] Failed to build pass '" << passDecl.shader
                      << "' for effect '" << name << "'\n";
            DestroyPass(pass);
            // Destroy any passes already built.
            for (auto& p : ef.passes) DestroyPass(p);
            ef.passes.clear();
            vkDestroyDescriptorPool(device_, ef.descriptorPool, nullptr);
            ef.pool.Destroy(ctx_->GetAllocator(), device_);
            return nullptr;
        }
        ef.passes.push_back(std::move(pass));
    }

    // --- 5. Parse the .meta file for tunable parameters (optional) ---
    std::string metaSub = "shaders/postprocess/" + name + "/" + name + ".meta";
    tString metaPath = sg_PPGetReadPath(metaSub);
    if (metaPath.Len() > 1)
    {
        ParseMetaFile(std::string(static_cast<const char*>(metaPath)), ef.params);
    }

    // Debug: uncomment to trace effect loading
    // std::cerr << "[PostProcess] Loaded effect '" << name << "' with "
    //           << ef.passes.size() << " pass(es), "
    //           << ef.params.size() << " param(s), "
    //           << ef.desc.resources.size() << " intermediate resource(s)\n";

    auto emplaced = effects_.emplace(name, std::move(ef));

    // Register MVP tSettingItems for this effect's parameters so users can
    // tune them live via the console. The registration is idempotent — if
    // the same effect is loaded twice, duplicate registrations are skipped.
    RegisterEffectMvp(name, emplaced.first->second.params);

    return &emplaced.first->second;
}

// Build one pass (shader + render pass + framebuffer + pipeline + descriptor set)
// from a PassDecl. Partial state on failure is the caller's problem — the caller
// should DestroyPass(pass) on failure.
bool rVulkanPostProcess::BuildPass(const std::string& effectName,
                                    const rPostProcessPassDecl& decl,
                                    rPostProcessResourcePool& pool,
                                    VkDescriptorPool descPool,
                                    rPostProcessPass& outPass)
{
    outPass.debugShader = decl.shader;
    outPass.debugTarget = decl.target;
    outPass.targetsSwapchain = (decl.target == "SWAPCHAIN");
    outPass.samplerCount = static_cast<uint32_t>(decl.samplers.size());
    outPass.set0Layout = samplerSetLayout_;     // shared across all passes
    outPass.pipelineLayout = pipelineLayout_;   // shared across all passes

    // --- Load the fragment shader ---
    // Shader basename from the PASS line → shaders/postprocess/<effect>/<basename>.frag
#ifdef HAVE_SHADERC_SHADERC_HPP
    {
        std::string shaderSub = "shaders/postprocess/" + effectName + "/" + decl.shader + ".frag";
        tString shaderPath = sg_PPGetReadPath(shaderSub);
        if (shaderPath.Len() <= 1)
        {
            std::cerr << "[PostProcess] Shader source not found: " << shaderSub << "\n";
            return false;
        }
        // Build include paths: moviepack effect dir first, then system dirs
        std::vector<std::string> includePaths;
        std::string effectSub = "shaders/postprocess/" + effectName;
        tString mvEffectDir = sg_PPGetReadPath(effectSub);
        if (mvEffectDir.Len() > 1)
            includePaths.push_back(static_cast<const char*>(mvEffectDir));
        // Also add system effect dir if different from moviepack dir
        tString sysEffectDir = tDirectories::Data().GetReadPath(effectSub.c_str());
        if (sysEffectDir.Len() > 1) {
            std::string s = static_cast<const char*>(sysEffectDir);
            if (includePaths.empty() || includePaths.back() != s)
                includePaths.push_back(s);
        }
        tString ppDir = tDirectories::Data().GetReadPath("shaders/postprocess");
        if (ppDir.Len() > 1)
            includePaths.push_back(static_cast<const char*>(ppDir));
        tString baseDir = tDirectories::Data().GetReadPath("shaders");
        if (baseDir.Len() > 1)
            includePaths.push_back(static_cast<const char*>(baseDir));
        std::string err;
        outPass.fragShader = rVulkanShader::CompileFromFile(
            device_, static_cast<const char*>(shaderPath),
            rVulkanShader::Stage::Fragment, includePaths, &err);
        if (outPass.fragShader == VK_NULL_HANDLE)
        {
            std::cerr << "[PostProcess] Shader compile failed for '" << shaderSub << "':\n"
                      << err << "\n";
            return false;
        }
    }
#else
    // No shaderc (Android): load pre-compiled SPIR-V from APK assets.
    {
        std::string spvPath = "shaders/postprocess/" + effectName + "/" + decl.shader + ".frag.spv";
        outPass.fragShader = rVulkanShader::LoadFromFile(device_, spvPath.c_str());
        if (outPass.fragShader == VK_NULL_HANDLE)
        {
            SDL_Log("[PostProcess] Failed to load pre-compiled SPV: %s", spvPath.c_str());
            return false;
        }
    }
#endif

    // --- Render pass + framebuffer ---
    // SWAPCHAIN-targeted passes reuse the existing main render pass (which is
    // render-pass compatible with a single-color-attachment pipeline). Their
    // framebuffer is resolved per-frame in Execute from the acquired swapchain
    // image index.
    //
    // Intermediate-target passes get a dedicated 1-attachment render pass and
    // pre-built framebuffer pointing at the pool resource's view.
    if (outPass.targetsSwapchain)
    {
        outPass.renderPass = swapchainRenderPass_;
        outPass.framebuffer = VK_NULL_HANDLE; // resolved at Execute time
        outPass.extent.width = width_;
        outPass.extent.height = height_;
    }
    else
    {
        const auto* targetRes = pool.Get(decl.target);
        if (!targetRes)
        {
            std::cerr << "[PostProcess] Pass '" << decl.shader << "' target '" << decl.target
                      << "' not found in resource pool\n";
            return false;
        }
        outPass.extent = targetRes->extent;

        // 1-attachment render pass with implicit layout transition to
        // SHADER_READ_ONLY_OPTIMAL so the NEXT pass can sample this one's
        // output without an explicit barrier.
        VkAttachmentDescription attach{};
        attach.format = targetRes->format;
        attach.samples = VK_SAMPLE_COUNT_1_BIT;
        attach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // we overwrite every pixel
        attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attach.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpCI{};
        rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCI.attachmentCount = 1;
        rpCI.pAttachments = &attach;
        rpCI.subpassCount = 1;
        rpCI.pSubpasses = &subpass;
        rpCI.dependencyCount = 1;
        rpCI.pDependencies = &dep;
        if (vkCreateRenderPass(device_, &rpCI, nullptr, &outPass.renderPass) != VK_SUCCESS)
        {
            return false;
        }

        VkFramebufferCreateInfo fbCI{};
        fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass = outPass.renderPass;
        fbCI.attachmentCount = 1;
        fbCI.pAttachments = &targetRes->view;
        fbCI.width = outPass.extent.width;
        fbCI.height = outPass.extent.height;
        fbCI.layers = 1;
        if (vkCreateFramebuffer(device_, &fbCI, nullptr, &outPass.framebuffer) != VK_SUCCESS)
        {
            return false;
        }
    }

    // --- Pipeline ---
    outPass.pipeline = CreatePassPipeline(outPass.fragShader, outPass.renderPass);
    if (outPass.pipeline == VK_NULL_HANDLE) return false;

    // --- Descriptor set for set 0 (samplers) ---
    // One set per pass is enough — the sampled image views don't change per
    // frame (the scene writes to the same offscreen image every frame).
    VkDescriptorSetAllocateInfo dsAI{};
    dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAI.descriptorPool = descPool;
    dsAI.descriptorSetCount = 1;
    dsAI.pSetLayouts = &samplerSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dsAI, &outPass.descSet[0]) != VK_SUCCESS)
    {
        return false;
    }

    WritePassDescriptors(outPass, decl, pool);
    return true;
}

void rVulkanPostProcess::WritePassDescriptors(rPostProcessPass& pass,
                                               const rPostProcessPassDecl& decl,
                                               const rPostProcessResourcePool& pool)
{
    // All PP_MAX_SAMPLERS_PER_PASS slots must be populated — the descriptor
    // set layout declares all of them, and Vulkan requires a valid descriptor
    // for every binding even if the shader doesn't reference it. Unused slots
    // get the offscreen color view as a safe placeholder.
    VkDescriptorImageInfo imgInfos[PP_MAX_SAMPLERS_PER_PASS]{};
    VkWriteDescriptorSet  writes[PP_MAX_SAMPLERS_PER_PASS]{};
    for (int i = 0; i < PP_MAX_SAMPLERS_PER_PASS; ++i)
    {
        imgInfos[i].sampler = offscreenSampler_;
        imgInfos[i].imageView = offscreenColorView_;
        imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = pass.descSet[0];
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imgInfos[i];
    }

    // Override slots that the .pipeline file declared with actual sources.
    for (const auto& smp : decl.samplers)
    {
        if (smp.binding < 0 || smp.binding >= PP_MAX_SAMPLERS_PER_PASS) continue;

        VkImageView  view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (smp.source == "SCENE_COLOR")
        {
            view = offscreenColorView_;
        }
        else if (smp.source == "SCENE_DEPTH")
        {
            view = offscreenDepthView_;
            layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else if (smp.source == "SCENE_EMISSIVE")
        {
            view = offscreenEmissiveView_;
        }
        else
        {
            const auto* res = pool.Get(smp.source);
            if (res) view = res->view;
            else
            {
                std::cerr << "[PostProcess] Unknown sampler source '" << smp.source
                          << "' in pass '" << pass.debugShader << "'\n";
            }
        }

        if (view != VK_NULL_HANDLE)
        {
            imgInfos[smp.binding].imageView = view;
            imgInfos[smp.binding].imageLayout = layout;
        }
    }

    vkUpdateDescriptorSets(device_, PP_MAX_SAMPLERS_PER_PASS, writes, 0, nullptr);
}

VkPipeline rVulkanPostProcess::CreatePassPipeline(VkShaderModule frag, VkRenderPass renderPass)
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = fullscreenVert_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &pipeline) != VK_SUCCESS)
    {
        std::cerr << "[PostProcess] Failed to create pass pipeline\n";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

void rVulkanPostProcess::DestroyPass(rPostProcessPass& pass)
{
    if (device_ == VK_NULL_HANDLE) return;
    if (pass.pipeline)     { vkDestroyPipeline(device_, pass.pipeline, nullptr); pass.pipeline = VK_NULL_HANDLE; }
    // Only destroy the framebuffer and render pass for intermediate targets —
    // SWAPCHAIN passes borrow the main swapchain render pass and resolve the
    // framebuffer per-frame.
    if (!pass.targetsSwapchain)
    {
        if (pass.framebuffer) { vkDestroyFramebuffer(device_, pass.framebuffer, nullptr); pass.framebuffer = VK_NULL_HANDLE; }
        if (pass.renderPass)  { vkDestroyRenderPass(device_, pass.renderPass, nullptr);   pass.renderPass = VK_NULL_HANDLE; }
    }
    else
    {
        pass.framebuffer = VK_NULL_HANDLE;
        pass.renderPass = VK_NULL_HANDLE;
    }
    if (pass.fragShader) { vkDestroyShaderModule(device_, pass.fragShader, nullptr); pass.fragShader = VK_NULL_HANDLE; }
    // descSet is freed when the per-effect descriptor pool is destroyed.
    pass.descSet[0] = VK_NULL_HANDLE;
    pass.descSet[1] = VK_NULL_HANDLE;
}

void rVulkanPostProcess::DestroyEffect(Effect& ef)
{
    if (device_ == VK_NULL_HANDLE) return;
    for (auto& pass : ef.passes) DestroyPass(pass);
    ef.passes.clear();
    if (ef.descriptorPool)
    {
        vkDestroyDescriptorPool(device_, ef.descriptorPool, nullptr);
        ef.descriptorPool = VK_NULL_HANDLE;
    }
    ef.pool.Destroy(ctx_->GetAllocator(), device_);
    ef.desc.resources.clear();
    ef.desc.passes.clear();
    ef.params.clear();
}

// ============================================================================
// MVP (moviepack variable) — dynamic per-effect parameter settings.
// ============================================================================
//
// Shader authors declare tunable parameters in .meta files. At effect load
// time we register a tSettingItem for each param, named MVP_<EFFECT>_<PARAM>.
// Users change values from the console; the tSettingItem writes to our
// backing storage and fires a callback that sets mvpDirty_. The next
// BeginFrame applies the updated values to currentParams_.
//
// tSettingItem<T> is used (not tConfItem<T>) so these items are never saved
// to user.cfg. Persistence is done manually to per-moviepack cfg files.

// s_mvpOwner is defined at the top of this file.

static void sr_mvpChanged()
{
    if (s_mvpOwner) s_mvpOwner->NotifyMvpChanged();
}

// Uppercase copy of a string. Used to build MVP config item names
// (MVP_BLOOM_INTENSITY) from effect/param names.
static std::string MvpUpper(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// Build the tSettingItem title for an MVP entry.
static std::string MvpTitle(const std::string& effect, const std::string& param)
{
    std::string t = "MVP_" + MvpUpper(effect) + "_" + MvpUpper(param);
    // tConfItem titles should not contain spaces — replace any in the param
    // name (effect names are directory names, no spaces expected).
    for (char& c : t) if (c == ' ') c = '_';
    return t;
}

void rVulkanPostProcess::RegisterEffectMvp(const std::string& effectName,
                                            const std::vector<rPostProcessParam>& params)
{
    s_mvpOwner = this; // needed by sr_mvpChanged callback

    for (const auto& p : params)
    {
        // Vec4 MVP tuning is not yet supported — splitting into 4 scalar
        // items is doable but would quadruple the MVP_ namespace for colors.
        // Skip them for now; the effect's default tint is used.
        if (p.type == rPostProcessParam::Vec4) continue;

        std::string title = MvpTitle(effectName, p.name);
        if (mvpRegistry_.count(title)) continue; // already registered

        MvpEntry entry;
        entry.effectName = effectName;
        entry.paramName  = p.name;
        entry.slot       = p.slot;
        entry.minVal     = p.minVal;
        entry.maxVal     = p.maxVal;
        entry.type       = (p.type == rPostProcessParam::Int) ? MvpEntry::Int : MvpEntry::Float;
        if (entry.type == MvpEntry::Int)
            entry.iValue = static_cast<int>(p.defaults[0]);
        else
            entry.fValue = p.defaults[0];

        auto [it, ok] = mvpRegistry_.emplace(title, std::move(entry));
        MvpEntry& e = it->second;

        // Heap-allocate the tSettingItem. Ownership stays with mvpRegistry_
        // (stored as void* to avoid including tConfiguration.h in the header).
        // The tSettingItem constructor takes a reference to the backing var,
        // so we pass references to the MvpEntry fields.
        if (e.type == MvpEntry::Int)
        {
            e.confItem = static_cast<void*>(
                new tSettingItem<int>(title.c_str(), e.iValue, &sr_mvpChanged));
        }
        else
        {
            e.confItem = static_cast<void*>(
                new tSettingItem<float>(title.c_str(), e.fValue, &sr_mvpChanged));
        }
    }

    // Ensure the registry values propagate into currentParams_ on the
    // next frame (in case the active effect's defaults just got loaded).
    mvpDirty_ = true;
}

void rVulkanPostProcess::UnregisterAllMvp()
{
    for (auto& kv : mvpRegistry_)
    {
        MvpEntry& e = kv.second;
        if (!e.confItem) continue;
        // The stored pointer is always a tSettingItem — we polymorphically
        // delete via tConfItemBase which is the virtual base of both float
        // and int specializations. Both derive from tConfItemBase which has
        // a virtual destructor, so the correct derived destructor runs.
        if (e.type == MvpEntry::Int)
            delete static_cast<tSettingItem<int>*>(e.confItem);
        else
            delete static_cast<tSettingItem<float>*>(e.confItem);
        e.confItem = nullptr;
    }
    mvpRegistry_.clear();
    // Don't clear s_mvpOwner — Destroy() will do that if we're shutting down.
}

void rVulkanPostProcess::ApplyMvpToCurrentParams()
{
    // Walk the registry and copy values for the currently active effect
    // into currentParams_. Entries for other effects are ignored here but
    // remain registered (their values are applied the next time their
    // effect becomes active via SetActiveEffect -> SeedEffectDefaults +
    // this function).
    for (const auto& kv : mvpRegistry_)
    {
        const MvpEntry& e = kv.second;
        if (e.effectName != activeEffect_) continue;
        if (e.slot < 0) continue;

        if (e.type == MvpEntry::Int)
        {
            if (e.slot < 16)
            {
                int v = e.iValue;
                // Clamp to declared range.
                int lo = static_cast<int>(e.minVal);
                int hi = static_cast<int>(e.maxVal);
                if (hi > lo) v = std::max(lo, std::min(hi, v));
                currentParams_.iparams[e.slot / 4][e.slot % 4] = v;
            }
        }
        else
        {
            if (e.slot < 32)
            {
                float v = std::max(e.minVal, std::min(e.maxVal, e.fValue));
                currentParams_.fparams[e.slot / 4][e.slot % 4] = v;
            }
        }
    }
    mvpDirty_ = false;
}

std::string rVulkanPostProcess::GetMvpConfigPath(const std::string& moviepackName)
{
    // Write MVP values to the user config dir next to user.cfg. When a
    // moviepack is active we use moviepack_<name>.cfg; otherwise just
    // postprocess.cfg for the base game.
    //
    // tDirectories::Var().GetWritePath rejects empty filenames (that's a
    // runtime ERROR in the console), so we pass the real filename we want
    // and let it compute the absolute path directly — no string splicing.
    const std::string filename = moviepackName.empty()
        ? std::string("postprocess.cfg")
        : "moviepack_" + moviepackName + ".cfg";
    tString full = tDirectories::Var().GetWritePath(filename.c_str());
    if (full.Len() > 1) return std::string(static_cast<const char*>(full));
    return filename; // fall back to a relative path; caller will handle failure
}

void rVulkanPostProcess::LoadMvpConfigFile(const std::string& moviepackName)
{
    std::string path = GetMvpConfigPath(moviepackName);
    std::ifstream f(path);
    if (!f.good()) return; // no file yet, use defaults

    // Simple line-oriented parser: "MVP_NAME value". We don't reuse
    // tConfItemBase::LoadAll because that parses every line against the
    // global registry and would also print warnings for unknown keys in
    // the file — we want per-moviepack files to ignore unknown keys
    // silently (the user may have removed an effect that used to exist).
    std::string line;
    while (std::getline(f, line))
    {
        // Strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);

        std::istringstream iss(line);
        std::string key;
        if (!(iss >> key)) continue;

        auto it = mvpRegistry_.find(key);
        if (it == mvpRegistry_.end()) continue;

        MvpEntry& e = it->second;
        if (e.type == MvpEntry::Int)
            iss >> e.iValue;
        else
            iss >> e.fValue;
    }
    mvpDirty_ = true;
}

void rVulkanPostProcess::SaveMvpConfigFile(const std::string& moviepackName) const
{
    if (mvpRegistry_.empty()) return;

    std::string path = GetMvpConfigPath(moviepackName);
    std::ofstream f(path, std::ios::trunc);
    if (!f.good())
    {
        std::cerr << "[PostProcess] Cannot write MVP config: " << path << "\n";
        return;
    }

    f << "# Armagetron Advanced post-process parameters\n";
    f << "# " << (moviepackName.empty() ? "base game" : ("moviepack: " + moviepackName)) << "\n";
    f << "# Auto-generated — edits here are applied when the moviepack activates.\n\n";

    for (const auto& kv : mvpRegistry_)
    {
        const MvpEntry& e = kv.second;
        f << kv.first << " ";
        if (e.type == MvpEntry::Int)
            f << e.iValue;
        else
            f << e.fValue;
        f << "\n";
    }
}

void rVulkanPostProcess::OnMoviepackActivated(const std::string& moviepackName)
{
    // Save current values under the OLD moviepack name first. On first
    // activation mvpMoviepackName_ is empty, so this writes to
    // postprocess.cfg (the base-game default location).
    if (!mvpRegistry_.empty())
        SaveMvpConfigFile(mvpMoviepackName_);

    // Tear down existing MVP registrations — the new moviepack may ship
    // different effects or different .meta contents, and we want a clean
    // slate. These will be re-registered lazily as effects load below.
    UnregisterAllMvp();

    // Destroy all cached effects — their shader modules, render passes,
    // framebuffers, descriptor sets, pipelines and intermediate pool are
    // all GPU resources tied to the NOW-STALE offscreen render pass (which
    // gets rebuilt when BuildOffscreen runs). Destroying here is only safe
    // because moviepack activation calls vkDeviceWaitIdle upstream via
    // sr_vkRendererReloadShaders.
    for (auto& kv : effects_)
        DestroyEffect(kv.second);
    effects_.clear();
    activeEffectPtr_ = nullptr;

    mvpMoviepackName_ = moviepackName;

    // Reload the active effect NOW, while we're still in the moviepack
    // activate code path (which has already called vkDeviceWaitIdle).
    // Otherwise the first frame after activation would find
    // activeEffectPtr_ == nullptr, Execute would early-return, the
    // swapchain image would never transition to PRESENT_SRC_KHR, and the
    // user would see the screen flicker between stale/undefined contents.
    //
    // Only reload if the offscreen target is actually built — if PP is
    // disabled we don't need an effect, and if it's enabled but the
    // offscreen isn't built yet we'd hit the same null-view descriptor
    // issue that SetActiveEffect's deferred path exists to avoid.
    if (offscreenBuilt_ && !activeEffect_.empty())
    {
        Effect* ef = EnsureEffectLoaded(activeEffect_);
        if (ef)
        {
            activeEffectPtr_ = ef;
            SeedEffectDefaults(*ef);
        }
        else
        {
            std::cerr << "[PostProcess] Failed to re-load active effect '"
                      << activeEffect_ << "' after moviepack activation\n";
        }
    }

    // Load overrides from the per-moviepack cfg file. EnsureEffectLoaded
    // above populated mvpRegistry_ for the active effect via
    // RegisterEffectMvp, so the Load here has entries to match against.
    LoadMvpConfigFile(mvpMoviepackName_);
}

void rVulkanPostProcess::OnMoviepackDeactivated()
{
    SaveMvpConfigFile(mvpMoviepackName_);
    UnregisterAllMvp();

    // Effects loaded from the deactivated moviepack are now stale —
    // destroy them so the next activation re-loads from disk.
    for (auto& kv : effects_)
        DestroyEffect(kv.second);
    effects_.clear();
    activeEffectPtr_ = nullptr;

    mvpMoviepackName_.clear();

    // Reload the active effect from the base-game shader directory so the
    // renderer has a valid pipeline to bind next frame. Same rationale as
    // OnMoviepackActivated: without this, activeEffectPtr_ stays null and
    // Execute early-returns, leaving the swapchain image untouched.
    if (offscreenBuilt_ && !activeEffect_.empty())
    {
        Effect* ef = EnsureEffectLoaded(activeEffect_);
        if (ef)
        {
            activeEffectPtr_ = ef;
            SeedEffectDefaults(*ef);
        }
    }
    LoadMvpConfigFile(mvpMoviepackName_);
}

// ============================================================================
// Resource pool — named intermediate render targets for multi-pass effects.
// ============================================================================

static VkFormat rPPFormatToVk(rPostProcessFormat f)
{
    switch (f)
    {
        case rPostProcessFormat::RGBA8:   return VK_FORMAT_R8G8B8A8_UNORM;
        case rPostProcessFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case rPostProcessFormat::R8:      return VK_FORMAT_R8_UNORM;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

bool rPostProcessResourcePool::Allocate(rVulkanContext& ctx,
                                        const std::vector<rPostProcessResourceDecl>& decls,
                                        VkExtent2D baseExtent)
{
    // Fresh allocation every time — destroy anything left over.
    Destroy(ctx.GetAllocator(), ctx.GetDevice());

    VkDevice device = ctx.GetDevice();

    for (const auto& decl : decls)
    {
        Resource res;
        res.format = rPPFormatToVk(decl.format);
        res.extent.width  = std::max(1u, static_cast<uint32_t>(baseExtent.width  * decl.scale));
        res.extent.height = std::max(1u, static_cast<uint32_t>(baseExtent.height * decl.scale));

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = res.format;
        imgInfo.extent.width = res.extent.width;
        imgInfo.extent.height = res.extent.height;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(ctx.GetAllocator(), &imgInfo, &vmaAllocCI,
                           &res.image, &res.allocation, nullptr) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] ResourcePool: failed to create image '"
                      << decl.name << "'\n";
            Destroy(ctx.GetAllocator(), device);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = res.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = res.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &res.view) != VK_SUCCESS)
        {
            std::cerr << "[PostProcess] ResourcePool: failed to create view for '"
                      << decl.name << "'\n";
            Destroy(ctx.GetAllocator(), device);
            return false;
        }

        resources_[decl.name] = res;
    }
    return true;
}

void rPostProcessResourcePool::Destroy(VmaAllocator allocator, VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;
    for (auto& kv : resources_)
    {
        Resource& r = kv.second;
        if (r.view)  { vkDestroyImageView(device, r.view, nullptr); r.view = VK_NULL_HANDLE; }
        if (r.image) { vmaDestroyImage(allocator, r.image, r.allocation); r.image = VK_NULL_HANDLE; r.allocation = VK_NULL_HANDLE; }
    }
    resources_.clear();
}

const rPostProcessResourcePool::Resource*
rPostProcessResourcePool::Get(const std::string& name) const
{
    auto it = resources_.find(name);
    return it == resources_.end() ? nullptr : &it->second;
}

// ============================================================================
// .pipeline file parser — declarative multi-pass effect description.
// ============================================================================
//
// Format (line-oriented, # and // comments, whitespace insensitive):
//
//   RESOURCE <name> <format> <scale>
//     format is one of: rgba8, rgba16f, r8
//     scale is a float multiplier of the swapchain extent (0.5 = half res)
//
//   PASS <shader_basename> <target>
//     target is an intermediate resource name or SWAPCHAIN
//     followed by one or more SAMPLER lines (indented or not)
//
//   SAMPLER <binding> <source>
//     source is an intermediate resource name or one of the built-ins:
//     SCENE_COLOR, SCENE_EMISSIVE, SCENE_DEPTH
//
// See shaders/postprocess/bloom/bloom.pipeline for a multi-pass example.

bool rVulkanPostProcess::ParsePipelineFile(const std::string& path,
                                           rPostProcessPipelineDesc& outDesc,
                                           std::string& outError)
{
    std::ifstream f(path);
    if (!f.good())
    {
        outError = "cannot open pipeline file: " + path;
        return false;
    }

    outDesc.resources.clear();
    outDesc.passes.clear();

    std::string line;
    int lineNo = 0;
    rPostProcessPassDecl* currentPass = nullptr;
    while (std::getline(f, line))
    {
        ++lineNo;
        // Strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto slash = line.find("//");
        if (slash != std::string::npos) line.erase(slash);

        std::istringstream iss(line);
        std::string tag;
        if (!(iss >> tag)) continue;

        if (tag == "RESOURCE")
        {
            rPostProcessResourceDecl decl;
            std::string format;
            if (!(iss >> decl.name >> format >> decl.scale))
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": RESOURCE needs <name> <format> <scale>";
                return false;
            }
            if      (format == "rgba8")   decl.format = rPostProcessFormat::RGBA8;
            else if (format == "rgba16f") decl.format = rPostProcessFormat::RGBA16F;
            else if (format == "r8")      decl.format = rPostProcessFormat::R8;
            else
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": unknown RESOURCE format '" + format + "'";
                return false;
            }
            outDesc.resources.push_back(std::move(decl));
            currentPass = nullptr;
        }
        else if (tag == "PASS")
        {
            if (outDesc.passes.size() >= PP_MAX_PASSES_PER_EFFECT)
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": too many PASS entries (max " +
                           std::to_string(PP_MAX_PASSES_PER_EFFECT) + ")";
                return false;
            }
            rPostProcessPassDecl decl;
            if (!(iss >> decl.shader >> decl.target))
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": PASS needs <shader> <target>";
                return false;
            }
            outDesc.passes.push_back(std::move(decl));
            currentPass = &outDesc.passes.back();
        }
        else if (tag == "SAMPLER")
        {
            if (!currentPass)
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": SAMPLER outside of a PASS";
                return false;
            }
            if (static_cast<int>(currentPass->samplers.size()) >= PP_MAX_SAMPLERS_PER_PASS)
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": too many SAMPLERs in one pass (max " +
                           std::to_string(PP_MAX_SAMPLERS_PER_PASS) + ")";
                return false;
            }
            rPostProcessSamplerDecl s;
            if (!(iss >> s.binding >> s.source))
            {
                outError = path + ":" + std::to_string(lineNo) +
                           ": SAMPLER needs <binding> <source>";
                return false;
            }
            currentPass->samplers.push_back(std::move(s));
        }
        // Silently ignore unknown tags so we can add new ones later without
        // breaking old .pipeline files.
    }

    if (outDesc.passes.empty())
    {
        outError = path + ": no PASS entries";
        return false;
    }
    return true;
}

// .meta file format (line-oriented, comments with # or //, whitespace ignored):
//
//   NAME Bloom (Tron Glow)
//   DESCRIPTION Bright-pass extraction with blur
//   PARAM_FLOAT <name> <slot> <default> <min> <max> <description...>
//   PARAM_INT   <name> <slot> <default> <min> <max> <description...>
//   PARAM_VEC4  <name> <slot> <r> <g> <b> <a> <description...>
//
// Slots index into fparams[] (float/vec4) or iparams[] (int). Vec4 occupies
// 4 consecutive float slots starting at `slot`.
void rVulkanPostProcess::ParseMetaFile(const std::string& path,
                                       std::vector<rPostProcessParam>& outParams)
{
    std::ifstream f(path);
    if (!f.good()) return;

    std::string line;
    while (std::getline(f, line))
    {
        // Strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto slash = line.find("//");
        if (slash != std::string::npos) line.erase(slash);

        std::istringstream iss(line);
        std::string tag;
        if (!(iss >> tag)) continue;

        if (tag == "PARAM_FLOAT" || tag == "PARAM_INT")
        {
            rPostProcessParam p;
            p.type = (tag == "PARAM_INT") ? rPostProcessParam::Int : rPostProcessParam::Float;
            if (!(iss >> p.name >> p.slot >> p.defaults[0] >> p.minVal >> p.maxVal))
                continue;
            std::getline(iss, p.description);
            // Trim leading whitespace from description
            size_t start = p.description.find_first_not_of(" \t");
            if (start != std::string::npos) p.description = p.description.substr(start);
            outParams.push_back(std::move(p));
        }
        else if (tag == "PARAM_VEC4")
        {
            rPostProcessParam p;
            p.type = rPostProcessParam::Vec4;
            if (!(iss >> p.name >> p.slot
                      >> p.defaults[0] >> p.defaults[1]
                      >> p.defaults[2] >> p.defaults[3]))
                continue;
            p.minVal = 0.0f;
            p.maxVal = 1.0f;
            std::getline(iss, p.description);
            size_t start = p.description.find_first_not_of(" \t");
            if (start != std::string::npos) p.description = p.description.substr(start);
            outParams.push_back(std::move(p));
        }
        // NAME / DESCRIPTION lines are informational only — ignored here.
    }
}

#endif // DEDICATED
