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

#include "rVulkanPipeline.h"
#include "rVertex.h"
#include <iostream>
#include <array>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>

static std::string GetPipelineCachePath()
{
    // Prefer XDG_CACHE_HOME; fall back to $HOME/.cache per the XDG base-dir spec.
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::string cacheBase;
    if (xdg && xdg[0] != '\0')
    {
        cacheBase = xdg;
    }
    else
    {
        const char* home = std::getenv("HOME");
        if (!home) return "";
        cacheBase = std::string(home) + "/.cache";
    }
    // Create parent directory if needed (e.g., ~/.cache may not exist)
    mkdir(cacheBase.c_str(), 0755);
    std::string dir = cacheBase + "/armagetronad";
    mkdir(dir.c_str(), 0755);
    return dir + "/vk_pipeline_cache.bin";
}

bool rVulkanPipelineManager::Init(VkDevice device, VkRenderPass renderPass,
                                  VkShaderModule vertShader, VkShaderModule fragShader,
                                  VkShaderModule fragShaderEmissive,
                                  const VkDescriptorSetLayout* setLayouts, uint32_t setLayoutCount,
                                  VkPipelineLayout* outLayout)
{
    device_ = device;
    renderPass_ = renderPass;
    vertShader_ = vertShader;
    fragShader_ = fragShader;
    fragShaderEmissive_ = fragShaderEmissive;

    // Push constant range for per-draw data (MVP, flags)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 192; // 64 bytes MVP + 64 bytes texMatrix + 64 bytes normalMatrix

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = setLayoutCount;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create pipeline layout" << std::endl;
        return false;
    }

    if (outLayout)
        *outLayout = layout_;

    // Load pipeline cache from disk (on-disk blob is validated by the driver)
    std::vector<uint8_t> cacheData;
    std::string cachePath = GetPipelineCachePath();
    if (!cachePath.empty())
    {
        std::ifstream f(cachePath, std::ios::binary | std::ios::ate);
        if (f.is_open())
        {
            auto sz = f.tellg();
            if (sz > 0)
            {
                cacheData.resize(static_cast<size_t>(sz));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(cacheData.data()), sz);
            }
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData    = cacheData.empty() ? nullptr : cacheData.data();
    if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache_) != VK_SUCCESS)
        pipelineCache_ = VK_NULL_HANDLE; // non-fatal: pipelines will compile without cache

    return true;
}

VkPipeline rVulkanPipelineManager::GetPipeline(const rVulkanPipelineKey& key)
{
    // Normalize: when culling is off, winding order is irrelevant — canonicalize
    // to reduce duplicate pipeline entries.
    rVulkanPipelineKey nkey = key;
    if (!nkey.cullFace)
        nkey.frontFaceCW = true;

    FullPipelineKey fullKey{renderPass_, nkey};
    auto it = cache_.find(fullKey);
    if (it != cache_.end())
        return it->second;

    VkPipeline pipeline = CreatePipeline(nkey);
    if (pipeline != VK_NULL_HANDLE)
        cache_[fullKey] = pipeline;
    return pipeline;
}

VkPipeline rVulkanPipelineManager::CreatePipeline(const rVulkanPipelineKey& key)
{
    // Look up the current render pass's color attachment count so we can
    // match the fragment shader variant. Must happen BEFORE the shader
    // stage array is filled in because the shader module selection depends
    // on it. See colorBlend setup later in this function for the matching
    // attachment-count logic.
    uint32_t thisPassColorCount = 1;
    {
        auto it = renderPassColorCounts_.find(renderPass_);
        if (it != renderPassColorCounts_.end())
            thisPassColorCount = it->second;
    }

    // Shader stages. Pick the "emissive" variant (writes to location 1)
    // for render passes with 2+ color attachments — i.e. the post-process
    // offscreen pass. For 1-attachment passes (swapchain pass), use the
    // plain variant so we don't get Vulkan validation warnings about
    // writes to nonexistent pColorAttachments[1].
    VkShaderModule chosenFragShader =
        (thisPassColorCount >= 2 && fragShaderEmissive_ != VK_NULL_HANDLE)
            ? fragShaderEmissive_
            : fragShader_;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = (key.instanced && vertShaderInstanced_ != VK_NULL_HANDLE)
                        ? vertShaderInstanced_ : vertShader_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = chosenFragShader;
    stages[1].pName = "main";

    // Vertex input — two formats depending on litVertex flag
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // rVertex20: pos(3f,0) + color(4u8n,12) + texcoord(2i16n,16) = 20 bytes
    // rVertexLit32: pos(3f,0) + normal(4i8n,12) + color(4u8n,16) + texcoord(2i16n,20) + reserved(8,24) = 32 bytes
    VkVertexInputAttributeDescription attrsUnlit[3] = {};
    attrsUnlit[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};   // position
    attrsUnlit[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 12};    // color (shader reads as aSlot1)
    attrsUnlit[2] = {2, 0, VK_FORMAT_R16G16_SNORM, 16};       // texcoord (shader reads as aSlot2.xy)

    // Lit pipeline: 3 attributes (color at byte 16 is not consumed; shader uses vec4(1.0) instead).
    // texcoord sits at byte 20 but is bound to location 2 — matching aTexCoord in the vertex shader.
    VkVertexInputAttributeDescription attrsLit[3] = {};
    attrsLit[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};     // position
    attrsLit[1] = {1, 0, VK_FORMAT_R8G8B8A8_SNORM, 12};      // normal (4x int8 SNORM)
    attrsLit[2] = {2, 0, VK_FORMAT_R16G16_SNORM, 20};         // texcoord (skips color bytes at 16)

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;

    // Per-instance binding and attributes (used when key.instanced is true).
    // rInstanceData: modelMatrix(mat4, 64 bytes) + color(vec4, 16 bytes) = 80 bytes.
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = 80; // sizeof(rInstanceData)
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrsInstance[5] = {};
    attrsInstance[0] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};   // modelCol0
    attrsInstance[1] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16};  // modelCol1
    attrsInstance[2] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32};  // modelCol2
    attrsInstance[3] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48};  // modelCol3
    attrsInstance[4] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64};  // instanceColor

    // Combined bindings/attributes for instanced pipeline
    VkVertexInputBindingDescription bindings2[2] = {binding, instanceBinding};
    VkVertexInputAttributeDescription attrsLitInstanced[8] = {};

    if (key.instanced)
    {
        binding.stride = 32;
        // Lit vertex attrs (3) + instance attrs (5)
        attrsLitInstanced[0] = attrsLit[0];
        attrsLitInstanced[1] = attrsLit[1];
        attrsLitInstanced[2] = attrsLit[2];
        for (int i = 0; i < 5; i++) attrsLitInstanced[3+i] = attrsInstance[i];
        bindings2[0] = binding;
        vertexInput.vertexBindingDescriptionCount = 2;
        vertexInput.pVertexBindingDescriptions = bindings2;
        vertexInput.vertexAttributeDescriptionCount = 8;
        vertexInput.pVertexAttributeDescriptions = attrsLitInstanced;
    }
    else if (key.litVertex)
    {
        binding.stride = 32; // sizeof(rVertexLit32)
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attrsLit;
    }
    else
    {
        binding.stride = 20; // sizeof(rVertex20)
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attrsUnlit;
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = key.useLines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic state: viewport, scissor, and depth bias (for sr_DepthOffset / polygon offset)
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    // Viewport (set dynamically, just need count=1)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // After the Y-flip in the vertex shader (gl_Position.y = -gl_Position.y), geometry
    // that is CCW in OpenGL screen space becomes CW in Vulkan screen space.
    // With VK_FRONT_FACE_CLOCKWISE the visible (GL-front) faces are CW = "front" in Vulkan,
    // so we must cull "back" (CCW) faces to match GL_CULL_FACE / GL_BACK behaviour.
    rasterizer.cullMode = key.cullFace ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace = key.frontFaceCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE; // controlled dynamically via vkCmdSetDepthBias

    // Multisampling (disabled)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = key.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = key.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = static_cast<VkColorComponentFlags>(key.colorWriteMask);

    switch (key.blendMode)
    {
    case 0: // Opaque
        blendAttachment.blendEnable = VK_FALSE;
        break;
    case 1: // Alpha
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case 2: // Additive
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case 3: // Multiply
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case 4: // PremultipliedAlpha
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    }

    // Reuse the count we already looked up at the top of this function
    // for shader variant selection. The post-process offscreen render pass
    // has 2 color attachments (scene color + scene emissive); the main
    // swapchain render pass has 1.
    uint32_t colorAttachmentCount = thisPassColorCount;

    // Duplicate the blend attachment state into N slots. All color
    // attachments share the same blend mode — cycles glowing into the
    // emissive buffer use the same alpha-blend / additive / opaque state
    // as their color output. Shaders that want a different blend per
    // attachment would need an independent blend extension.
    VkPipelineColorBlendAttachmentState blendAttachments[4]{}; // max 4 — raise if needed
    if (colorAttachmentCount > 4)
    {
        std::cerr << "[Vulkan] Render pass has " << colorAttachmentCount
                  << " color attachments, pipeline supports max 4\n";
        colorAttachmentCount = 4;
    }
    for (uint32_t i = 0; i < colorAttachmentCount; ++i)
        blendAttachments[i] = blendAttachment;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = colorAttachmentCount;
    colorBlend.pAttachments = blendAttachments;

    // Create pipeline
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
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create pipeline" << std::endl;
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

void rVulkanPipelineManager::SetRenderPass(VkRenderPass rp)
{
    // Just switch the active render pass — cached pipelines for other render passes
    // remain valid and will be reused when that render pass is active again.
    renderPass_ = rp;
}

void rVulkanPipelineManager::RegisterRenderPass(VkRenderPass rp, uint32_t colorAttachmentCount)
{
    renderPassColorCounts_[rp] = colorAttachmentCount;
}

void rVulkanPipelineManager::PrewarmCommonPipelines()
{
    // Enumerate the pipeline variants the game actually uses, create them all
    // up front, and let Vulkan's pipeline cache batch the JIT compilation work.
    // Skipping this step causes visible stutter on the first draw of each new
    // variant (first game frame after menus, first zone spawn, etc).
    //
    // The matrix below is intentionally a bit larger than strictly necessary
    // — creating an unused pipeline wastes a few hundred KB and a few ms, but
    // missing one means stutter. Adjust if profiling shows specific combos
    // that are never hit.

    const uint8_t blendModes[] = {
        0x0000, // Opaque
        0x0001, // Alpha
        0x0002, // Additive
    };

    rVulkanPipelineKey key{};
    key.colorWriteMask = 0xF;  // full RGBA write — stereo paths can warm their own masks if needed

    // --- Unlit triangles (floor, walls, zones, effects, HUD) ---
    key.litVertex = false;
    key.useLines = false;
    for (uint8_t blend : blendModes)
    {
        key.blendMode = blend;
        for (int dt = 0; dt < 2; ++dt)
        {
            key.depthTest = (dt != 0);
            for (int dw = 0; dw < 2; ++dw)
            {
                key.depthWrite = (dw != 0);
                // No culling — frontFace is irrelevant, warm once
                key.cullFace = false;
                key.frontFaceCW = true;
                GetPipeline(key);
                // With culling — warm both winding orders
                key.cullFace = true;
                key.frontFaceCW = true;
                GetPipeline(key);
                key.frontFaceCW = false;
                GetPipeline(key);
            }
        }
    }

    // --- Unlit lines (grid, outlines, debug) ---
    key.litVertex = false;
    key.useLines = true;
    key.cullFace = false;  // lines are never culled
    for (uint8_t blend : blendModes)
    {
        key.blendMode = blend;
        for (int dt = 0; dt < 2; ++dt)
        {
            key.depthTest = (dt != 0);
            for (int dw = 0; dw < 2; ++dw)
            {
                key.depthWrite = (dw != 0);
                key.frontFaceCW = true;
                GetPipeline(key);
            }
        }
    }

    // --- Lit triangles (cycle bodies) ---
    key.litVertex = true;
    key.useLines = false;
    key.blendMode = 0x0000; // Opaque — cycles don't blend
    for (int dt = 0; dt < 2; ++dt)
    {
        key.depthTest = (dt != 0);
        for (int dw = 0; dw < 2; ++dw)
        {
            key.depthWrite = (dw != 0);
            // No culling — frontFace irrelevant
            key.cullFace = false;
            key.frontFaceCW = true;
            GetPipeline(key);
            // With culling — both winding orders
            key.cullFace = true;
            key.frontFaceCW = true;
            GetPipeline(key);
            key.frontFaceCW = false;
            GetPipeline(key);
        }
    }
}

void rVulkanPipelineManager::InvalidateRenderPass(VkRenderPass rp)
{
    // Remove only pipelines associated with a destroyed render pass.
    for (auto it = cache_.begin(); it != cache_.end(); )
    {
        if (it->first.renderPass == rp)
        {
            vkDestroyPipeline(device_, it->second, nullptr);
            it = cache_.erase(it);
        }
        else
            ++it;
    }
    renderPassColorCounts_.erase(rp);
}

void rVulkanPipelineManager::Destroy()
{
    for (auto& [key, pipeline] : cache_)
        vkDestroyPipeline(device_, pipeline, nullptr);
    cache_.clear();

    if (layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

    if (pipelineCache_ != VK_NULL_HANDLE)
    {
        // Persist the cache blob to disk so next launch can reuse it
        std::string cachePath = GetPipelineCachePath();
        if (!cachePath.empty())
        {
            size_t dataSize = 0;
            vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, nullptr);
            if (dataSize > 0)
            {
                std::vector<uint8_t> data(dataSize);
                vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, data.data());
                // Write to temp file then rename for atomicity (crash-safe)
                std::string tmpPath = cachePath + ".tmp";
                std::ofstream f(tmpPath, std::ios::binary);
                if (f.is_open())
                {
                    f.write(reinterpret_cast<const char*>(data.data()),
                            static_cast<std::streamsize>(dataSize));
                    f.close();
                    std::rename(tmpPath.c_str(), cachePath.c_str());
                }
            }
        }
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
    }
}

#endif // DEDICATED
