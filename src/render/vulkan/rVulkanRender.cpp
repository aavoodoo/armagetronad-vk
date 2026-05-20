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
#include "rVulkanRender.h"
#include "rVulkanHelpers.h"
#include "rScreen.h"
#include "rRenderBucket.h"
#include "rRenderQueue.h"
#include "rVertex.h"
#include "tDirectories.h"
#include "tString.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "tSysTime.h"
#include "tConfiguration.h"
#include "rRenderStats.h"
#include "tLuaState.h"
#include <cstring>
#include <iostream>
#include <format>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Enforce that the descriptor manager's deferred-free slot count matches the renderer's
// frame-in-flight count. If MAX_FRAMES_IN_FLIGHT changes, PP_MAX_FRAMES must also change.
static_assert(rVulkanDescriptorManager::PP_MAX_FRAMES == MAX_FRAMES_IN_FLIGHT,
    "PP_MAX_FRAMES in rVulkanDescriptor.h must equal MAX_FRAMES_IN_FLIGHT in rVulkanRender.h");

// Arena bounds for floor shader (set via sr_vkSetArenaBounds before floor rendering)
static float s_arenaBoundsLow[2]  = {-100.0f, -100.0f};
static float s_arenaBoundsHigh[2] = { 100.0f,  100.0f};
// Camera world position (set from eCamera::Render for parallax shader hooks)
static float s_cameraWorldPos[3] = {0, 0, 5};

// Full render context enum value (set via sr_vkSetRenderContext).
// Matches rRenderContext in rRendererState.h. Shader code reads this from
// push constants to dispatch per-component hook functions.
int s_renderContextId = 0;

// Model mesh cache version — incremented on cache clear so cycle renderers re-prime.
int sr_modelCacheVersion = 0;

// Informational logging — guarded to reduce stderr spam in production.
// Error logging (compile failures, VK errors) is always on.
#ifdef NDEBUG
#define VK_LOG_INFO(x) do {} while(0)
#else
#define VK_LOG_INFO(x) std::cerr << x
#endif

// Global renderer pointer — defined at the bottom of this file near sr_vkRendererInit().
// Forward-declared here so the destructor and sr_vkSet* helpers can reference it.
static vkRenderer* s_vkRenderer = nullptr;

// ============================================================================
// Wall GPU compute segment accumulator (Sprint 3.1)
// ============================================================================
// Segments collected during the current frame's render callback. Swapped with
// sg_wallSegsPrev_ at BeginFrame, so the previous frame's segments are
// dispatched to the compute shader while this frame's walls are collected.
static std::vector<WallSegmentGPU> sg_wallSegsCurrent_;
static std::vector<WallSegmentGPU> sg_wallSegsPrev_;
// Persistent UV bounds (never shrink). Expands as new segments are added.
// Reused across frames so the texture matrix doesn't pop on boundary changes.
static float sg_wallUMin_ =  1e30f;
static float sg_wallUMax_ = -1e30f;
// Multi-viewport guard: incremented at each BeginViewportFBO call.
// sr_AddWallComputeSegment only accumulates when this is 0 (first viewport).
static int sg_wallAccumViewportN_ = 0;

// Tracks whether the CURRENT frame has drawn any in-game 3D content.
// Flipped to true whenever sr_vkSetRenderContext sees a Game3D_* context ID
// (3..10 per rRenderContext enum), reset at EndFrame.
// `s_lastFrameWasInGame` holds the previous frame's value and is what
// BeginFrame reads to decide whether to redirect to the post-process
// offscreen target. This one-frame latency is imperceptible.
static bool s_currentFrameIsInGame = false;
static bool s_lastFrameWasInGame = false;

// Vulkan validation layers (useful for troubleshooting without a debug build)
static bool sr_vulkanValidation = false;
static tConfItem<bool> sr_vulkanValidationCI("VULKAN_ENABLE_VALIDATION", sr_vulkanValidation);

// Post-processing activation state.
// No config items — driven exclusively by:
//   • sr_vkPostProcessActivate / sr_vkPostProcessDeactivate (C++ API, called by moviepack)
//   • aa_pp_enable / aa_pp_disable (Lua API, exposed via gLuaBindings)
//   • macOS BeginFrame block (force-enables passthrough for depth preservation)
// All callers run on the render thread, so no mutex is needed.
static bool   s_pendingPPEnabled = false;
static std::string s_pendingPPEffect;
static bool   s_pendingPPDirty   = true;  // apply on first BeginFrame

// ============================================================================
// Constructor / Destructor
// ============================================================================

vkRenderer::vkRenderer()
    : currentStack_(&modelviewStack_)
    , textureEnabled_(false)
    , lightingEnabled_(false)
    , depthTestEnabled_(true)
    , depthWriteEnabled_(true)
    , blendEnabled_(false)
    , cullFaceEnabled_(false)
    , frontFaceCW_(false) // default matches game's normal-play GL_CCW front face
    , blendSrc_(1) // ONE
    , blendDst_(0) // ZERO
    , boundTexture2D_(0)
    , commandPool_(VK_NULL_HANDLE)
    , currentFrame_(0)
    , currentImageIndex_(0)
    , acquireSemaphoreIndex_(0)
    , frameStarted_(false)
    , pendingShaderReload_(false)
    , nextTextureId_(1)
    , vertShader_(VK_NULL_HANDLE)
    , vertShaderInstanced_(VK_NULL_HANDLE)
    , fragShader_(VK_NULL_HANDLE)
    , fragShaderEmissive_(VK_NULL_HANDLE)
{
    currentColor_[0] = currentColor_[1] = currentColor_[2] = currentColor_[3] = 1.0f;
    currentTexCoord_[0] = currentTexCoord_[1] = 0.0f;
    clearColor_[0] = clearColor_[1] = clearColor_[2] = 0.0f;
    clearColor_[3] = 1.0f;
    cachedViewport_[0] = cachedViewport_[1] = 0;
    cachedViewport_[2] = cachedViewport_[3] = 0;

    memset(lights_, 0, sizeof(lights_));
    float white[4] = {1,1,1,1};
    memcpy(materialDiffuse_, white, 16);
    memcpy(materialSpecular_, white, 16);

    memset(commandBuffers_, 0, sizeof(commandBuffers_));
    memset(inFlightFences_, 0, sizeof(inFlightFences_));
}

vkRenderer::~vkRenderer()
{
    // Clear global alias so sr_vk* helpers don't dereference a dangling pointer
    // after sr_RendererCleanup() (which deletes this object via `delete renderer`).
    s_vkRenderer = nullptr;

    if (context_.IsValid())
    {
        vkDeviceWaitIdle(context_.GetDevice());

        // Note: command pool destroyed AFTER all resources to avoid driver memory
        // leaks in Apple's kosmickrisp Vulkan driver. Destroying the pool before
        // resources causes the driver to leak internal CmdBindDescriptorSets state.
        // The validation layer may warn about "buffer in use by command buffer" but
        // these are harmless after vkDeviceWaitIdle.

        // Destroy user-created fences that were never deleted
        for (auto* ptr : userFences_)
        {
            if (ptr)
            {
                vkDestroyFence(context_.GetDevice(), *ptr, nullptr);
                delete ptr;
            }
        }
        userFences_.clear();

        // Destroy sync objects
        for (auto sem : imageAvailableSemaphores_)
            if (sem) vkDestroySemaphore(context_.GetDevice(), sem, nullptr);
        for (auto sem : renderFinishedSemaphores_)
            if (sem) vkDestroySemaphore(context_.GetDevice(), sem, nullptr);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            if (inFlightFences_[i])
                vkDestroyFence(context_.GetDevice(), inFlightFences_[i], nullptr);


        VkDevice device = context_.GetDevice();

        // Destroy ALL descriptor pools first — this frees all descriptor sets so
        // that subsequent sampler/image-view destruction doesn't trigger validation
        // warnings about resources still being referenced by live descriptor sets.
        descriptorManager_.Destroy();
        postProcess_.DestroyDescriptorPools();
        VK_DESTROY(vkDestroyDescriptorPool, device, lightingUBOPool_);
        VK_DESTROY(vkDestroyDescriptorSetLayout, device, lightingUBOLayout_);

        // Destroy shadow maps FIRST (erases textures_ entries + destroys handles)
        DestroyShadowMaps();
        // Destroy shadow staging buffers
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            rVulkanBufferManager::DestroyBuffer(context_.GetAllocator(), shadowStagingBuf_[i]);

        // Erase viewport FBO texture entries from textures_ map so the texture
        // loop below doesn't double-destroy resources owned by ViewportFBO structs.
        for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
            for (int i = 0; i < MAX_VIEWPORT_FBOS; i++) {
                ViewportFBO& vfbo = viewportFBOs_[f][i];
                if (vfbo.colorTexId) textures_.erase(vfbo.colorTexId);
                if (vfbo.depthTexId) textures_.erase(vfbo.depthTexId);
            }

        // Descriptor sets are gone; safe to destroy textures.
        // Samplers are owned by samplerCache_ (destroyed separately below) — do NOT destroy here.
        for (auto& [id, tex] : textures_)
        {
            VK_DESTROY(vkDestroyImageView, device, tex.view);
            if (tex.image != VK_NULL_HANDLE)
            {
                vmaDestroyImage(context_.GetAllocator(), tex.image, tex.imageAllocation);
                tex.image           = VK_NULL_HANDLE;
                tex.imageAllocation = VK_NULL_HANDLE;
            }
        }

        // Flush deferred texture deletions (samplers owned by cache — not destroyed here)
        VmaAllocator vmaAlloc = context_.GetAllocator();
        auto destroyTexInfo = [vmaAlloc](VkDevice dev, VkTextureInfo& tex) {
            VK_DESTROY(vkDestroyImageView, dev, tex.view);
            if (tex.image != VK_NULL_HANDLE)
            {
                vmaDestroyImage(vmaAlloc, tex.image, tex.imageAllocation);
                tex.image           = VK_NULL_HANDLE;
                tex.imageAllocation = VK_NULL_HANDLE;
            }
        };
        pendingDeleteTextures_.drainAllWith(device, destroyTexInfo);

        // Destroy dummy texture
        VK_DESTROY(vkDestroySampler, device, dummyTexture_.sampler);
        VK_DESTROY(vkDestroyImageView, device, dummyTexture_.view);
        if (dummyTexture_.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(context_.GetAllocator(), dummyTexture_.image, dummyTexture_.imageAllocation);
            dummyTexture_.image           = VK_NULL_HANDLE;
            dummyTexture_.imageAllocation = VK_NULL_HANDLE;
        }

        // Destroy all cached samplers
        for (auto& [key, sampler] : samplerCache_)
            if (sampler != VK_NULL_HANDLE)
                vkDestroySampler(device, sampler, nullptr);
        samplerCache_.clear();

        // Destroy per-frame lighting UBO buffers (layouts/pools already destroyed above)
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            lightingUBOMapped_[i] = nullptr;  // VMA manages unmap via vmaDestroyBuffer
            if (lightingUBOBuffer_[i])
            {
                vmaDestroyBuffer(context_.GetAllocator(), lightingUBOBuffer_[i], lightingUBOAlloc_[i]);
                lightingUBOBuffer_[i] = VK_NULL_HANDLE;
                lightingUBOAlloc_[i]  = VK_NULL_HANDLE;
            }
        }

        DestroyViewportFBOs();
        vulkanQueue_.Destroy();
        stagingPool_.Destroy();
        pendingTexUploads_.clear();

        rVulkanShader::Destroy(device, vertShader_);
        rVulkanShader::Destroy(device, vertShaderInstanced_);
        rVulkanShader::Destroy(device, fragShader_);
        rVulkanShader::Destroy(device, fragShaderEmissive_);

        DestroyWallComputePipeline();
        postProcess_.Destroy();
        pipelineManager_.Destroy();

        // Command pool destroyed LAST — after all resources that command buffers
        // referenced. Destroying it earlier causes Apple's kosmickrisp driver to
        // leak internal CmdBindDescriptorSets allocations.
        if (commandPool_)
            vkDestroyCommandPool(device, commandPool_, nullptr);

        framebuffer_.Destroy(context_);
        swapchain_.Destroy(device);
        context_.Shutdown();

        // Shut down the Lua scripting layer last — after all Vulkan resources are
        // gone, so any Lua-owned C++ objects that hold Vulkan handles are already
        // destroyed by this point.
        tLuaState::Instance().Shutdown();
    }
}

bool vkRenderer::Init(SDL_Window* window)
{
    window_ = window;
    bool validation = sr_vulkanValidation; // Enable via VULKAN_ENABLE_VALIDATION 1

    if (!context_.Init(window, validation))
        return false;

    int w = 800, h = 600;
    SDL_GetWindowSizeInPixels(window, &w, &h);

    if (!swapchain_.Create(context_, w, h))
        return false;

    if (!framebuffer_.Create(context_, swapchain_))
        return false;

    if (!descriptorManager_.Init(context_.GetDevice()))
        return false;

    // Load uber shaders. On desktop (HAVE_SHADERC), compile from GLSL at init
    // time with moviepack override support. On Android (no shaderc), load
    // pre-compiled SPIR-V from APK assets via SDL_IOFromFile.
#ifdef HAVE_SHADERC_SHADERC_HPP
    {
        VkDevice device = context_.GetDevice();

        // Build the include search path by resolving a known sentinel file
        // in each candidate directory. GetReadPath returns empty strings
        // for directories, so locating a real known file inside is the
        // reliable way to get a directory path. See ReloadShaders for the
        // matching logic.
        std::vector<std::string> includePaths;

        tString mvHooks = tDirectories::Data().GetReadPath("moviepack/shaders/uber_hooks.glsl");
        if (mvHooks.Len() > 1)
        {
            std::string p = static_cast<const char*>(mvHooks);
            auto slash = p.find_last_of('/');
            if (slash != std::string::npos) includePaths.push_back(p.substr(0, slash));
        }

        tString sysHooks = tDirectories::Data().GetReadPath("shaders/uber_hooks.glsl");
        if (sysHooks.Len() > 1)
        {
            std::string p = static_cast<const char*>(sysHooks);
            auto slash = p.find_last_of('/');
            if (slash != std::string::npos) includePaths.push_back(p.substr(0, slash));
        }

        // Vertex shader: moviepack/shaders/uber.vert → shaders/uber.vert
        tString vertSrcPath = tDirectories::Data().GetReadPath("moviepack/shaders/uber.vert");
        if (vertSrcPath.Len() <= 1)
            vertSrcPath = tDirectories::Data().GetReadPath("shaders/uber.vert");
        if (vertSrcPath.Len() > 1)
        {
            VK_LOG_INFO("[Vulkan] Compiling vert: " << static_cast<const char*>(vertSrcPath) << std::endl);
            std::string vertErr;
            vertShader_ = rVulkanShader::CompileFromFile(
                device, static_cast<const char*>(vertSrcPath),
                rVulkanShader::Stage::Vertex, includePaths, &vertErr);
            if (vertShader_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] Vertex compile failed:\n" << vertErr << "\n";
        }

        // Instanced vertex shader for cycle rendering
        tString instVertPath = tDirectories::Data().GetReadPath("shaders/uber_instanced.vert");
        if (instVertPath.Len() > 1)
        {
            std::cerr << "[Vulkan] Compiling instanced vert: " << static_cast<const char*>(instVertPath) << std::endl;
            std::string instErr;
            vertShaderInstanced_ = rVulkanShader::CompileFromFile(
                device, static_cast<const char*>(instVertPath),
                rVulkanShader::Stage::Vertex, includePaths, &instErr);
            if (vertShaderInstanced_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] Instanced vert compile failed:\n" << instErr << "\n";
        }
        else
        {
            std::cerr << "[Vulkan] WARNING: Instanced shader not found at shaders/uber_instanced.vert — instancing disabled\n";
        }

        // Fragment shader: moviepack/shaders/uber.frag → shaders/uber.frag
        // We compile TWO variants from the same source:
        //   - fragShader_         : no defines, declares only location=0
        //   - fragShaderEmissive_ : with USE_EMISSIVE_OUT defined, also
        //                           declares location=1 for the post-process
        //                           scene color + emissive render pass.
        // rVulkanPipelineManager picks the right one per render pass.
        tString fragSrcPath = tDirectories::Data().GetReadPath("moviepack/shaders/uber.frag");
        if (fragSrcPath.Len() <= 1)
            fragSrcPath = tDirectories::Data().GetReadPath("shaders/uber.frag");
        if (fragSrcPath.Len() > 1)
        {
            const std::string fragPathStr = static_cast<const char*>(fragSrcPath);
            VK_LOG_INFO("[Vulkan] Compiling frag (plain): " << fragPathStr << std::endl);
            std::string fragErr;
            auto spvPlain = rVulkanShader::CompileGLSLFromFile(
                fragPathStr, rVulkanShader::Stage::Fragment, includePaths, {}, &fragErr);
            if (!spvPlain.empty())
                fragShader_ = rVulkanShader::LoadFromMemory(device, spvPlain);
            if (fragShader_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] Fragment compile failed (plain):\n" << fragErr << "\n";

            VK_LOG_INFO("[Vulkan] Compiling frag (emissive): " << fragPathStr << std::endl);
            std::string fragErrEm;
            auto spvEmissive = rVulkanShader::CompileGLSLFromFile(
                fragPathStr, rVulkanShader::Stage::Fragment, includePaths,
                {{"USE_EMISSIVE_OUT", "1"}}, &fragErrEm);
            if (!spvEmissive.empty())
                fragShaderEmissive_ = rVulkanShader::LoadFromMemory(device, spvEmissive);
            if (fragShaderEmissive_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] Fragment compile failed (emissive):\n" << fragErrEm << "\n";
        }
    }
#else
    // No shaderc (Android): load pre-compiled SPIR-V from APK assets.
    // SDL_IOFromFile reads from APK assets when given a relative path.
    {
        VkDevice device = context_.GetDevice();
        vertShader_         = rVulkanShader::LoadFromFile(device, "shaders/uber.vert.spv");
        vertShaderInstanced_ = rVulkanShader::LoadFromFile(device, "shaders/uber_instanced.vert.spv");
        fragShader_         = rVulkanShader::LoadFromFile(device, "shaders/uber.frag.spv");
        fragShaderEmissive_ = rVulkanShader::LoadFromFile(device, "shaders/uber.frag.emissive.spv");
        if (vertShader_ == VK_NULL_HANDLE)
            SDL_Log("[Vulkan] Failed to load shaders/uber.vert.spv");
        if (vertShaderInstanced_ == VK_NULL_HANDLE)
            SDL_Log("[Vulkan] Failed to load shaders/uber_instanced.vert.spv (instancing disabled)");
        if (fragShader_ == VK_NULL_HANDLE)
            SDL_Log("[Vulkan] Failed to load shaders/uber.frag.spv");
        if (fragShaderEmissive_ == VK_NULL_HANDLE)
            SDL_Log("[Vulkan] Failed to load shaders/uber.frag.emissive.spv");
    }
#endif
    if (!vertShader_ || !fragShader_ || !fragShaderEmissive_)
    {
        std::cerr << "[Vulkan] Failed to load shaders" << std::endl;
        return false;
    }

    // Validate push constant capacity (we use 192 bytes: MVP + texMatrix + normalMatrix)
    if (context_.GetDeviceProperties().limits.maxPushConstantsSize < 192)
    {
        std::cerr << std::format("[Vulkan] GPU maxPushConstantsSize={} < 192 required\n",
                  context_.GetDeviceProperties().limits.maxPushConstantsSize);
        return false;
    }

    // Create the set 1 descriptor layout first — needed by the pipeline layout.
    // Binding 0: lighting UBO, bindings 1-2: shadow map samplers (sampler2DShadow)
    {
        VkDescriptorSetLayoutBinding bindings[3]{};
        // Binding 0: UBO
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        // Binding 1: shadow map 0 (sampler2DShadow)
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        // Binding 2: shadow map 1 (sampler2DShadow)
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo uboLayoutInfo{};
        uboLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        uboLayoutInfo.bindingCount = 3;
        uboLayoutInfo.pBindings    = bindings;
        if (vkCreateDescriptorSetLayout(context_.GetDevice(), &uboLayoutInfo, nullptr, &lightingUBOLayout_) != VK_SUCCESS)
            return false;
    }

    VkDescriptorSetLayout pipelineSetLayouts[2] = { descriptorManager_.GetLayout(), lightingUBOLayout_ };
    VkPipelineLayout layout;
    if (!pipelineManager_.Init(context_.GetDevice(), framebuffer_.GetRenderPass(),
                               vertShader_, fragShader_, fragShaderEmissive_,
                               pipelineSetLayouts, 2, &layout,
                               &context_.GetDeviceProperties()))
        return false;
    pipelineManager_.SetInstancedVertShader(vertShaderInstanced_);

    // Pre-warm the pipeline cache with all variants the game is known to use.
    // Eliminates per-frame JIT compilation stutter on the first draw of each
    // new state combination. Subsequent runs hit the persisted disk cache so
    // this becomes near-instant.
    pipelineManager_.PrewarmCommonPipelines();

    // Wire the pipeline manager into the post-process subsystem so it can
    // register its offscreen render pass's 2-color-attachment layout with
    // the scene pipeline cache.
    postProcess_.SetPipelineManager(&pipelineManager_);

    // Initialize the post-processing subsystem. This creates the stable
    // pipeline resources (descriptor layouts, pipeline layout, passthrough
    // shader/pipeline) but does NOT allocate the offscreen render target —
    // that happens lazily on first SetEnabled(true). Stash the swapchain
    // extent so Enable knows what size to build at.
    if (!postProcess_.Init(context_, swapchain_.GetFormat(),
                           framebuffer_.GetDepthFormat(),
                           framebuffer_.GetRenderPass()))
    {
        // Non-fatal: post-processing is optional. Log and continue.
        std::cerr << "[Vulkan] Post-process init failed; feature will be unavailable\n";
    }
    postProcess_.OnSwapchainResized(context_, swapchain_.GetExtent().width,
                                    swapchain_.GetExtent().height,
                                    framebuffer_.GetRenderPass());

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.GetGraphicsFamily();
    if (vkCreateCommandPool(context_.GetDevice(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        return false;

    // Allocate command buffers
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    if (vkAllocateCommandBuffers(context_.GetDevice(), &allocInfo, commandBuffers_) != VK_SUCCESS)
        return false;

    // Create sync objects
    // Semaphores are per-swapchain-image to avoid reuse conflicts between
    // presentation and submission (swapchain may have more images than MAX_FRAMES_IN_FLIGHT).
    uint32_t imageCount = swapchain_.GetImageCount();
    imageAvailableSemaphores_.resize(imageCount);
    renderFinishedSemaphores_.resize(imageCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(context_.GetDevice(), &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_.GetDevice(), &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS)
            return false;
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateFence(context_.GetDevice(), &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            return false;
    }

    // Init Vulkan render queue
    if (!vulkanQueue_.Init(context_, commandPool_))
        return false;

    // Init staging pool (lazy allocation — no GPU memory until first Acquire)
    stagingPool_.Init(context_);

    // Create dummy 1x1 white texture for non-textured draws
    if (!CreateDummyTexture())
        return false;

    // Create per-frame lighting UBO buffers (one per frame-in-flight slot).
    // Each frame writes to its own buffer, so GPU reads from frame N never alias
    // CPU writes for frame N+1 (the classic single-buffer UBO blinking bug).
    // lightingUBOLayout_ was already created above (needed by pipelineManager_.Init).
    {
        // Dedicated pool for the lighting descriptor sets (one per frame slot)
        // Includes UBO + 2 shadow map sampler bindings per set
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; // 2 shadow maps per frame
        VkDescriptorPoolCreateInfo uboPoolInfo{};
        uboPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        uboPoolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;
        uboPoolInfo.poolSizeCount = 2;
        uboPoolInfo.pPoolSizes    = poolSizes;
        if (vkCreateDescriptorPool(context_.GetDevice(), &uboPoolInfo, nullptr, &lightingUBOPool_) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayout layouts_arr[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts_arr[i] = lightingUBOLayout_;
        VkDescriptorSetAllocateInfo uboAllocInfo{};
        uboAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        uboAllocInfo.descriptorPool     = lightingUBOPool_;
        uboAllocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        uboAllocInfo.pSetLayouts        = layouts_arr;
        if (vkAllocateDescriptorSets(context_.GetDevice(), &uboAllocInfo, lightingDescSet_) != VK_SUCCESS)
            return false;

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = sizeof(LightingUBO);
        bufInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        LightingUBO initUBO{};  // all-zero (lighting disabled) initial state

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VmaAllocationCreateInfo vmaAllocCI{};
            vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
            vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                             | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo vmaAllocInfo{};
            if (vmaCreateBuffer(context_.GetAllocator(), &bufInfo, &vmaAllocCI,
                                &lightingUBOBuffer_[i], &lightingUBOAlloc_[i], &vmaAllocInfo) != VK_SUCCESS)
                return false;
            lightingUBOMapped_[i] = vmaAllocInfo.pMappedData;
            if (!lightingUBOMapped_[i])
            {
                std::cerr << std::format("[Vulkan] VMA persistent map failed for lightingUBO[{}]\n", i);
                return false;
            }
            memcpy(lightingUBOMapped_[i], &initUBO, sizeof(initUBO));

            // Point this frame's descriptor set to its own buffer + dummy shadow maps
            VkDescriptorBufferInfo bufDesc{};
            bufDesc.buffer = lightingUBOBuffer_[i];
            bufDesc.offset = 0;
            bufDesc.range  = sizeof(LightingUBO);
            // Use dummy texture for shadow map bindings until real shadow maps are created
            VkDescriptorImageInfo shadowImgInfo{};
            shadowImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadowImgInfo.imageView   = dummyTexture_.view;
            shadowImgInfo.sampler     = dummyTexture_.sampler;

            VkWriteDescriptorSet writes[3]{};
            // UBO binding 0
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = lightingDescSet_[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo     = &bufDesc;
            // Shadow map binding 1 (dummy)
            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = lightingDescSet_[i];
            writes[1].dstBinding      = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo      = &shadowImgInfo;
            // Shadow map binding 2 (dummy)
            writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet          = lightingDescSet_[i];
            writes[2].dstBinding      = 2;
            writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo      = &shadowImgInfo;
            vkUpdateDescriptorSets(context_.GetDevice(), 3, writes, 0, nullptr);
        }

        // Dummy descriptor set (set 0 = texture only — UBO is now separate in set 1)
        dummyDescriptorSet_ = descriptorManager_.GetOrCreateTextureSet(
            dummyTexture_.view, dummyTexture_.sampler);
    }

    // Initialize wall GPU compute pipeline (non-fatal — falls back to CPU if shader missing)
    if (!CreateWallComputePipeline())
    {
        std::cerr << "[Vulkan] Wall compute pipeline init failed; CPU wall path active\n";
        // Non-fatal: wallComputeReady_ stays false, CPU path used
    }

    // Initialize Lua scripting layer (Season 4 — post-process effect DSL).
    tLuaState::Instance();  // creates the state; sandbox applied in constructor
    VK_LOG_INFO("[Lua] Scripting layer initialized (LuaJIT)" << std::endl);

    VK_LOG_INFO("[Vulkan] Renderer initialized: " << context_.GetDeviceName() << std::endl);
    return true;
}

bool vkRenderer::RecreateSwapchain(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    vkDeviceWaitIdle(context_.GetDevice());
    vulkanQueue_.CompactIfNeeded(context_.GetDevice());

    // GPU is idle — free ALL descriptor sets immediately so that the image
    // views they reference can be safely destroyed below.  Without this,
    // cached and deferred descriptor sets still formally "reference" their
    // views, and vkDestroyImageView triggers VUID-01026.
    descriptorManager_.FlushAllDeferred();
    // The dummy descriptor set is allocated from the same pools and is now
    // freed.  Rebuild it immediately so draws that fall back to "no texture"
    // never bind a stale handle.
    dummyDescriptorSet_ = descriptorManager_.GetOrCreateTextureSet(
        dummyTexture_.view, dummyTexture_.sampler);

    // Destroy viewport FBOs — they're sized to viewport dimensions which change
    DestroyViewportFBOs();

    // Save old render pass handle before destroying — we need to invalidate
    // only those cached pipelines, not the entire cache.
    VkRenderPass oldRenderPass = framebuffer_.GetRenderPass();

    // Clear stale render pass copies in the post-process BEFORE destroying
    // the framebuffer — the PP's composite passes and swapchainRenderPass_
    // may hold the same handle that framebuffer_.Destroy() is about to free.
    postProcess_.ClearRenderPassRefs(oldRenderPass);

    framebuffer_.Destroy(context_);

    if (!swapchain_.Recreate(context_, width, height))
    {
        std::cerr << "[Vulkan] RecreateSwapchain: swapchain creation failed" << std::endl;
        return false;
    }

    if (!framebuffer_.Create(context_, swapchain_))
    {
        std::cerr << "[Vulkan] RecreateSwapchain: framebuffer creation failed" << std::endl;
        return false;
    }

    // Invalidate pipelines that referenced the old (now destroyed) render pass,
    // then switch to the new one. Also invalidate post-process and viewport FBO
    // pipelines since those render passes are rebuilt during resize.
    pipelineManager_.InvalidateRenderPass(oldRenderPass);
    if (postProcess_.GetSceneRenderPass() != VK_NULL_HANDLE)
        pipelineManager_.InvalidateRenderPass(postProcess_.GetSceneRenderPass());
    if (viewportRenderPass_ != VK_NULL_HANDLE)
        pipelineManager_.InvalidateRenderPass(viewportRenderPass_);
    pipelineManager_.SetRenderPass(framebuffer_.GetRenderPass());

    // Rebuild the post-process offscreen target at the new swapchain extent.
    // If PP is disabled this is a cheap no-op (it just records the new size).
    // Pass the old (destroyed) render pass so PP can skip destroying stale copies.
    postProcess_.OnSwapchainResized(context_, width, height,
                                    framebuffer_.GetRenderPass(), oldRenderPass);
    // Re-activate the effect — OnSwapchainResized cleared all effects and
    // set activeEffectPtr_ to nullptr. Without this, the composite pass
    // doesn't run and swapchain images stay UNDEFINED (magenta screen).
    if (postProcess_.IsEnabled() && !s_pendingPPEffect.empty())
        postProcess_.SetActiveEffect(s_pendingPPEffect.c_str());

    // Resize semaphores if swapchain image count changed
    uint32_t newImageCount = swapchain_.GetImageCount();
    if (newImageCount != static_cast<uint32_t>(imageAvailableSemaphores_.size()))
    {
        VkDevice device = context_.GetDevice();
        for (auto sem : imageAvailableSemaphores_)
            if (sem) vkDestroySemaphore(device, sem, nullptr);
        for (auto sem : renderFinishedSemaphores_)
            if (sem) vkDestroySemaphore(device, sem, nullptr);

        imageAvailableSemaphores_.resize(newImageCount);
        renderFinishedSemaphores_.resize(newImageCount);
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < newImageCount; i++)
        {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS)
                return false;
        }
        acquireSemaphoreIndex_ = 0;
    }

    // Keep sr_screenWidth/Height in sync with the new swapchain extent so cockpit
    // widget layout (computed from sr_screenHeight) matches the Y-flip in Viewport().
    sr_screenWidth  = static_cast<int>(swapchain_.GetExtent().width);
    sr_screenHeight = static_cast<int>(swapchain_.GetExtent().height);

    needsSwapchainRecreation_ = false;

    // Notify the game that the screen dimensions changed so cockpit widgets
    // and other layout-dependent code can readjust.
    rCallbackAfterScreenModeChange::Exec();

    return true;
}

VkSampler vkRenderer::GetOrCreateSampler(VkFilter minFilter, VkFilter magFilter,
                                          VkSamplerAddressMode wrapS, VkSamplerAddressMode wrapT,
                                          float maxLod)
{
    SamplerKey key{minFilter, magFilter, wrapS, wrapT, maxLod};
    auto it = samplerCache_.find(key);
    if (it != samplerCache_.end())
        return it->second;

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = magFilter;
    si.minFilter    = minFilter;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = wrapS;
    si.addressModeV = wrapT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod       = maxLod;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(context_.GetDevice(), &si, nullptr, &sampler) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] GetOrCreateSampler: vkCreateSampler failed\n";
        return VK_NULL_HANDLE;
    }
    samplerCache_[key] = sampler;
    return sampler;
}

void vkRenderer::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                        VkImageLayout oldLayout, VkImageLayout newLayout,
                                        VkImageAspectFlags aspectMask,
                                        uint32_t mipLevels,
                                        VkPipelineStageFlags srcStage,
                                        VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {aspectMask, 0, mipLevels, 0, 1};

    // Derive access masks from layouts
    switch (oldLayout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        barrier.srcAccessMask = 0;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        break;
    }

    switch (newLayout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        barrier.dstAccessMask = 0;
        break;
    default:
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        break;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

bool vkRenderer::CreateDummyTexture()
{
    VkDevice device = context_.GetDevice();
    dummyTexture_ = VkTextureInfo{};

    uint8_t whitePixel[4] = {255, 255, 255, 255};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo vmaAllocCI{};
    vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(context_.GetAllocator(), &imageInfo, &vmaAllocCI,
                       &dummyTexture_.image, &dummyTexture_.imageAllocation, nullptr) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] VMA: failed to create dummy texture image\n";
        return false;
    }

    // Upload via staging buffer
    rVulkanBuffer staging;
    if (!rVulkanBufferManager::CreateStagingBuffer(context_, whitePixel, 4, staging))
        return false;

    VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

    TransitionImageLayout(cmd, dummyTexture_.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 1,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, dummyTexture_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, dummyTexture_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 1,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
    rVulkanBufferManager::DestroyBuffer(context_.GetAllocator(), staging);
    dummyTexture_.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = dummyTexture_.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &dummyTexture_.view) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create dummy texture image view" << std::endl;
        vmaDestroyImage(context_.GetAllocator(), dummyTexture_.image, dummyTexture_.imageAllocation);
        dummyTexture_.image           = VK_NULL_HANDLE;
        dummyTexture_.imageAllocation = VK_NULL_HANDLE;
        return false;
    }

    // Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &dummyTexture_.sampler) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create dummy texture sampler" << std::endl;
        VK_DESTROY(vkDestroyImageView, device, dummyTexture_.view);
        vmaDestroyImage(context_.GetAllocator(), dummyTexture_.image, dummyTexture_.imageAllocation);
        dummyTexture_.image           = VK_NULL_HANDLE;
        dummyTexture_.imageAllocation = VK_NULL_HANDLE;
        return false;
    }

    // Create descriptor set for dummy texture
    dummyDescriptorSet_ = descriptorManager_.GetOrCreateTextureSet(dummyTexture_.view, dummyTexture_.sampler);

    dummyTexture_.width = 1;
    dummyTexture_.height = 1;
    return dummyDescriptorSet_ != VK_NULL_HANDLE;
}

// ============================================================================
// Frame rendering loop
// ============================================================================

void vkRenderer::BeginFrame()
{
    if (frameStarted_) return;
    if (deviceLost_)   return;  // GPU is gone — stop trying

    // iOS: skip frame acquisition entirely while in background.
    // vkAcquireNextImageKHR / vkWaitForFences with UINT64_MAX would block
    // indefinitely when no Metal drawable is available, causing the OS to
    // kill the app and display a black-screen transition animation.
    if (appInBackground_) return;

    // Detect external window resize (e.g. from menu SDL_SetWindowSize).
    // Skip until after the first successful present: window pixel size may not
    // have settled right after swapchain creation (especially on iOS/MoltenVK),
    // and calling RecreateSwapchain before the first present crashes in waitIdle.
    if (!needsSwapchainRecreation_ && window_ && firstFramePresented_)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w > 0 && h > 0 &&
            (static_cast<uint32_t>(w) != swapchain_.GetExtent().width ||
             static_cast<uint32_t>(h) != swapchain_.GetExtent().height))
        {
            needsSwapchainRecreation_ = true;
        }
    }

    // Recreate swapchain if flagged (window resize, present OUT_OF_DATE, etc.)
    if (needsSwapchainRecreation_ && window_)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w > 0 && h > 0)
            RecreateSwapchain(w, h);
        else
        {
            return; // window is minimized — skip frame
        }
    }

#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // On macOS, force post-process ON every frame. Must run BEFORE the pending
    // handler below so the offscreen target exists when SetActiveEffect is called
    // (otherwise SetActiveEffect defers the load and the pending handler can't
    // immediately load a custom effect like "bloom").
    if (!postProcess_.IsEnabled())
    {
        // OnSwapchainResized destroys ALL cached effect pipelines/framebuffers/
        // render passes.  Those objects may be referenced by the previous frame's
        // command buffer, which hasn't been fence-waited yet (fence wait is below
        // at line ~1125).  Wait for all GPU work to complete first so we don't
        // destroy objects still in use.
        vkDeviceWaitIdle(context_.GetDevice());
        auto ext = swapchain_.GetExtent();
        postProcess_.OnSwapchainResized(context_, ext.width, ext.height,
                                        framebuffer_.GetRenderPass());
        // Use passthrough as the bootstrap effect. If a custom effect is pending
        // (s_pendingPPDirty), the pending handler below will overwrite it immediately
        // after the offscreen is built.
        postProcess_.SetActiveEffect("passthrough");
        // Reset enabled_ to force SetEnabled to re-enter the build path
        postProcess_.SetEnabled(false);
        postProcess_.SetEnabled(true);

        // Prewarm pipelines against the PP offscreen render pass so the first
        // multi-viewport frame doesn't stutter from JIT pipeline compilation.
        // The viewport FBOs reuse the PP scene render pass (3 attachments).
        if (postProcess_.IsEnabled())
        {
            VkRenderPass ppRP = postProcess_.GetSceneRenderPass();
            if (ppRP != VK_NULL_HANDLE)
            {
                pipelineManager_.SetRenderPass(ppRP);
                pipelineManager_.PrewarmCommonPipelines();
                // Restore to swapchain render pass (BeginFrame will set the correct one)
                pipelineManager_.SetRenderPass(framebuffer_.GetRenderPass());
            }
        }
    }
#endif

    // Apply pending post-process activation requests.
    // s_pendingPPDirty is set by sr_vkPostProcessActivate / sr_vkPostProcessDeactivate.
    // On macOS this runs after the force-enable block, so the offscreen is already
    // built and SetActiveEffect loads the effect immediately (no deferral).
    //
    // This MUST run before the shader reload below: SetEnabled(true) calls
    // BuildOffscreen which registers the PP scene render pass with
    // pipelineManager_. DoReloadShaders's PP-RP prewarm depends on that
    // registration being current, and on the new render pass existing if PP
    // was just toggled on by a moviepack switch.
    if (s_pendingPPDirty)
    {
        s_pendingPPDirty = false;
        if (!s_pendingPPEffect.empty())
            postProcess_.SetActiveEffect(s_pendingPPEffect.c_str());
#if defined(__APPLE__) && !(TARGET_OS_IOS)
        // macOS: PP must always be enabled (depth preservation for MoltenVK).
        // Already handled by the force-enable block above.
        (void)s_pendingPPEnabled;
#else
        postProcess_.SetEnabled(s_pendingPPEnabled);
#endif
    }

    // Honour deferred shader reload. Multiple requests (moviepack deactivate +
    // activate) are coalesced into one rebuild here at the frame boundary.
    // Runs AFTER swapchain/PP setup so DoReloadShaders sees the final
    // framebuffer render pass and (if active) PP scene render pass, and its
    // prewarm covers both.
    if (pendingShaderReload_)
    {
        pendingShaderReload_ = false;
        DoReloadShaders();
        if (!IsInitialized()) return;
    }

    VkDevice device = context_.GetDevice();

    // Wait for previous frame using this slot's fence (5-second timeout to detect GPU hangs)
    VkResult fenceResult = vkWaitForFences(device, 1, &inFlightFences_[currentFrame_], VK_TRUE,
                                           5000000000ULL);  // 5 seconds
    if (fenceResult == VK_TIMEOUT)
    {
        std::cerr << "[Vulkan] GPU fence timeout — possible GPU hang or device lost\n";
        needsSwapchainRecreation_ = true;
        return;
    }
    if (fenceResult != VK_SUCCESS)
    {
        std::cerr << std::format("[Vulkan] vkWaitForFences error: {}\n", static_cast<int>(fenceResult));
        if (fenceResult == VK_ERROR_DEVICE_LOST)
        {
            std::cerr << "[Vulkan] GPU device lost — rendering stopped. Restart the game.\n";
            deviceLost_ = true;
        }
        return;
    }

    // GPU work for this slot is now complete — safe to free deferred resources.
    // Vertex buffers replaced mid-frame (P0-2 fix)
    vulkanQueue_.CleanupOldBuffers();
    // Reset staging pool ring for this slot — fence guarantees GPU is done reading it
    stagingPool_.Reset(currentFrame_);

    // Acquire next swapchain image — use rotating semaphore index for acquire,
    // then index render-finished semaphore by the acquired image index.
    VkResult result = vkAcquireNextImageKHR(device, swapchain_.GetSwapchain(), UINT64_MAX,
                                            imageAvailableSemaphores_[acquireSemaphoreIndex_],
                                            VK_NULL_HANDLE,
                                            &currentImageIndex_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        needsSwapchainRecreation_ = true;
        return;  // Fence stays signaled — next BeginFrame's wait will succeed
    }

    // Reset fence AFTER successful acquire to avoid deadlock on OUT_OF_DATE
    vkResetFences(device, 1, &inFlightFences_[currentFrame_]);
    // SUBOPTIMAL: still usable for this frame, but recreate after present.
    // On Android we use preTransform=IDENTITY which permanently produces SUBOPTIMAL —
    // suppress it to avoid an infinite recreation loop.
#ifndef __ANDROID__
    if (result == VK_SUBOPTIMAL_KHR)
        needsSwapchainRecreation_ = true;
#endif

    // Drain descriptor sets queued for deferred free MAX_FRAMES_IN_FLIGHT
    // frames ago — safe now that all in-flight work on this slot's prior
    // submission has finished (fence waited above). This also updates the
    // descriptor manager's "current slot" so subsequent InvalidateCache
    // calls queue into this slot (to be drained next time this slot
    // cycles around).
    descriptorManager_.DrainDeferred(currentFrame_);
    rRenderStats::Instance().SetDescriptorPoolCount(descriptorManager_.GetPoolCount());

    // Drain textures deferred for deletion — this slot's previous frame has now completed.
    pendingDeleteTextures_.drainWith(device, currentFrame_, [this](VkDevice dev, VkTextureInfo& tex) {
        // Invalidate cached descriptor sets first — they reference the view.
        // Sampler is owned by samplerCache_ — do NOT destroy it here.
        if (tex.view) descriptorManager_.InvalidateCache(tex.view);
        VK_DESTROY(vkDestroyImageView, dev, tex.view);
        if (tex.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(context_.GetAllocator(), tex.image, tex.imageAllocation);
            tex.image           = VK_NULL_HANDLE;
            tex.imageAllocation = VK_NULL_HANDLE;
        }
    });

    // Cache per-frame values to avoid per-draw system calls
    cachedFrameTime_ = static_cast<float>(tSysTimeFloat());

    // Begin command buffer
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass — when post-processing is active AND the previous
    // frame was rendering in-game 3D content, the scene renders into the
    // offscreen color+depth target instead of directly to the swapchain.
    // EndFrame() will run the post-process pass to composite it onto the
    // swapchain. When PP is disabled OR the previous frame was a menu/title
    // screen, this is the original direct path (no post-process overhead
    // and no risk of post-process shaders messing with menu text).
    // macOS PP force-enable already handled above (lines 873-902)
    const bool postProcessActive = postProcess_.IsEnabled() && s_lastFrameWasInGame;
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    if (postProcessActive)
    {
        rpInfo.renderPass  = postProcess_.GetSceneRenderPass();
        rpInfo.framebuffer = postProcess_.GetSceneFramebuffer();
    }
    else
    {
        rpInfo.renderPass  = framebuffer_.GetRenderPass();
        rpInfo.framebuffer = framebuffer_.GetFramebuffer(currentImageIndex_);
    }
    // Always tell the pipeline manager which render pass is active — it
    // caches pipelines per-renderPass, and binding a pipeline from the
    // wrong cache causes "render pass incompatible" validation errors and
    // garbage rendering. This MUST run in both branches, not just the PP
    // branch, because the previous frame may have left renderPass_ pointing
    // at the other render pass.
    pipelineManager_.SetRenderPass(rpInfo.renderPass);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchain_.GetExtent();

    // Clear values — when post-processing is active, the offscreen render
    // pass has 3 attachments: scene color, scene emissive, depth. The
    // emissive attachment clears to transparent black so pixels that don't
    // have an emissive hook contribute nothing to bloom.
    VkClearValue clearValues[3]{};
    clearValues[0].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
    if (postProcessActive)
    {
        clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};   // emissive clears to 0
        clearValues[2].depthStencil = {0.0f, 0};             // reverse-Z: clear to far
        rpInfo.clearValueCount = 3;
    }
    else
    {
        clearValues[1].depthStencil = {0.0f, 0};             // reverse-Z: clear to far
        rpInfo.clearValueCount = 2;  // swapchain: 2 attachments
    }
    rpInfo.pClearValues = clearValues;

    // === Render graph setup ===
    // Create shadow maps first (if needed) so we know if they're available
    // for resource registration.
    if (sr_shadowMode == rSHADOW_MAP && !shadowMapsCreated_)
        CreateShadowMaps();

    // Declare per-frame pass dependencies and compile barrier sets.
    // For transitions already covered by render-pass subpass dependencies,
    // the computed barrier is redundant but harmless (Vulkan merges them).
    {
        const bool hasShadow = (sr_shadowMode == rSHADOW_MAP && shadowMapsCreated_);

        renderGraph_.Reset();

        if (postProcessActive)
        {
            renderGraph_.SetResource(RGResourceId::SceneColor,
                                     postProcess_.GetOffscreenColorImage(),
                                     VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
            renderGraph_.SetResource(RGResourceId::SceneEmissive,
                                     postProcess_.GetOffscreenEmissiveImage(),
                                     VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
            renderGraph_.SetResource(RGResourceId::SceneDepth,
                                     postProcess_.GetOffscreenDepthImage(),
                                     VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        if (hasShadow)
        {
            // Shadow maps end each frame in DEPTH_STENCIL_READ_ONLY_OPTIMAL
            // (render pass finalLayout). Use that as the starting layout.
            renderGraph_.SetResource(RGResourceId::ShadowMap0,
                                     shadowMaps_[0].depthImage,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                     VK_IMAGE_ASPECT_DEPTH_BIT);
            renderGraph_.SetResource(RGResourceId::ShadowMap1,
                                     shadowMaps_[1].depthImage,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                     VK_IMAGE_ASPECT_DEPTH_BIT);
        }

        // Shadow pass: writes shadow depth maps (finalLayout = DEPTH_STENCIL_READ_ONLY)
        if (hasShadow)
            renderGraph_.AddPass({"shadow", {},
                                  {RGResourceId::ShadowMap0, RGResourceId::ShadowMap1},
                                  nullptr});

        // Scene pass: reads shadow maps, writes offscreen color/emissive/depth
        {
            std::vector<RGResourceId> sceneReads;
            if (hasShadow)
            {
                sceneReads.push_back(RGResourceId::ShadowMap0);
                sceneReads.push_back(RGResourceId::ShadowMap1);
            }
            std::vector<RGResourceId> sceneWrites;
            if (postProcessActive)
                sceneWrites = {RGResourceId::SceneColor,
                               RGResourceId::SceneEmissive,
                               RGResourceId::SceneDepth};
            renderGraph_.AddPass({"scene",
                                  std::move(sceneReads), std::move(sceneWrites),
                                  nullptr});
        }

        // Post-process pass: reads offscreen targets, composites onto swapchain
        if (postProcessActive)
            renderGraph_.AddPass({"postprocess",
                                  {RGResourceId::SceneColor,
                                   RGResourceId::SceneEmissive,
                                   RGResourceId::SceneDepth},
                                  {RGResourceId::SwapchainOut}, nullptr});

        renderGraph_.Compile();
    }

    // === Shadow map pass (before main render pass) ===
    // Uses shadow vertices collected from the PREVIOUS frame (one-frame lag, imperceptible).
    // Must run before the main render pass so shadow maps are ready for sampling.
    if (sr_shadowMode == rSHADOW_MAP)
    {
        if (shadowMapsCreated_)
        {
            renderGraph_.EmitBarriersForPass(cmd, "shadow");
            RenderShadowPass(cmd);
        }
        // Clear dynamic shadow vertices (already consumed by shadow pass).
        // Static vertices persist until InvalidateShadowStatic() is called.
        // After first frame with static collection, mark static as clean.
        auto& queue = rRenderQueue::Instance();
        if (queue.IsShadowStaticDirty() && !queue.GetShadowStaticVertices().empty())
            queue.SetShadowStaticClean();
        queue.ClearShadowDynamic();
    }
    else
    {
        rRenderQueue::Instance().ClearShadowDynamic();
    }

    // === Wall compute pass (before main render pass) ===
    // Swap segment accumulators: prev = segments collected last frame (ready to dispatch),
    // current = empty buffer for this frame's wall collection.
    // Reset viewport accumulation guard so the first viewport accumulates.
    std::swap(sg_wallSegsCurrent_, sg_wallSegsPrev_);
    sg_wallSegsCurrent_.clear();
    sg_wallAccumViewportN_ = 0;
    DispatchWallCompute(cmd);  // uploads sg_wallSegsPrev_ to SSBO, dispatches, barriers

    // Track offscreen image layouts: render pass declares initialLayout=UNDEFINED,
    // so content is discarded — record this so we know the images are not readable.
    if (postProcessActive)
        postProcess_.NotifySceneRenderPassBeginning();

    // Emit render graph barriers for the scene pass. For shadow-mode frames,
    // this ensures shadow maps are in DEPTH_STENCIL_READ_ONLY_OPTIMAL before
    // the scene fragment shader samples them (the shadow render pass finalLayout
    // guarantees this, so in practice no barrier is emitted here).
    renderGraph_.EmitBarriersForPass(cmd, "scene");

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vulkanQueue_.InvalidateBindingCache();  // new render pass — force pipeline/descriptor rebind

    // Write arena bounds + shadow state into this frame's lighting UBO up front,
    // so that even frames without any lit draws (menus, etc.) have valid data for
    // shader hooks and post-process effects.
    if (lightingUBOMapped_[currentFrame_])
    {
        LightingUBO* ubo = static_cast<LightingUBO*>(lightingUBOMapped_[currentFrame_]);
        ubo->arenaBBox[0] = s_arenaBoundsLow[0];
        ubo->arenaBBox[1] = s_arenaBoundsLow[1];
        ubo->arenaBBox[2] = s_arenaBoundsHigh[0];
        ubo->arenaBBox[3] = s_arenaBoundsHigh[1];
        ubo->shadowEnabled = (sr_shadowMode == rSHADOW_MAP && shadowMapsCreated_) ? 1 : 0;
        if (ubo->shadowEnabled)
        {
            memcpy(ubo->shadowVP[0], shadowVP_[0], 64);
            memcpy(ubo->shadowVP[1], shadowVP_[1], 64);
        }
    }

    // Bind the per-frame lighting UBO descriptor set to set 1 for the entire frame.
    // The render queue only rebinds set 0 (texture) per draw — set 1 persists until EndFrame.
    if (lightingDescSet_[currentFrame_] != VK_NULL_HANDLE)
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineManager_.GetLayout(),
                                1, 1, &lightingDescSet_[currentFrame_],
                                0, nullptr);
    }

    // Reset per-frame viewport FBO count (will be set by BeginViewportFBO calls)
    viewportFBOCount_ = 0;
    activeViewportFBO_ = -1;  // guard: reset in case last frame didn't close FBO cleanly

    // Apply viewport/scissor. If Viewport() was called before BeginFrame() (e.g. the
    // first split-screen player calls conf->Select() before any draw triggers BeginFrame),
    // use those cached values so the first player gets the correct sub-viewport.
    // Otherwise default to full-screen.
    if (cachedViewport_[2] == 0)
    {
        cachedViewport_[0] = 0;
        cachedViewport_[1] = 0;
        cachedViewport_[2] = static_cast<int>(swapchain_.GetExtent().width);
        cachedViewport_[3] = static_cast<int>(swapchain_.GetExtent().height);
    }

    {
        int x = cachedViewport_[0];
        int y = cachedViewport_[1];
        int w = cachedViewport_[2];
        int h = cachedViewport_[3];
        int screenH = static_cast<int>(swapchain_.GetExtent().height);

        // When on-screen keyboard is visible, squash the viewport to the top
        // portion of the screen so content isn't hidden behind the keyboard.
        // Only apply during menu/chat (not in-game) to avoid shifting 3D content.
        REAL kbFrac = sr_ScreenKeyboardHeightFraction();
        if (kbFrac > 0.0f && activeViewportFBO_ < 0 && !s_currentFrameIsInGame) {
            int kbPixels = static_cast<int>(screenH * kbFrac);
            h = std::max(h - kbPixels, 1);
            if (y < kbPixels) y = kbPixels;  // shift up
        }

        int vkY = screenH - y - h;

        // Negative-height viewport (VK_KHR_maintenance1): puts y=+1 (GL "up")
        // at the top of the screen region, replacing the per-vertex Y-flip
        // that uber.vert / uber_instanced.vert used to apply. vp.y is the
        // *bottom* of the region in framebuffer space; vp.height is negative.
        VkViewport vp{};
        vp.x = static_cast<float>(x);
        vp.y = static_cast<float>(vkY + h);
        vp.width = static_cast<float>(w);
        vp.height = -static_cast<float>(h);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        int scX = std::max(x, 0);
        int scY = std::max(vkY, 0);
        int scRight  = std::min(x + w, static_cast<int>(swapchain_.GetExtent().width));
        int scBottom = std::min(vkY + h, static_cast<int>(swapchain_.GetExtent().height));
        int scW = std::max(scRight - scX, 0);
        int scH = std::max(scBottom - scY, 0);
        VkRect2D scissor{};
        scissor.offset = {scX, scY};
        scissor.extent = {static_cast<uint32_t>(scW), static_cast<uint32_t>(scH)};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    // Depth bias starts at zero; sr_DepthOffset will call PolygonOffset to change it.
    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);

    // Select the per-frame vertex buffer and reset its offset — GPU has finished
    // this frame slot's previous work (fence waited above), so it's safe to reuse.
    vulkanQueue_.SetCurrentFrame(currentFrame_);
    vulkanQueue_.ResetFrameOffset();

    frameStarted_ = true;
    lightingDirty_ = true;  // force UBO write on first lit draw of the frame
}

void vkRenderer::SwapBuffers()
{

    if (!frameStarted_)
        BeginFrame();
    EndFrame();
}

void vkRenderer::EndFrame()
{
    if (!frameStarted_) [[unlikely]] return;

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    vkCmdEndRenderPass(cmd);

    // Flush deferred texture sub-image uploads — these were queued by TexSubImage2D
    // while a render pass was active, where transfer commands are not allowed.
    // Now that the render pass has ended, the command buffer accepts transfers.
    // Uploads are sorted by dstImage so each image only pays 2 barriers (open/close)
    // regardless of how many sub-regions were updated in the frame (e.g. font atlas
    // glyph additions: N glyph updates → 2 barriers total, not 2N).
    if (!pendingTexUploads_.empty())
    {
        std::sort(pendingTexUploads_.begin(), pendingTexUploads_.end(),
                  [](const PendingTexUpload& a, const PendingTexUpload& b){
                      return a.dstImage < b.dstImage;
                  });

        VkImage activeImage = VK_NULL_HANDLE;
        for (size_t i = 0; i <= pendingTexUploads_.size(); ++i)
        {
            VkImage nextImage = (i < pendingTexUploads_.size())
                                ? pendingTexUploads_[i].dstImage
                                : VK_NULL_HANDLE;
            if (nextImage != activeImage)
            {
                // Close previous image (transition back to shader-readable)
                if (activeImage != VK_NULL_HANDLE)
                    TransitionImageLayout(cmd, activeImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

                activeImage = nextImage;

                // Open new image (transition to transfer-writable)
                if (activeImage != VK_NULL_HANDLE)
                    TransitionImageLayout(cmd, activeImage,
                        pendingTexUploads_[i].srcLayout,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
            if (i < pendingTexUploads_.size())
            {
                auto& up = pendingTexUploads_[i];
                vkCmdCopyBufferToImage(cmd, up.srcBuffer, up.dstImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &up.region);
            }
        }
        pendingTexUploads_.clear();
    }

    // Track offscreen image layouts after scene render pass: finalLayout transitions
    // color/emissive to SHADER_READ_ONLY, depth to DEPTH_STENCIL_READ_ONLY.
    const bool postProcessActive = postProcess_.IsEnabled() && s_lastFrameWasInGame;
    if (postProcessActive)
        postProcess_.NotifySceneRenderPassEnded();

    // Run the post-process pass ONLY if the scene actually rendered into the
    // offscreen target this frame. BeginFrame sets this based on
    // s_lastFrameWasInGame — we must use the same gate here so menu/title
    // frames that rendered straight to the swapchain don't also try to run
    // post-process (which would sample an uninitialized offscreen image).
    if (postProcessActive)
    {
        // Emit render graph barriers for the PP pass. The scene render pass's
        // finalLayout + subpass dependency already transition color/emissive to
        // SHADER_READ_ONLY and depth to DEPTH_STENCIL_READ_ONLY; this explicit
        // barrier is an additional safety net that documents the PP dependency
        // on all three offscreen images.
        renderGraph_.EmitBarriersForPass(cmd, "postprocess");

        postProcess_.Execute(cmd, currentFrame_,
                             framebuffer_.GetFramebuffer(currentImageIndex_),
                             swapchain_.GetExtent(),
                             cachedFrameTime_);
        // Restore the pipeline manager to the scene (offscreen) render pass
        // so that any draws in the NEXT frame hit the correct cache entry.
        pipelineManager_.SetRenderPass(postProcess_.GetSceneRenderPass());
    }
    else if (postProcess_.IsEnabled())
    {
        // PP is enabled but this was a non-game frame — make sure the pipeline
        // manager is pointing at the swapchain render pass for the draws that
        // already happened this frame (matching what BeginFrame set up).
        pipelineManager_.SetRenderPass(framebuffer_.GetRenderPass());
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] vkEndCommandBuffer failed — skipping present\n";
        frameStarted_ = false;
        return;
    }

    // Submit command buffer — wait on imageAvailable (indexed by acquire rotation),
    // signal renderFinished (indexed by swapchain image to avoid present reuse conflicts).
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[acquireSemaphoreIndex_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentImageIndex_]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(context_.GetGraphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]);
    if (submitResult != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] vkQueueSubmit failed (" << submitResult << ") — skipping present\n";
        if (submitResult == VK_ERROR_DEVICE_LOST)
        {
            std::cerr << "[Vulkan] GPU device lost — rendering stopped. Restart the game.\n";
            deviceLost_ = true;
        }
        frameStarted_ = false;
        return;
    }

    // Present — wait on renderFinished semaphore
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {swapchain_.GetSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &currentImageIndex_;

    VkResult presentResult = vkQueuePresentKHR(context_.GetPresentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
        needsSwapchainRecreation_ = true;
#ifndef __ANDROID__
    // On Android, SUBOPTIMAL is expected (preTransform=IDENTITY vs currentTransform).
    // Suppress it to avoid infinite recreation loop.
    else if (presentResult == VK_SUBOPTIMAL_KHR)
        needsSwapchainRecreation_ = true;
#endif
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR)
        firstFramePresented_ = true;

    // NOTE: Vertex buffer and FBO cleanup moved to BeginFrame (after fence wait)
    // to avoid use-after-free on in-flight GPU resources.

    acquireSemaphoreIndex_ = (acquireSemaphoreIndex_ + 1) % static_cast<uint32_t>(imageAvailableSemaphores_.size());
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    frameStarted_ = false;

    // Reset cached viewport so BeginFrame detects whether Viewport() is called
    // before the first draw next frame (split-screen first-player fix).
    cachedViewport_[0] = cachedViewport_[1] = cachedViewport_[2] = cachedViewport_[3] = 0;

    // Roll the in-game tracking flag forward. Next frame's BeginFrame reads
    // s_lastFrameWasInGame to decide whether to redirect to the post-process
    // offscreen target. This is updated progressively by sr_vkSetRenderContext.
    s_lastFrameWasInGame = s_currentFrameIsInGame;
    s_currentFrameIsInGame = false;
}

// ============================================================================
// Matrix operations (delegate to rMatrixStack)
// ============================================================================

void vkRenderer::ProjMatrix()     { End(true); currentStack_ = &projectionStack_; }
void vkRenderer::ModelMatrix()    { End(true); currentStack_ = &modelviewStack_; }
void vkRenderer::TexMatrix()      { End(true); currentStack_ = &textureStack_; }
void vkRenderer::PushMatrix()     { currentStack_->Push(); }
void vkRenderer::PopMatrix()      { currentStack_->Pop(); }
void vkRenderer::IdentityMatrix() { currentStack_->LoadIdentity(); }

void vkRenderer::MultMatrix(REAL mdata[4][4])
{
    currentStack_->MultRowMajor(reinterpret_cast<const float(*)[4]>(mdata));
}

void vkRenderer::MultMatrixFlat(const REAL* mdata)
{
    currentStack_->Mult(mdata);
}

void vkRenderer::LoadMatrixFlat(const REAL* mdata)
{
    currentStack_->Load(mdata);
}

void vkRenderer::ScaleMatrix(REAL f)
{
    currentStack_->Scale(f);
}

void vkRenderer::ScaleMatrix(REAL f1, REAL f2, REAL f3)
{
    currentStack_->Scale(f1, f2, f3);
}

void vkRenderer::TranslateMatrix(REAL x, REAL y, REAL z)
{
    currentStack_->Translate(x, y, z);
}

void vkRenderer::RotateMatrix(REAL angle, REAL x, REAL y, REAL z)
{
    currentStack_->Rotate(angle, x, y, z);
}

// GL→VK depth remap + reverse-Z, applied on top of GLM's GL-style projection:
// maps GL-depth [-w,w] → VK-depth [w,0] (reverse-Z, near = w / far = 0).
// Float depth precision is then uniform across the whole range, removing the
// precision pressure that drove the eCamera zNear *= .3f shrink and the macOS
// depth-sampling kludge.
//
// Composite of glToVk * reverseZ:
//   GL→VK depth | 1 0 0   0  |   z' = (z + w) / 2
//               | 0 1 0   0  |
//               | 0 0 0.5 0.5|
//               | 0 0 0   1  |
//   Reverse-Z   | 1 0  0 0 |    z' = w - z
//               | 0 1  0 0 |
//               | 0 0 -1 1 |
//               | 0 0  0 1 |
// = reverseZ * glToVk =
//   | 1 0  0    0  |
//   | 0 1  0    0  |
//   | 0 0 -0.5  0.5|
//   | 0 0  0    1  |
//
// Y-flip is NOT here. It is performed by the rasterizer via
// VK_KHR_maintenance1 negative-height viewport (vp.height < 0) — see the
// VkViewport setup in this file. Baking Y-flip into the projection matrix
// would leave the HUD phase upside-down because rRenderQueue::SetupPhase
// loads an identity projection for HUD, bypassing Frustum/Ortho/Perspective.
// The negative viewport flips Y at the rasterizer regardless of matrix,
// catching every path uniformly.
//
// Pipelines pair this with depthCompareOp = GREATER_OR_EQUAL and clear depth
// = 0.0. Shadow maps still use forward-Z (separate VP matrix in
// ComputeShadowVPMatrices, separate pipeline in BuildShadowPipeline) and a
// positive-height viewport.
static glm::mat4 VkClipReverseZ()
{
    glm::mat4 c(1.0f);
    // Only depth remap + reverse-Z here. Y-flip is in the negative-height
    // viewport (see vkCmdSetViewport call sites in this file).
    c[2][2] = -0.5f;
    c[3][2] =  0.5f;
    return c;
}

void vkRenderer::Ortho(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar)
{
    glm::mat4 m = glm::ortho(static_cast<float>(left), static_cast<float>(right),
                              static_cast<float>(bottom), static_cast<float>(top),
                              static_cast<float>(zNear), static_cast<float>(zFar));
    glm::mat4 vk = VkClipReverseZ() * m;
    currentStack_->Mult(glm::value_ptr(vk));
}

void vkRenderer::Frustum(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar)
{
    glm::mat4 f = glm::frustum(static_cast<float>(left), static_cast<float>(right),
                                static_cast<float>(bottom), static_cast<float>(top),
                                static_cast<float>(zNear), static_cast<float>(zFar));
    glm::mat4 vk = VkClipReverseZ() * f;
    currentStack_->Mult(glm::value_ptr(vk));
}

void vkRenderer::Perspective(REAL fovy, REAL aspect, REAL zNear, REAL zFar)
{
    glm::mat4 p = glm::perspective(glm::radians(static_cast<float>(fovy)),
                                    static_cast<float>(aspect),
                                    static_cast<float>(zNear),
                                    static_cast<float>(zFar));
    glm::mat4 vk = VkClipReverseZ() * p;
    currentStack_->Mult(glm::value_ptr(vk));
}

void vkRenderer::LookAt(REAL eyeX, REAL eyeY, REAL eyeZ,
                         REAL centerX, REAL centerY, REAL centerZ,
                         REAL upX, REAL upY, REAL upZ)
{
    currentStack_->LookAt(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
}

// ============================================================================
// State queries
// ============================================================================

void vkRenderer::GetModelviewMatrix(float* matrix)
{
    if (matrix) memcpy(matrix, modelviewStack_.Get(), 16 * sizeof(float));
}

void vkRenderer::GetProjectionMatrix(float* matrix)
{
    if (matrix) memcpy(matrix, projectionStack_.Get(), 16 * sizeof(float));
}

void vkRenderer::GetMVPMatrix(float* matrix)
{
    if (!matrix) return;
    glm::mat4 mvp = projectionStack_.GetMat4() * modelviewStack_.GetMat4();
    memcpy(matrix, glm::value_ptr(mvp), 16 * sizeof(float));
}

void vkRenderer::GetColor(float* color)
{
    if (color) memcpy(color, currentColor_, 4 * sizeof(float));
}

bool vkRenderer::IsEnabled(int capability)
{
    if (capability == rGLConst::DepthTest) return depthTestEnabled_;
    if (capability == rGLConst::Blend) return blendEnabled_;
    if (capability == rGLConst::CullFace) return cullFaceEnabled_;
    if (capability == rGLConst::Lighting) return lightingEnabled_;
    if (capability == rGLConst::Texture2D) return textureEnabled_;
    return false;
}

// ============================================================================
// State management
// ============================================================================

void vkRenderer::EnableState(int cap)
{
    if (cap == rGLConst::DepthTest) depthTestEnabled_ = true;
    else if (cap == rGLConst::Blend) blendEnabled_ = true;
    else if (cap == rGLConst::CullFace) cullFaceEnabled_ = true;
    else if (cap == rGLConst::Lighting) lightingEnabled_ = true;
    else if (cap == rGLConst::Texture2D) textureEnabled_ = true;
    // Scissor test enable: in Vulkan the scissor rect is always active;
    // the game will call Scissor() immediately after, so nothing to do here.
}

void vkRenderer::DisableState(int cap)
{
    if (cap == rGLConst::DepthTest) depthTestEnabled_ = false;
    else if (cap == rGLConst::Blend) blendEnabled_ = false;
    else if (cap == rGLConst::CullFace) cullFaceEnabled_ = false;
    else if (cap == rGLConst::Lighting) lightingEnabled_ = false;
    else if (cap == rGLConst::Texture2D) textureEnabled_ = false;
    else if (cap == rGLConst::ScissorTest && frameStarted_)
    {
        // Reset scissor to full viewport when scissor test is disabled
        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        VkRect2D fullScissor{};
        fullScissor.offset = {0, 0};
        fullScissor.extent = swapchain_.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &fullScissor);
    }
}

void vkRenderer::BlendFunc(int sfactor, int dfactor) { blendSrc_ = sfactor; blendDst_ = dfactor; }
void vkRenderer::DepthMask(bool write) { depthWriteEnabled_ = write; }
// Match GL convention directly: game-CW → Vulkan CW, game-CCW → Vulkan CCW.
// The Y-flip from the negative-height viewport (VK_KHR_maintenance1) reverses
// framebuffer-space winding compared to NDC. Removing the previous shader
// Y-flip and adding the rasterizer Y-flip both reverse winding once, so the
// effective screen-space winding is unchanged from the original GL layout.
// Pass-through CCW therefore still produces correct front-face selection.
void vkRenderer::FrontFace(int mode) { frontFaceCW_ = (mode == 0x0900); } // GL_CW (0x0900) → Vulkan CW

void vkRenderer::PolygonOffset(REAL factor, REAL units)
{
    if (!frameStarted_) [[unlikely]] return;
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    // OpenGL: depthBias = factor * maxSlope + units * minResolvable
    // Vulkan: depthBias = slopeFactor * maxSlope + constantFactor * minResolvable [+ clamp]
    // Mapping: slopeFactor = factor, constantFactor = units.
    //
    // Reverse-Z sign flip: callers pass GL-convention values where negative
    // bias pulls a fragment TOWARD the camera (smaller depth = closer in
    // forward-Z). Under reverse-Z, closer = larger depth, so the same intent
    // requires the opposite sign. Negate here so callers stay unchanged.
    vkCmdSetDepthBias(cmd, -static_cast<float>(units), 0.0f, -static_cast<float>(factor));
}
// ============================================================================
// Viewport / Scissor / Clear
// ============================================================================

void vkRenderer::Viewport(int x, int y, int w, int h)
{
    cachedViewport_[0] = x; cachedViewport_[1] = y;
    cachedViewport_[2] = w; cachedViewport_[3] = h;

    if (frameStarted_)
    {
        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        // OpenGL viewport: y is from the bottom of the window.
        // Vulkan viewport: y is from the top. Convert: vkY = targetH - y - h.
        // Use FBO height when rendering into a viewport FBO, swapchain height otherwise.
        int targetH, targetW;
        if (activeViewportFBO_ >= 0) {
            ViewportFBO& vfbo = viewportFBOs_[currentFrame_][activeViewportFBO_];
            targetH = vfbo.height;
            targetW = vfbo.width;
        } else {
            targetH = static_cast<int>(swapchain_.GetExtent().height);
            targetW = static_cast<int>(swapchain_.GetExtent().width);
        }
        int vkY = targetH - y - h;

        // Negative-height viewport (VK_KHR_maintenance1) — see the matching
        // block above for rationale. vp.y is the bottom of the region in
        // framebuffer space, vp.height is negative.
        VkViewport vp{};
        vp.x = static_cast<float>(x);
        vp.y = static_cast<float>(vkY + h);
        vp.width = static_cast<float>(w);
        vp.height = -static_cast<float>(h);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        // Keep scissor in sync with viewport, clamped to valid Vulkan bounds.
        // Scissor uses framebuffer-space coords, NOT NDC, so it stays
        // unaffected by the negative-height trick.
        int scX = std::max(x, 0);
        int scY = std::max(vkY, 0);
        int scRight  = std::min(x + w, targetW);
        int scBottom = std::min(vkY + h, targetH);
        int scW = std::max(scRight - scX, 0);
        int scH = std::max(scBottom - scY, 0);
        VkRect2D scissor{};
        scissor.offset = {scX, scY};
        scissor.extent = {static_cast<uint32_t>(scW), static_cast<uint32_t>(scH)};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }
}

void vkRenderer::GetViewport(int vp[4])
{
    if (vp) memcpy(vp, cachedViewport_, 4 * sizeof(int));
}

void vkRenderer::Scissor(int x, int y, int w, int h)
{
    if (!frameStarted_) [[unlikely]] return;
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    // Convert from OpenGL bottom-origin to Vulkan top-origin
    int targetH, targetW;
    if (activeViewportFBO_ >= 0) {
        ViewportFBO& vfbo = viewportFBOs_[currentFrame_][activeViewportFBO_];
        targetH = vfbo.height;
        targetW = vfbo.width;
    } else {
        targetH = static_cast<int>(swapchain_.GetExtent().height);
        targetW = static_cast<int>(swapchain_.GetExtent().width);
    }
    int vkY = targetH - y - h;
    int scX = std::max(x, 0);
    int scY = std::max(vkY, 0);
    int scRight  = std::min(x + w, targetW);
    int scBottom = std::min(vkY + h, targetH);
    int scW = std::max(scRight - scX, 0);
    int scH = std::max(scBottom - scY, 0);
    VkRect2D scissor{};
    scissor.offset = {scX, scY};
    scissor.extent = {static_cast<uint32_t>(scW), static_cast<uint32_t>(scH)};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void vkRenderer::ClearColor(REAL r, REAL g, REAL b, REAL a)
{
    clearColor_[0] = r; clearColor_[1] = g; clearColor_[2] = b; clearColor_[3] = a;
}

void vkRenderer::Clear(bool color, bool depth, bool /*stencil*/)
{
    if (!frameStarted_) [[unlikely]] return;
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // Color clears always use full framebuffer extent.
    if (color)
    {
        VkClearAttachment clr{};
        clr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clr.colorAttachment = 0;
        clr.clearValue.color =
            {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};

        VkClearRect rect{};
        rect.rect.offset = {0, 0};
        rect.rect.extent = swapchain_.GetExtent();
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clr, 1, &rect);
    }

    // Depth clears are scoped to the current viewport so per-viewport clears
    // (OpaqueStatic phase) don't wipe other viewports' depth values.
    // Without this, geometry from one viewport can "invade" another because
    // a later viewport's depth clear erases the earlier viewport's depth,
    // leaving stale depth that passes the depth test for any subsequent draw.
    if (depth)
    {
        VkClearAttachment clr{};
        clr.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clr.clearValue.depthStencil = {0.0f, 0};   // reverse-Z: clear to far

        int screenH = static_cast<int>(swapchain_.GetExtent().height);
        int vkY = screenH - cachedViewport_[1] - cachedViewport_[3];
        int x = std::max(cachedViewport_[0], 0);
        int y = std::max(vkY, 0);
        int r = std::min(cachedViewport_[0] + cachedViewport_[2],
                         static_cast<int>(swapchain_.GetExtent().width));
        int b = std::min(vkY + cachedViewport_[3], screenH);
        int w = std::max(r - x, 0);
        int h = std::max(b - y, 0);

        VkClearRect rect{};
        rect.rect.offset = {x, y};
        rect.rect.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clr, 1, &rect);
    }
}

// ============================================================================
// Vertex / Color / TexCoord (immediate mode batch)
// ============================================================================

void vkRenderer::Color(REAL r, REAL g, REAL b)
{
    currentColor_[0] = r; currentColor_[1] = g;
    currentColor_[2] = b; currentColor_[3] = 1.0f;
}

void vkRenderer::Color(REAL r, REAL g, REAL b, REAL a)
{
    currentColor_[0] = r; currentColor_[1] = g;
    currentColor_[2] = b; currentColor_[3] = a;
}

void vkRenderer::TexCoord(REAL u, REAL v)
{
    currentTexCoord_[0] = u; currentTexCoord_[1] = v;
}

void vkRenderer::TexCoord(REAL u, REAL v, REAL) { TexCoord(u, v); }
void vkRenderer::TexCoord(REAL u, REAL v, REAL, REAL) { TexCoord(u, v); }

// Immediate mode — dead code, all geometry goes through batch render queue
void vkRenderer::Vertex(REAL x, REAL y) {}
void vkRenderer::Vertex(REAL x, REAL y, REAL z) {}
void vkRenderer::Vertex3(REAL* x) {}
void vkRenderer::Vertex(REAL x, REAL y, REAL z, REAL) {}
void vkRenderer::BeginLines() {}
void vkRenderer::BeginTriangles() {}
void vkRenderer::BeginQuads() {}
void vkRenderer::BeginLineStrip() {}
void vkRenderer::BeginTriangleStrip() {}
void vkRenderer::BeginQuadStrip() {}
void vkRenderer::BeginTriangleFan() {}
void vkRenderer::BeginLineLoop() {}
void vkRenderer::BeginPolygon() {}
void vkRenderer::End(bool) {}

// ============================================================================
// Texture lifecycle (stub — will be fully implemented in Phase 2)
// ============================================================================

unsigned int vkRenderer::GetBoundTexture2D() { return boundTexture2D_; }

int vkRenderer::GetMaxTextureSize()
{
    return static_cast<int>(context_.GetDeviceProperties().limits.maxImageDimension2D);
}

unsigned int vkRenderer::GenTexture()
{
    unsigned int id = nextTextureId_++;
    textures_[id] = VkTextureInfo{};
    return id;
}

void vkRenderer::DeleteTexture(unsigned int id)
{
    auto it = textures_.find(id);
    if (it == textures_.end()) return;

    // Invalidate descriptor cache before queuing for deletion — the drain
    // lambda also calls InvalidateCache as a safety net, but that call runs
    // AFTER the image view is destroyed, leaving dangling descriptor sets
    // in deferredFree_ if they were created between here and the drain.
    // Calling it here guarantees descriptor sets are deferred in the same
    // slot as the texture, so DrainDeferred frees them before drainWith
    // destroys the view.
    if (it->second.view)
        descriptorManager_.InvalidateCache(it->second.view);

    pendingDeleteTextures_.queue(std::move(it->second), descriptorManager_.GetCurrentSlot());
    textures_.erase(it);
}

void vkRenderer::BindTexture(int, unsigned int tex)
{
    boundTexture2D_ = tex;
    // Auto-register texture IDs allocated through RenderGenTexture()
    if (tex != 0 && textures_.find(tex) == textures_.end())
        textures_[tex] = VkTextureInfo{};
}
void vkRenderer::TexParameter(int target, int pname, int param)
{
    if (target != rGLConst::Texture2D || boundTexture2D_ == 0) return;
    auto it = textures_.find(boundTexture2D_);
    if (it == textures_.end()) return;

    auto& tex = it->second;
    bool needsRecreate = false;

    if (pname == rGLConst::TextureMinFilter) {
        VkFilter f = (param == rGLConst::Nearest || param == rGLConst::NearestMipmapNearest
                      || param == rGLConst::NearestMipmapLinear)
            ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        bool mip = (param == rGLConst::LinearMipmapLinear || param == rGLConst::LinearMipmapNearest
                 || param == rGLConst::NearestMipmapLinear || param == rGLConst::NearestMipmapNearest);
        if (tex.minFilter != f || tex.usesMipmapFilter != mip) {
            tex.minFilter = f; tex.usesMipmapFilter = mip; needsRecreate = true;
        }
    } else if (pname == rGLConst::TextureMagFilter) {
        VkFilter f = (param == rGLConst::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        if (tex.magFilter != f) { tex.magFilter = f; needsRecreate = true; }
    } else if (pname == rGLConst::TextureWrapS) {
        VkSamplerAddressMode m = (param == rGLConst::ClampToEdge) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            : (param == rGLConst::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (tex.wrapS != m) { tex.wrapS = m; needsRecreate = true; }
    } else if (pname == rGLConst::TextureWrapT) {
        VkSamplerAddressMode m = (param == rGLConst::ClampToEdge) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            : (param == rGLConst::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (tex.wrapT != m) { tex.wrapT = m; needsRecreate = true; }
    }
    // Swizzle parameters are handled during data upload, not by sampler
    // If sampler doesn't exist yet (TexParameter called before TexImage2D),
    // just store the state — it will be used when TexImage2D creates the sampler.
    if (!needsRecreate || tex.sampler == VK_NULL_HANDLE)
        return;

    if (needsRecreate)
    {
        float maxLod = tex.usesMipmapFilter
            ? ((tex.mipLevels > 1) ? static_cast<float>(tex.mipLevels - 1) : 1.0f)
            : 0.25f;
        VkSampler newSampler = GetOrCreateSampler(tex.minFilter, tex.magFilter,
                                                   tex.wrapS, tex.wrapT, maxLod);
        if (newSampler != tex.sampler)
        {
            // Invalidate cached descriptor set — the (view, sampler) pair has changed
            if (tex.view != VK_NULL_HANDLE)
                descriptorManager_.InvalidateCache(tex.view);
            // Sampler is owned by samplerCache_ — no deferred destroy needed
            tex.sampler = newSampler;
        }
    }
}

void vkRenderer::TexImage2D(int target, int level, int /*internalFormat*/,
                            int width, int height, int /*border*/,
                            int format, int /*type*/, const void* data)
{
    if (target == rGLConst::ProxyTexture2D) return;
    if (level > 0) return;
    if (boundTexture2D_ == 0) return;
    if (!context_.IsValid()) return;

    // CRITICAL: Store texture ID locally — do NOT hold references to the textures_ map
    // across operations that may trigger map mutations (staging buffer, command submission).
    unsigned int texId = boundTexture2D_;
    VkDevice device = context_.GetDevice();

    // Defer destruction of old resources — in-flight render frames may still
    // be sampling the old sampler/image via their descriptor sets.
    // Synchronous uploads mean the GPU *upload* is done, but not the *render*.
    {
        auto it = textures_.find(texId);
        if (it != textures_.end() && it->second.image != VK_NULL_HANDLE)
        {
            // Invalidate descriptor cache now (CPU-side) so new descriptors
            // are allocated immediately, but delay GPU resource destruction
            // until the deferred slot cycles through all in-flight frames.
            // IMPORTANT: use GetCurrentSlot() — NOT currentFrame_ — so the
            // texture drain and the descriptor drain land in the *same* slot.
            // TexImage2D can be called from sr_FontBeginFrame() before the
            // renderer's lazy BeginFrame updates currentFrame_; at that point
            // currentSlot_ still equals the previous frame's slot, and
            // currentFrame_ has already been incremented to the new frame.
            // Using currentFrame_ here would drain the texture in the current
            // frame's BeginFrame (same frame it was created), before
            // DrainDeferred has freed the descriptor sets queued to currentSlot_.
            if (it->second.view)
                descriptorManager_.InvalidateCache(it->second.view);
            pendingDeleteTextures_.queue(std::move(it->second), descriptorManager_.GetCurrentSlot());
            textures_.erase(it);
        }
    }

    // Build all Vulkan resources into LOCAL variables (not map references)
    VkImage       newImage      = VK_NULL_HANDLE;
    VmaAllocation newAllocation = VK_NULL_HANDLE;
    VkImageView   newView       = VK_NULL_HANDLE;
    VkSampler     newSampler    = VK_NULL_HANDLE;

    VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    bool swapRB = (format == rGLConst::BGRA || format == rGLConst::BGR);

    // Compute full mipmap chain depth
    uint32_t mipLevels = 1;
    {
        uint32_t dim = static_cast<uint32_t>(std::max(width, height));
        while (dim > 1) { dim >>= 1; ++mipLevels; }
    }

    // Create VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vkFormat;
    imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_SRC needed for blit-based mipmap generation
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    {
        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(context_.GetAllocator(), &imageInfo, &vmaAllocCI,
                           &newImage, &newAllocation, nullptr) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] VMA: failed to create texture image\n";
            return;
        }
    }

    // RGBA conversion buffer — declared at function scope so it can be moved into cpuData later
    std::vector<uint8_t> rgbaData;

    // Upload data directly to HOST_VISIBLE image (LINEAR tiling)
    if (width > 0 && height > 0)
    {
        int srcChannels = 4;
        if (format == rGLConst::RGB || format == rGLConst::BGR) srcChannels = 3;
        else if (format == rGLConst::Red || format == rGLConst::Luminance) srcChannels = 1;
        else if (format == rGLConst::LuminanceAlpha) srcChannels = 2;

        // Convert to RGBA (kept alive for cpuData reuse below)
        rgbaData.resize(static_cast<size_t>(width) * height * 4, 0);
        if (data)
        {
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (int i = 0; i < width * height; i++)
            {
                if (srcChannels == 4 && swapRB) {
                    rgbaData[i*4+0] = src[i*4+2];
                    rgbaData[i*4+1] = src[i*4+1];
                    rgbaData[i*4+2] = src[i*4+0];
                    rgbaData[i*4+3] = src[i*4+3];
                } else if (srcChannels == 4) {
                    rgbaData[i*4+0] = src[i*4+0];
                    rgbaData[i*4+1] = src[i*4+1];
                    rgbaData[i*4+2] = src[i*4+2];
                    rgbaData[i*4+3] = src[i*4+3];
                } else if (srcChannels == 3) {
                    rgbaData[i*4+0] = swapRB ? src[i*3+2] : src[i*3+0];
                    rgbaData[i*4+1] = src[i*3+1];
                    rgbaData[i*4+2] = swapRB ? src[i*3+0] : src[i*3+2];
                    rgbaData[i*4+3] = 255;
                } else if (srcChannels == 1) {
                    // Store in ALL channels: legacy fonts read .a (alpha blend),
                    // SDF fonts read .r (distance field). Both work with V=V=V=V.
                    rgbaData[i*4+0] = src[i];
                    rgbaData[i*4+1] = src[i];
                    rgbaData[i*4+2] = src[i];
                    rgbaData[i*4+3] = src[i];
                } else if (srcChannels == 2) {
                    rgbaData[i*4+0] = 255;
                    rgbaData[i*4+1] = 255;
                    rgbaData[i*4+2] = 255;
                    rgbaData[i*4+3] = src[i*2+1];
                }
            }
        }

        // Upload via staging buffer (DEVICE_LOCAL + OPTIMAL tiling)
        VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;
        rVulkanBuffer staging;
        if (!rVulkanBufferManager::CreateStagingBuffer(context_, rgbaData.data(), imageSize, staging))
        {
            vmaDestroyImage(context_.GetAllocator(), newImage, newAllocation);
            return;
        }

        VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

        // Transition ALL mip levels UNDEFINED → TRANSFER_DST (upload destination)
        TransitionImageLayout(cmd, newImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, mipLevels,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy staging buffer → mip level 0
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Generate mip levels by blitting each level from the previous.
        // Per-mip barriers use fine-grained subresource ranges — inline is clearest here.
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = newImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;
        int32_t mipW = width, mipH = height;
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            // Transition mip i-1: TRANSFER_DST → TRANSFER_SRC (ready to blit from)
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Blit mip i-1 → mip i (linear filter for quality)
            int32_t nextW = std::max(mipW / 2, 1);
            int32_t nextH = std::max(mipH / 2, 1);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i-1, 0, 1};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipW, mipH, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {nextW, nextH, 1};
            vkCmdBlitImage(cmd,
                           newImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            // Transition mip i-1: TRANSFER_SRC → SHADER_READ_ONLY
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            mipW = nextW;
            mipH = nextH;
        }
        // Transition the last mip level: TRANSFER_DST → SHADER_READ_ONLY
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
        rVulkanBufferManager::DestroyBuffer(context_.GetAllocator(), staging);
    }

    // Create image view covering all mip levels
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = newImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &newView) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create texture image view for texId=" << texId << std::endl;
        vmaDestroyImage(context_.GetAllocator(), newImage, newAllocation);
        return;
    }

    // Create sampler using tracked state (may have been set by TexParameter before this call)
    VkFilter savedMinFilter = VK_FILTER_LINEAR;
    VkFilter savedMagFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode savedWrapS = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode savedWrapT = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    bool savedUsesMipmap = true;
    {
        auto it2 = textures_.find(texId);
        if (it2 != textures_.end())
        {
            savedMinFilter = it2->second.minFilter;
            savedMagFilter = it2->second.magFilter;
            savedWrapS = it2->second.wrapS;
            savedWrapT = it2->second.wrapT;
            savedUsesMipmap = it2->second.usesMipmapFilter;
        }
    }

    float maxLod = savedUsesMipmap ? static_cast<float>(mipLevels - 1) : 0.25f;
    newSampler = GetOrCreateSampler(savedMinFilter, savedMagFilter, savedWrapS, savedWrapT, maxLod);
    if (newSampler == VK_NULL_HANDLE)
    {
        std::cerr << "[Vulkan] Failed to get/create texture sampler for texId=" << texId << std::endl;
        vkDestroyImageView(device, newView, nullptr);
        vmaDestroyImage(context_.GetAllocator(), newImage, newAllocation);
        return;
    }

    // NOW write everything to the map with a FRESH lookup (safe from rehash)
    VkTextureInfo newInfo{};
    newInfo.image           = newImage;
    newInfo.imageAllocation = newAllocation;
    newInfo.view = newView;
    newInfo.sampler = newSampler;
    newInfo.width = width;
    newInfo.height = height;
    newInfo.mipLevels = mipLevels;
    newInfo.minFilter = savedMinFilter;
    newInfo.magFilter = savedMagFilter;
    newInfo.wrapS = savedWrapS;
    newInfo.wrapT = savedWrapT;
    newInfo.usesMipmapFilter = savedUsesMipmap;
    // After upload + barrier: image is in SHADER_READ_ONLY_OPTIMAL
    newInfo.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Reuse the already-converted RGBA data from the staging upload (avoids double conversion)
    newInfo.cpuData = std::move(rgbaData);
    textures_[texId] = std::move(newInfo);
}

// TexSubImage2D uploads only the dirty sub-region (not the full texture).
// When called while a frame is recording (frameStarted_), the copy is deferred
// into the per-frame staging ring and flushed in EndFrame after the render pass
// ends. Multiple updates to the same image in one frame share a single pair of
// layout transitions (sorted+grouped in EndFrame).
void vkRenderer::TexSubImage2D(int /*target*/, int /*level*/,
                               int xoffset, int yoffset,
                               int width, int height,
                               int format, int /*type*/, const void* data)
{
    if (boundTexture2D_ == 0 || !context_.IsValid() || !data) return;

    unsigned int texId = boundTexture2D_;
    auto it = textures_.find(texId);
    if (it == textures_.end() || it->second.image == VK_NULL_HANDLE) return;

    auto& tex = it->second;
    int texW = tex.width;
    int texH = tex.height;
    if (texW <= 0 || texH <= 0) return;

    // Determine source channel count
    int srcChannels = 4;
    if (format == rGLConst::RGB || format == rGLConst::BGR) srcChannels = 3;
    else if (format == rGLConst::Red || format == rGLConst::Luminance) srcChannels = 1;
    else if (format == rGLConst::LuminanceAlpha) srcChannels = 2;

    // Ensure CPU copy exists
    if (tex.cpuData.size() != static_cast<size_t>(texW * texH * 4))
        tex.cpuData.resize(texW * texH * 4, 0);

    // Update the sub-region in the CPU copy (convert to RGBA)
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (int row = 0; row < height; ++row)
    {
        for (int col = 0; col < width; ++col)
        {
            int srcIdx = row * width + col;
            int dstIdx = (yoffset + row) * texW + (xoffset + col);
            if (dstIdx < 0 || dstIdx >= texW * texH) continue;

            if (srcChannels == 1) {
                tex.cpuData[dstIdx*4+0] = src[srcIdx];
                tex.cpuData[dstIdx*4+1] = src[srcIdx];
                tex.cpuData[dstIdx*4+2] = src[srcIdx];
                tex.cpuData[dstIdx*4+3] = src[srcIdx];
            } else if (srcChannels == 3) {
                tex.cpuData[dstIdx*4+0] = src[srcIdx*3+0];
                tex.cpuData[dstIdx*4+1] = src[srcIdx*3+1];
                tex.cpuData[dstIdx*4+2] = src[srcIdx*3+2];
                tex.cpuData[dstIdx*4+3] = 255;
            } else if (srcChannels == 4) {
                memcpy(&tex.cpuData[dstIdx*4], &src[srcIdx*4], 4);
            } else if (srcChannels == 2) {
                tex.cpuData[dstIdx*4+0] = 255;
                tex.cpuData[dstIdx*4+1] = 255;
                tex.cpuData[dstIdx*4+2] = 255;
                tex.cpuData[dstIdx*4+3] = src[srcIdx*2+1];
            }
        }
    }

    // Stage only the dirty sub-region (avoids uploading the full texture for small updates).
    VkDevice device = context_.GetDevice();
    VkDeviceSize subSize = static_cast<VkDeviceSize>(width) * height * 4;

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {xoffset, yoffset, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    if (frameStarted_)
    {
        // Deferred path: stage into the per-frame ring, flush transfers in EndFrame
        // (after vkCmdEndRenderPass, where transfer commands are valid). The updated
        // texture takes effect the same frame (transfers happen before submit).
        rVulkanStagingPool::Allocation stg;
        if (!stagingPool_.Acquire(currentFrame_, subSize, stg) || !stg.mapped) return;
        for (int row = 0; row < height; ++row)
            memcpy(static_cast<char*>(stg.mapped) + row * width * 4,
                   &tex.cpuData[((yoffset + row) * texW + xoffset) * 4],
                   static_cast<size_t>(width) * 4);
        region.bufferOffset = stg.offset;
        pendingTexUploads_.push_back({stg.buffer, stg.offset, tex.image, region, tex.currentLayout});
    }
    else
    {
        // Blocking path (initialization / outside frame): one-shot command buffer + vkQueueWaitIdle.
        std::vector<uint8_t> subData(subSize);
        for (int row = 0; row < height; ++row)
            memcpy(&subData[row * width * 4],
                   &tex.cpuData[((yoffset + row) * texW + xoffset) * 4],
                   static_cast<size_t>(width) * 4);

        rVulkanBuffer staging;
        if (!rVulkanBufferManager::CreateStagingBuffer(context_, subData.data(), subSize, staging))
            return;

        VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);
        TransitionImageLayout(cmd, tex.image,
            tex.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, 1,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        TransitionImageLayout(cmd, tex.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
        rVulkanBufferManager::DestroyBuffer(context_.GetAllocator(), staging);
    }
    tex.dirty = false;
    tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Note: mipmap regeneration after sub-image updates was removed because it
    // causes synchronization issues with font atlas textures (glyphs go missing).
    // Font atlases use GL_LINEAR with no mip filtering, so stale higher mip levels
    // are never sampled. For non-font textures that use TexSubImage2D (rare),
    // the caller should explicitly call GenerateMipmap if needed.
}
void vkRenderer::GenerateMipmap(int /*target*/)
{
    if (boundTexture2D_ == 0 || !context_.IsValid()) return;
    auto it = textures_.find(boundTexture2D_);
    if (it == textures_.end() || it->second.mipLevels <= 1) return;

    VkDevice device = context_.GetDevice();
    auto& tex = it->second;
    VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    // Transition mip 0 SHADER_READ_ONLY → TRANSFER_SRC
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Transition mip 1..N SHADER_READ_ONLY → TRANSFER_DST
    barrier.subresourceRange.baseMipLevel = 1;
    barrier.subresourceRange.levelCount = tex.mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    barrier.subresourceRange.levelCount = 1;

    int32_t mipW = tex.width, mipH = tex.height;
    for (uint32_t i = 1; i < tex.mipLevels; i++)
    {
        int32_t nextW = std::max(mipW / 2, 1), nextH = std::max(mipH / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i-1, 0, 1};
        blit.srcOffsets[1] = {mipW, mipH, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {nextW, nextH, 1};
        vkCmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &blit, VK_FILTER_LINEAR);

        // Transition mip i-1 TRANSFER_SRC → SHADER_READ_ONLY (except mip 0 which stays SRC for now)
        if (i > 1)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        // Transition mip i: last mip goes straight to SHADER_READ_ONLY (no further blit);
        // intermediate mips go to TRANSFER_SRC so the next iteration can blit from them.
        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        if (i == tex.mipLevels - 1)
        {
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        else
        {
            barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        mipW = nextW; mipH = nextH;
    }
    // Transition mip 0 TRANSFER_SRC → SHADER_READ_ONLY (last mip already handled in-loop)
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount   = 1;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
}
void vkRenderer::PixelStorei(int, int) {}
int vkRenderer::GetTexLevelParameteriv(int target, int /*level*/, int pname)
{
    // For proxy texture queries, always return success (Vulkan can handle any size up to max)
    if (target == rGLConst::ProxyTexture2D && pname == rGLConst::TextureWidth)
        return 1; // non-zero = size accepted
    return 0;
}

// ============================================================================
// System operations (stubs)
// ============================================================================

void vkRenderer::Finish() { /* no-op: Vulkan present is synchronized by renderFinishedSemaphore_ */ }
void vkRenderer::Flush() {}
void vkRenderer::ReadPixels(int x, int y, int width, int height,
                             int /*format*/, int /*type*/, void* data)
{
    if (!data || width <= 0 || height <= 0) return;

    VkDevice device = context_.GetDevice();
    // Ensure GPU is fully idle before reading back. EndFrame waited on the
    // in-flight fence, but the present engine may still be scanning out.
    vkDeviceWaitIdle(device);

    // Get the current swapchain image
    VkImage srcImage = swapchain_.GetImages()[currentImageIndex_];
    VkFormat srcFormat = swapchain_.GetFormat();

    // Create readback staging buffer (CPU-readable, persistently mapped)
    VkDeviceSize bufferSize = width * height * 4; // RGBA
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo vmaCI{};
    vmaCI.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo vmaInfo{};
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    if (vmaCreateBuffer(context_.GetAllocator(), &bufInfo, &vmaCI,
                        &stagingBuffer, &stagingAlloc, &vmaInfo) != VK_SUCCESS) return;

    // Record copy command
    VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

    // Transition swapchain image to TRANSFER_SRC
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = srcImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {x, y, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer, 1, &region);

    // Transition back
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);

    // Map and convert BGRA→RGB (Vulkan swapchain is typically BGRA, caller expects RGB)
    const uint8_t* src = static_cast<const uint8_t*>(vmaInfo.pMappedData);
    if (!src)
    {
        std::cerr << "[Vulkan] VMA persistent map failed for screenshot staging buffer\n";
        vmaDestroyBuffer(context_.GetAllocator(), stagingBuffer, stagingAlloc);
        return;
    }
    uint8_t* dst = static_cast<uint8_t*>(data);
    bool isBGR = (srcFormat == VK_FORMAT_B8G8R8A8_UNORM || srcFormat == VK_FORMAT_B8G8R8A8_SRGB);

    // OpenGL ReadPixels returns bottom-to-top, Vulkan image is top-to-bottom — flip Y
    for (int row = 0; row < height; row++)
    {
        const uint8_t* srcRow = src + (height - 1 - row) * width * 4;
        uint8_t* dstRow = dst + row * width * 3;
        for (int col = 0; col < width; col++)
        {
            if (isBGR) {
                dstRow[0] = srcRow[2]; // R
                dstRow[1] = srcRow[1]; // G
                dstRow[2] = srcRow[0]; // B
            } else {
                dstRow[0] = srcRow[0];
                dstRow[1] = srcRow[1];
                dstRow[2] = srcRow[2];
            }
            srcRow += 4;
            dstRow += 3;
        }
    }
    vmaDestroyBuffer(context_.GetAllocator(), stagingBuffer, stagingAlloc);
}
const char* vkRenderer::GetRendererString(int name)
{
    if (name == rGLConst::Renderer) return context_.GetDeviceName();
    if (name == rGLConst::Vendor)
    {
        // Build a descriptive vendor string from the Vulkan device properties
        static char vendor[128];
        uint32_t vendorId = context_.GetDeviceProperties().vendorID;
        const char* vendorName =
            vendorId == 0x1002 ? "AMD" :
            vendorId == 0x10DE ? "NVIDIA" :
            vendorId == 0x8086 ? "Intel" :
            vendorId == 0x106B ? "Apple" : "Vulkan";
        snprintf(vendor, sizeof(vendor), "%s (Vulkan)", vendorName);
        return vendor;
    }
    if (name == rGLConst::Version)
    {
        static char version[128];
        const auto& props = context_.GetDeviceProperties();
        uint32_t api = props.apiVersion;
        uint32_t drv = props.driverVersion;
        snprintf(version, sizeof(version), "Vulkan %u.%u.%u (driver %u.%u.%u)",
                 VK_VERSION_MAJOR(api), VK_VERSION_MINOR(api), VK_VERSION_PATCH(api),
                 VK_VERSION_MAJOR(drv), VK_VERSION_MINOR(drv), VK_VERSION_PATCH(drv));
        return version;
    }
    return "";
}

// ============================================================================
// Per-viewport FBOs (split-screen depth isolation)
// ============================================================================

bool vkRenderer::CreateViewportFBO(int frame, int index, int w, int h)
{
    if (index < 0 || index >= MAX_VIEWPORT_FBOS) return false;
    if (frame < 0 || frame >= MAX_FRAMES_IN_FLIGHT) return false;
    ViewportFBO& vfbo = viewportFBOs_[frame][index];

    // Skip if already correct size
    if (vfbo.framebuffer != VK_NULL_HANDLE && vfbo.width == w && vfbo.height == h)
        return true;

    VkDevice device = context_.GetDevice();

    // Invalidate descriptor cache BEFORE destroying views (views are the cache key)
    if (vfbo.colorView) descriptorManager_.InvalidateCache(vfbo.colorView);
    if (vfbo.depthView) descriptorManager_.InvalidateCache(vfbo.depthView);
    if (vfbo.colorTexId) { textures_.erase(vfbo.colorTexId); vfbo.colorTexId = 0; }
    if (vfbo.depthTexId) { textures_.erase(vfbo.depthTexId); vfbo.depthTexId = 0; }

    // Destroy old resources if resizing
    if (vfbo.framebuffer) { vkDestroyFramebuffer(device, vfbo.framebuffer, nullptr); vfbo.framebuffer = VK_NULL_HANDLE; }
    if (vfbo.colorView)  { vkDestroyImageView(device, vfbo.colorView, nullptr);  vfbo.colorView = VK_NULL_HANDLE; }
    if (vfbo.colorImage) { vmaDestroyImage(context_.GetAllocator(), vfbo.colorImage, vfbo.colorAlloc); vfbo.colorImage = VK_NULL_HANDLE; vfbo.colorAlloc = VK_NULL_HANDLE; }
    if (vfbo.emissiveView)  { vkDestroyImageView(device, vfbo.emissiveView, nullptr);  vfbo.emissiveView = VK_NULL_HANDLE; }
    if (vfbo.emissiveImage) { vmaDestroyImage(context_.GetAllocator(), vfbo.emissiveImage, vfbo.emissiveAlloc); vfbo.emissiveImage = VK_NULL_HANDLE; vfbo.emissiveAlloc = VK_NULL_HANDLE; }
    if (vfbo.depthView)  { vkDestroyImageView(device, vfbo.depthView, nullptr);  vfbo.depthView = VK_NULL_HANDLE; }
    if (vfbo.depthImage) { vmaDestroyImage(context_.GetAllocator(), vfbo.depthImage, vfbo.depthAlloc); vfbo.depthImage = VK_NULL_HANDLE; vfbo.depthAlloc = VK_NULL_HANDLE; }
    if (vfbo.sampler)      { vkDestroySampler(device, vfbo.sampler, nullptr);      vfbo.sampler = VK_NULL_HANDLE; }
    if (vfbo.depthSampler) { vkDestroySampler(device, vfbo.depthSampler, nullptr); vfbo.depthSampler = VK_NULL_HANDLE; }

    vfbo.width = w;
    vfbo.height = h;

    // --- Color image (COLOR_ATTACHMENT + SAMPLED) ---
    VkImageCreateInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.format = swapchain_.GetFormat();
    colorInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VmaAllocationCreateInfo vmaCI{}; vmaCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(context_.GetAllocator(), &colorInfo, &vmaCI,
                           &vfbo.colorImage, &vfbo.colorAlloc, nullptr) != VK_SUCCESS) return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = vfbo.colorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchain_.GetFormat();
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &vfbo.colorView) != VK_SUCCESS) return false;

    // --- Emissive image (dummy — matches PP offscreen 3-attachment layout) ---
    {
        VkImageCreateInfo emInfo = colorInfo;  // same format/size as color
        VmaAllocationCreateInfo vmaCI{}; vmaCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(context_.GetAllocator(), &emInfo, &vmaCI,
                           &vfbo.emissiveImage, &vfbo.emissiveAlloc, nullptr) != VK_SUCCESS) return false;
        VkImageViewCreateInfo emViewInfo = viewInfo;
        emViewInfo.image = vfbo.emissiveImage;
        if (vkCreateImageView(device, &emViewInfo, nullptr, &vfbo.emissiveView) != VK_SUCCESS) return false;
    }

    // --- Depth image (DEPTH_STENCIL_ATTACHMENT + SAMPLED — SAMPLED prevents MoltenVK memoryless) ---
    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = framebuffer_.GetDepthFormat();
    depthInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    {
        VmaAllocationCreateInfo vmaCI{}; vmaCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(context_.GetAllocator(), &depthInfo, &vmaCI,
                           &vfbo.depthImage, &vfbo.depthAlloc, nullptr) != VK_SUCCESS) return false;
    }

    viewInfo.image = vfbo.depthImage;
    viewInfo.format = framebuffer_.GetDepthFormat();
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &vfbo.depthView) != VK_SUCCESS) return false;

    // --- Sampler for composite pass ---
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &vfbo.sampler) != VK_SUCCESS) return false;

    if (viewportRenderPass_ == VK_NULL_HANDLE)
    {
        // 2-attachment render pass (color + depth). Uses the plain shader variant
        // (not emissive) which correctly renders fonts and HUD text.
        VkAttachmentDescription attachments[2]{};
        // Color: clear, store, transition to SHADER_READ_ONLY for composite sampling
        attachments[0].format = swapchain_.GetFormat();
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Depth: clear, store, READ_ONLY for composite depth binding
        attachments[1].format = framebuffer_.GetDepthFormat();
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        // Dependency: rendering → shader read (for composite pass)
        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &viewportRenderPass_) != VK_SUCCESS) return false;
    }

    // --- Framebuffer (2 attachments: color, depth) ---
    VkImageView views[2] = {vfbo.colorView, vfbo.depthView};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = viewportRenderPass_;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = views;
    fbInfo.width = w;
    fbInfo.height = h;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &vfbo.framebuffer) != VK_SUCCESS) return false;

    // --- Register color texture for descriptor set binding ---
    // NOTE: image/memory are NOT stored here — they're owned by the ViewportFBO
    // struct. The texture entry only holds view+sampler for descriptor binding.
    // This prevents double-destroy at shutdown (destructor iterates textures_,
    // DestroyViewportFBOs destroys the ViewportFBO resources separately).
    unsigned int texId = nextTextureId_++;
    VkTextureInfo texInfo{};
    // texInfo.image/memory left as VK_NULL_HANDLE — ViewportFBO owns them
    texInfo.view = vfbo.colorView;
    texInfo.sampler = vfbo.sampler;
    texInfo.width = w;
    texInfo.height = h;
    texInfo.mipLevels = 1;
    texInfo.minFilter = VK_FILTER_LINEAR;
    texInfo.magFilter = VK_FILTER_LINEAR;
    texInfo.wrapS = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texInfo.wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    textures_[texId] = std::move(texInfo);
    vfbo.colorTexId = texId;

    // --- Depth sampler (separate from color to avoid double-destroy on cleanup) ---
    {
        VkSamplerCreateInfo dsi{};
        dsi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        dsi.magFilter = VK_FILTER_NEAREST;
        dsi.minFilter = VK_FILTER_NEAREST;
        dsi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dsi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dsi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &dsi, nullptr, &vfbo.depthSampler) != VK_SUCCESS) return false;
    }

    // --- Register depth texture (forces Metal to preserve full D32_SFLOAT) ---
    // image/memory NOT stored — owned by ViewportFBO (same pattern as color above)
    unsigned int depthTexId = nextTextureId_++;
    VkTextureInfo depthTexInfo{};
    // depthTexInfo.image/memory left as VK_NULL_HANDLE — ViewportFBO owns them
    depthTexInfo.view = vfbo.depthView;
    depthTexInfo.sampler = vfbo.depthSampler;
    depthTexInfo.width = w;
    depthTexInfo.height = h;
    depthTexInfo.mipLevels = 1;
    depthTexInfo.minFilter = VK_FILTER_NEAREST;
    depthTexInfo.magFilter = VK_FILTER_NEAREST;
    depthTexInfo.wrapS = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthTexInfo.wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthTexInfo.descriptorLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    textures_[depthTexId] = std::move(depthTexInfo);
    vfbo.depthTexId = depthTexId;

    // Debug: uncomment to trace FBO creation
    // std::cerr << "[Vulkan] Created viewport FBO " << index << " (" << w << "x" << h << ")\n";
    return true;
}

void vkRenderer::DestroyViewportFBOs()
{
    VkDevice device = context_.GetDevice();
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
    {
        for (int i = 0; i < MAX_VIEWPORT_FBOS; i++)
        {
            ViewportFBO& vfbo = viewportFBOs_[f][i];
            // Invalidate descriptor cache BEFORE destroying image views.
            // (FlushAllDeferred was called earlier in RecreateSwapchain so
            //  the cache is already empty; these calls are a safety net for
            //  any path that destroys FBOs without a prior FlushAllDeferred.)
            if (vfbo.colorTexId && vfbo.colorView)
                descriptorManager_.InvalidateCache(vfbo.colorView);
            if (vfbo.depthTexId && vfbo.depthView)
                descriptorManager_.InvalidateCache(vfbo.depthView);

            if (vfbo.framebuffer)   vkDestroyFramebuffer(device, vfbo.framebuffer, nullptr);
            if (vfbo.colorView)     vkDestroyImageView(device, vfbo.colorView, nullptr);
            if (vfbo.colorImage)    vmaDestroyImage(context_.GetAllocator(), vfbo.colorImage, vfbo.colorAlloc);
            if (vfbo.emissiveView)  vkDestroyImageView(device, vfbo.emissiveView, nullptr);
            if (vfbo.emissiveImage) vmaDestroyImage(context_.GetAllocator(), vfbo.emissiveImage, vfbo.emissiveAlloc);
            if (vfbo.depthView)     vkDestroyImageView(device, vfbo.depthView, nullptr);
            if (vfbo.depthImage)    vmaDestroyImage(context_.GetAllocator(), vfbo.depthImage, vfbo.depthAlloc);
            if (vfbo.sampler)      vkDestroySampler(device, vfbo.sampler, nullptr);
            if (vfbo.depthSampler) vkDestroySampler(device, vfbo.depthSampler, nullptr);
            if (vfbo.colorTexId) textures_.erase(vfbo.colorTexId);
            if (vfbo.depthTexId) textures_.erase(vfbo.depthTexId);
            vfbo = ViewportFBO{};
        }
    }
    // Only destroy the render pass if we own it (not borrowed from PP offscreen)
    if (viewportRenderPass_ && (!postProcess_.IsEnabled() || viewportRenderPass_ != postProcess_.GetSceneRenderPass()))
    {
        vkDestroyRenderPass(device, viewportRenderPass_, nullptr);
    }
    viewportRenderPass_ = VK_NULL_HANDLE;
    viewportFBOCount_ = 0;
}

// =============================================================================
// Shadow Map FBOs (FR13)
// =============================================================================

bool vkRenderer::CreateShadowMaps()
{
    if (shadowMapsCreated_) return true;

    VkDevice device = context_.GetDevice();
    const int size = SHADOW_MAP_SIZE;
    VkFormat depthFormat = framebuffer_.GetDepthFormat();

    // --- Depth-only render pass (no color attachment) ---
    if (shadowRenderPass_ == VK_NULL_HANDLE)
    {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pColorAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        // Dependency: depth write → fragment shader read (main pass samples shadow map)
        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &shadowRenderPass_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create shadow render pass" << std::endl;
            return false;
        }
    }

    // --- Shadow pipeline layout (push constants only, no descriptor sets needed) ---
    if (shadowPipelineLayout_ == VK_NULL_HANDLE)
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = 64; // single mat4 lightVP

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create shadow pipeline layout" << std::endl;
            return false;
        }
    }

    // --- Shadow pipeline (depth-only, vertex shader only) ---
    if (shadowPipeline_ == VK_NULL_HANDLE)
    {
        // Load shadow shaders: try runtime compilation first, fall back to pre-compiled SPIR-V
        if (shadowVertShader_ == VK_NULL_HANDLE)
        {
#ifdef HAVE_SHADERC_SHADERC_HPP
            {
                tString vertPath = tDirectories::Data().GetReadPath("shaders/shadow.vert");
                if (vertPath.Len() > 1)
                {
                    std::string err;
                    shadowVertShader_ = rVulkanShader::CompileFromFile(
                        device, static_cast<const char*>(vertPath),
                        rVulkanShader::Stage::Vertex, {}, &err);
                    if (shadowVertShader_ == VK_NULL_HANDLE)
                        std::cerr << "[Vulkan] Shadow vert compile failed: " << err << "\n";
                }
            }
#endif
            if (shadowVertShader_ == VK_NULL_HANDLE)
                shadowVertShader_ = rVulkanShader::LoadFromFile(device, "shaders/shadow.vert.spv");
            if (shadowVertShader_ == VK_NULL_HANDLE)
            {
                std::cerr << "[Vulkan] Failed to load shadow vertex shader" << std::endl;
                return false;
            }
        }

        // Minimal fragment shader for MoltenVK compatibility (some Metal drivers
        // require a fragment shader even for depth-only passes)
        if (shadowFragShader_ == VK_NULL_HANDLE)
        {
#ifdef HAVE_SHADERC_SHADERC_HPP
            {
                tString fragPath = tDirectories::Data().GetReadPath("shaders/shadow.frag");
                if (fragPath.Len() > 1)
                {
                    std::string err;
                    shadowFragShader_ = rVulkanShader::CompileFromFile(
                        device, static_cast<const char*>(fragPath),
                        rVulkanShader::Stage::Fragment, {}, &err);
                    if (shadowFragShader_ == VK_NULL_HANDLE)
                        std::cerr << "[Vulkan] Shadow frag compile failed: " << err << "\n";
                }
            }
#endif
            if (shadowFragShader_ == VK_NULL_HANDLE)
                shadowFragShader_ = rVulkanShader::LoadFromFile(device, "shaders/shadow.frag.spv");
            // Fragment shader is optional — some drivers work without it
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        uint32_t stageCount = 1;
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = shadowVertShader_;
        stages[0].pName = "main";
        if (shadowFragShader_)
        {
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = shadowFragShader_;
            stages[1].pName = "main";
            stageCount = 2;
        }

        // Vertex input: position only (vec3 at offset 0 from rVertex20)
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(rVertex20);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attr{};
        attr.binding = 0;
        attr.location = 0;
        attr.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr.offset = 0;
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attr;

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
        rasterizer.cullMode = VK_CULL_MODE_NONE;  // shadow casters need both faces (thin walls)
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        // Depth bias to reduce shadow acne
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.5f;
        rasterizer.depthBiasSlopeFactor = 1.75f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        // No color blend state (no color attachments)
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 0;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pipeInfo{};
        pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeInfo.stageCount = stageCount;
        pipeInfo.pStages = stages;
        pipeInfo.pVertexInputState = &vertexInput;
        pipeInfo.pInputAssemblyState = &inputAssembly;
        pipeInfo.pViewportState = &viewportState;
        pipeInfo.pRasterizationState = &rasterizer;
        pipeInfo.pMultisampleState = &multisample;
        pipeInfo.pDepthStencilState = &depthStencil;
        pipeInfo.pColorBlendState = &colorBlend;
        pipeInfo.pDynamicState = &dynState;
        pipeInfo.layout = shadowPipelineLayout_;
        pipeInfo.renderPass = shadowRenderPass_;
        pipeInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr,
                                      &shadowPipeline_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create shadow pipeline" << std::endl;
            return false;
        }
    }

    // --- Create shadow map depth textures ---
    for (int i = 0; i < SHADOW_MAP_COUNT; i++)
    {
        ShadowMapFBO& sm = shadowMaps_[i];
        if (sm.framebuffer != VK_NULL_HANDLE) continue; // already created

        // Depth image
        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = depthFormat;
        imgInfo.extent = {static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        {
            VmaAllocationCreateInfo vmaCI{}; vmaCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if (vmaCreateImage(context_.GetAllocator(), &imgInfo, &vmaCI,
                               &sm.depthImage, &sm.depthAlloc, nullptr) != VK_SUCCESS) return false;
        }

        // Depth image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = sm.depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &viewInfo, nullptr, &sm.depthView) != VK_SUCCESS) return false;

        // Comparison sampler for PCF shadow sampling (sampler2DShadow)
        VkSamplerCreateInfo sampInfo{};
        sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampInfo.magFilter = VK_FILTER_LINEAR;
        sampInfo.minFilter = VK_FILTER_LINEAR;
        sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside = lit (no shadow)
        sampInfo.compareEnable = VK_TRUE;
        sampInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        sampInfo.maxLod = 0.0f;
        if (vkCreateSampler(device, &sampInfo, nullptr, &sm.sampler) != VK_SUCCESS) return false;

        // Framebuffer (depth-only, single attachment)
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowRenderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &sm.depthView;
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &sm.framebuffer) != VK_SUCCESS) return false;

        // Register shadow depth texture for descriptor binding
        unsigned int texId = nextTextureId_++;
        VkTextureInfo texInfo{};
        texInfo.view = sm.depthView;
        texInfo.sampler = sm.sampler;
        texInfo.width = size;
        texInfo.height = size;
        texInfo.mipLevels = 1;
        texInfo.minFilter = VK_FILTER_LINEAR;
        texInfo.magFilter = VK_FILTER_LINEAR;
        texInfo.wrapS = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        texInfo.wrapT = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        texInfo.descriptorLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        textures_[texId] = std::move(texInfo);
        sm.texId = texId;
    }

    // Run an empty shadow render pass on each map to transition from UNDEFINED →
    // DEPTH_STENCIL_READ_ONLY_OPTIMAL. The render pass loadOp=CLEAR clears to 1.0
    // and finalLayout transitions the image — no manual barriers needed.
    {
        VkCommandBuffer transCmd;
        VkCommandBufferAllocateInfo allocCmdInfo{};
        allocCmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocCmdInfo.commandPool = commandPool_;
        allocCmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocCmdInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &allocCmdInfo, &transCmd) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] vkAllocateCommandBuffers failed for shadow map initialization\n";
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(transCmd, &beginInfo);

        for (int i = 0; i < SHADOW_MAP_COUNT; i++)
        {
            VkClearValue clearValue{};
            clearValue.depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = shadowRenderPass_;
            rpBegin.framebuffer = shadowMaps_[i].framebuffer;
            rpBegin.renderArea = {{0, 0}, {static_cast<uint32_t>(size), static_cast<uint32_t>(size)}};
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;
            vkCmdBeginRenderPass(transCmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(transCmd);
        }

        vkEndCommandBuffer(transCmd);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &transCmd;
        vkQueueSubmit(context_.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(context_.GetGraphicsQueue());
        vkFreeCommandBuffers(device, commandPool_, 1, &transCmd);
    }

    // Update lighting descriptor sets to point to real shadow map textures
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorImageInfo shadowImgInfos[2]{};
        VkWriteDescriptorSet writes[2]{};
        for (int s = 0; s < SHADOW_MAP_COUNT; s++)
        {
            shadowImgInfos[s].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            shadowImgInfos[s].imageView   = shadowMaps_[s].depthView;
            shadowImgInfos[s].sampler     = shadowMaps_[s].sampler;

            writes[s].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[s].dstSet          = lightingDescSet_[i];
            writes[s].dstBinding      = static_cast<uint32_t>(1 + s);
            writes[s].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[s].descriptorCount = 1;
            writes[s].pImageInfo      = &shadowImgInfos[s];
        }
        vkUpdateDescriptorSets(device, SHADOW_MAP_COUNT, writes, 0, nullptr);
    }

    shadowMapsCreated_ = true;
    VK_LOG_INFO("[Vulkan] Created " << SHADOW_MAP_COUNT << " shadow maps (" << size << "x" << size << ")" << std::endl);
    return true;
}

void vkRenderer::DestroyShadowMaps()
{
    VkDevice device = context_.GetDevice();

    for (int i = 0; i < SHADOW_MAP_COUNT; i++)
    {
        ShadowMapFBO& sm = shadowMaps_[i];
        if (sm.texId) {
            if (sm.depthView) descriptorManager_.InvalidateCache(sm.depthView);
            textures_.erase(sm.texId);
            sm.texId = 0;
        }
        if (sm.framebuffer) { vkDestroyFramebuffer(device, sm.framebuffer, nullptr); sm.framebuffer = VK_NULL_HANDLE; }
        if (sm.depthView)   { vkDestroyImageView(device, sm.depthView, nullptr);     sm.depthView = VK_NULL_HANDLE; }
        if (sm.depthImage)  { vmaDestroyImage(context_.GetAllocator(), sm.depthImage, sm.depthAlloc); sm.depthImage = VK_NULL_HANDLE; sm.depthAlloc = VK_NULL_HANDLE; }
        if (sm.sampler)     { vkDestroySampler(device, sm.sampler, nullptr);          sm.sampler = VK_NULL_HANDLE; }
    }

    if (shadowPipeline_)       { vkDestroyPipeline(device, shadowPipeline_, nullptr);             shadowPipeline_ = VK_NULL_HANDLE; }
    if (shadowPipelineLayout_) { vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr);  shadowPipelineLayout_ = VK_NULL_HANDLE; }
    if (shadowRenderPass_)     { vkDestroyRenderPass(device, shadowRenderPass_, nullptr);          shadowRenderPass_ = VK_NULL_HANDLE; }
    rVulkanShader::Destroy(device, shadowVertShader_);
    rVulkanShader::Destroy(device, shadowFragShader_);

    shadowMapsCreated_ = false;
}

// =============================================================================
// Wall GPU Compute Pipeline (Sprint 3.1)
// =============================================================================

bool vkRenderer::CreateWallComputePipeline()
{
    VkDevice device     = context_.GetDevice();
    VmaAllocator alloc  = context_.GetAllocator();

    // --- Per-frame SSBOs: host-visible, persistently mapped ---
    {
        VkBufferCreateInfo bufCI{};
        bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCI.size        = kMaxWallSegments * sizeof(WallSegmentGPU);
        bufCI.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaCI{};
        vmaCI.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VmaAllocationInfo info{};
            if (vmaCreateBuffer(alloc, &bufCI, &vmaCI,
                                &wallSSBO_[i], &wallSSBOAlloc_[i], &info) != VK_SUCCESS)
            {
                std::cerr << "[Vulkan] CreateWallComputePipeline: SSBO alloc failed\n";
                return false;
            }
            wallSSBOMapped_[i] = info.pMappedData;
            if (!wallSSBOMapped_[i])
            {
                std::cerr << "[Vulkan] CreateWallComputePipeline: SSBO persistent map failed\n";
                return false;
            }
        }
    }

    // --- Per-frame device-local output VBOs: compute writes, vertex input reads ---
    {
        // 6 rVertex20 verts per segment, 5 uint32s per vert
        VkBufferCreateInfo bufCI{};
        bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCI.size        = kMaxWallSegments * 6u * 5u * sizeof(uint32_t);
        bufCI.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaCI{};
        vmaCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vmaCreateBuffer(alloc, &bufCI, &vmaCI,
                                &wallOutputVBO_[i], &wallOutputVBOAlloc_[i], nullptr) != VK_SUCCESS)
            {
                std::cerr << "[Vulkan] CreateWallComputePipeline: output VBO alloc failed\n";
                return false;
            }
        }
    }

    // --- Descriptor set layout: binding 0 = input SSBO, binding 1 = output VBO ---
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutCI{};
        layoutCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCI.bindingCount = 2;
        layoutCI.pBindings    = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &wallComputeSetLayout_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] CreateWallComputePipeline: descriptor set layout failed\n";
            return false;
        }
    }

    // --- Descriptor pool ---
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;  // 2 storage bindings per set

        VkDescriptorPoolCreateInfo poolCI{};
        poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.maxSets       = MAX_FRAMES_IN_FLIGHT;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes    = &poolSize;
        if (vkCreateDescriptorPool(device, &poolCI, nullptr, &wallComputePool_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] CreateWallComputePipeline: descriptor pool failed\n";
            return false;
        }
    }

    // --- Allocate and update descriptor sets (per frame) ---
    {
        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts[i] = wallComputeSetLayout_;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = wallComputePool_;
        allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        allocInfo.pSetLayouts        = layouts;
        if (vkAllocateDescriptorSets(device, &allocInfo, wallComputeSets_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] CreateWallComputePipeline: descriptor set alloc failed\n";
            return false;
        }

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo ssboInfo{};
            ssboInfo.buffer = wallSSBO_[i];
            ssboInfo.offset = 0;
            ssboInfo.range  = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo vboInfo{};
            vboInfo.buffer = wallOutputVBO_[i];
            vboInfo.offset = 0;
            vboInfo.range  = VK_WHOLE_SIZE;

            VkWriteDescriptorSet writes[2]{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = wallComputeSets_[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo     = &ssboInfo;
            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = wallComputeSets_[i];
            writes[1].dstBinding      = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo     = &vboInfo;
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
    }

    // --- Pipeline layout: single set + push constants (WallComputePC) ---
    {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(WallComputePC);

        VkPipelineLayoutCreateInfo layoutCI{};
        layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCI.setLayoutCount         = 1;
        layoutCI.pSetLayouts            = &wallComputeSetLayout_;
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges    = &pcRange;
        if (vkCreatePipelineLayout(device, &layoutCI, nullptr, &wallComputePipeLayout_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] CreateWallComputePipeline: pipeline layout failed\n";
            return false;
        }
    }

    // --- Load/compile the compute shader ---
    {
#ifdef HAVE_SHADERC_SHADERC_HPP
        std::vector<std::string> includePaths;
        tString compPath = tDirectories::Data().GetReadPath("shaders/wall_gen.comp");
        if (compPath.Len() > 1)
        {
            std::string p = static_cast<const char*>(compPath);
            auto slash = p.find_last_of('/');
            if (slash != std::string::npos)
                includePaths.push_back(p.substr(0, slash));
            std::string err;
            wallComputeShader_ = rVulkanShader::CompileFromFile(
                device, p, rVulkanShader::Stage::Compute, includePaths, &err);
            if (wallComputeShader_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] wall_gen.comp compile failed:\n" << err << "\n";
        }
        else
        {
            std::cerr << "[Vulkan] wall_gen.comp not found in data dirs — wall compute disabled\n";
        }
#else
        // Android (no shaderc): load pre-compiled SPIR-V from APK assets
        wallComputeShader_ = rVulkanShader::LoadFromFile(device, "shaders/wall_gen.comp.spv");
        if (wallComputeShader_ == VK_NULL_HANDLE)
            SDL_Log("[Vulkan] Failed to load shaders/wall_gen.comp.spv");
#endif
        if (wallComputeShader_ == VK_NULL_HANDLE)
        {
            // Non-fatal: wall compute disabled, CPU path remains active
            DestroyWallComputePipeline();
            return true;
        }
    }

    // --- Create the compute pipeline ---
    {
        VkPipelineShaderStageCreateInfo stageCI{};
        stageCI.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageCI.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stageCI.module = wallComputeShader_;
        stageCI.pName  = "main";

        VkComputePipelineCreateInfo pipeCI{};
        pipeCI.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeCI.stage  = stageCI;
        pipeCI.layout = wallComputePipeLayout_;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &wallComputePipe_) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] CreateWallComputePipeline: vkCreateComputePipelines failed\n";
            return false;
        }
    }

    wallComputeReady_ = true;
    VK_LOG_INFO("[Vulkan] Wall compute pipeline ready (" << kMaxWallSegments << " max segments)\n");
    return true;
}

void vkRenderer::DestroyWallComputePipeline()
{
    wallComputeReady_ = false;
    VkDevice     device = context_.GetDevice();
    VmaAllocator alloc  = context_.GetAllocator();

    VK_DESTROY(vkDestroyPipeline,       device, wallComputePipe_);
    VK_DESTROY(vkDestroyPipelineLayout, device, wallComputePipeLayout_);
    rVulkanShader::Destroy(device, wallComputeShader_);
    wallComputeShader_ = VK_NULL_HANDLE;
    VK_DESTROY(vkDestroyDescriptorPool,      device, wallComputePool_);
    VK_DESTROY(vkDestroyDescriptorSetLayout, device, wallComputeSetLayout_);
    memset(wallComputeSets_, 0, sizeof(wallComputeSets_));

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (wallOutputVBO_[i])
        {
            vmaDestroyBuffer(alloc, wallOutputVBO_[i], wallOutputVBOAlloc_[i]);
            wallOutputVBO_[i]      = VK_NULL_HANDLE;
            wallOutputVBOAlloc_[i] = VK_NULL_HANDLE;
        }
        wallSSBOMapped_[i] = nullptr;
        if (wallSSBO_[i])
        {
            vmaDestroyBuffer(alloc, wallSSBO_[i], wallSSBOAlloc_[i]);
            wallSSBO_[i]      = VK_NULL_HANDLE;
            wallSSBOAlloc_[i] = VK_NULL_HANDLE;
        }
    }
}

void vkRenderer::DispatchWallCompute(VkCommandBuffer cmd)
{
    if (!wallComputeReady_) return;

    const auto& segs = sg_wallSegsPrev_;
    uint32_t segCount = static_cast<uint32_t>(
        std::min(segs.size(), static_cast<size_t>(kMaxWallSegments)));

    if (segCount == 0)
    {
        wallUMinPrev_   = 0.0f;
        wallURangePrev_ = 1.0f;
        return;
    }

    // Snapshot UV normalization params for this batch
    float uMin   = sg_wallUMin_;
    float uMax   = sg_wallUMax_;
    float uRange = std::max(uMax - uMin, 1.0e-6f);
    wallUMinPrev_   = uMin;
    wallURangePrev_ = uRange;

    // Upload segment descriptors to SSBO
    memcpy(wallSSBOMapped_[currentFrame_], segs.data(), segCount * sizeof(WallSegmentGPU));

    // Barrier: HOST_WRITE → COMPUTE_SHADER_READ on SSBO
    VkBufferMemoryBarrier hostToComp{};
    hostToComp.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostToComp.srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
    hostToComp.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    hostToComp.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostToComp.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostToComp.buffer              = wallSSBO_[currentFrame_];
    hostToComp.offset              = 0;
    hostToComp.size                = segCount * sizeof(WallSegmentGPU);
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 1, &hostToComp, 0, nullptr);

    // Dispatch compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wallComputePipe_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            wallComputePipeLayout_, 0, 1, &wallComputeSets_[currentFrame_],
                            0, nullptr);

    WallComputePC pc{};
    pc.u_min     = uMin;
    pc.u_range   = uRange;
    pc.seg_count = segCount;
    pc._pad      = 0;
    vkCmdPushConstants(cmd, wallComputePipeLayout_,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WallComputePC), &pc);

    uint32_t groupCount = (segCount + 63u) / 64u;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Barrier: COMPUTE_SHADER_WRITE → VERTEX_INPUT_READ on output VBO
    VkBufferMemoryBarrier compToVert{};
    compToVert.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    compToVert.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    compToVert.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    compToVert.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    compToVert.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    compToVert.buffer              = wallOutputVBO_[currentFrame_];
    compToVert.offset              = 0;
    compToVert.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, nullptr, 1, &compToVert, 0, nullptr);
}

uint32_t vkRenderer::GetWallComputeCurrentCount() const
{
    return static_cast<uint32_t>(sg_wallSegsCurrent_.size());
}

void vkRenderer::DrawComputedWallsRange(unsigned int textureId, uint32_t startSeg, uint32_t segCount)
{
    if (!wallComputeReady_ || segCount == 0) return;
    if (!frameStarted_) [[unlikely]] return;

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // Texture matrix: remap normalized [0,1] UV → raw UV space
    // Column-major 4x4: index = col*4 + row
    //   [0]  = U scale (col 0, row 0)
    //   [5]  = V scale (col 1, row 1) — identity, V is already in [0,1]
    //   [12] = U translate (col 3, row 0)
    //   [15] = W component = 1 (col 3, row 3)
    // Indices [8],[9],[10],[11] are overwritten by BuildPushConstants (time, ctx, alphatest, lit)
    float texMat[16] = {};
    texMat[0]  = wallURangePrev_;
    texMat[5]  = 1.0f;
    texMat[12] = wallUMinPrev_;
    texMat[15] = 1.0f;

    // Build render state key matching the CPU wall path
    rRenderStateKey state = textureId
        ? rRenderStateKey::Textured(static_cast<uint64_t>(textureId), rBlendMode::Alpha)
        : rRenderStateKey::Colored(rBlendMode::Alpha);
    state.SetTexMatrix(texMat);
    state.SetRenderContext(s_renderContextId);

    // Build push constants (reads MVP from current matrix stacks)
    VkPushConstants pc{};
    BuildPushConstants(pc, state, false);

    // Look up texture descriptor
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    // Get graphics pipeline matching the wall render state
    rVulkanPipelineKey pipeKey{};
    pipeKey.blendMode      = static_cast<uint8_t>(state.blendMode);
    pipeKey.depthTest      = (state.flags & rRenderStateKey::DepthTest)  != 0;
    pipeKey.depthWrite     = (state.flags & rRenderStateKey::DepthWrite) != 0;
    pipeKey.cullFace       = cullFaceEnabled_;
    pipeKey.frontFaceCW    = frontFaceCW_;
    pipeKey.useLines       = false;
    pipeKey.colorWriteMask = 0xF;

    VkPipeline pipeline = pipelineManager_.GetPipeline(pipeKey);
    if (pipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineManager_.GetLayout(), 0, 1, &descSet, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineManager_.GetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &wallOutputVBO_[currentFrame_], &offset);

    vkCmdDraw(cmd, segCount * 6u, 1, startSeg * 6u, 0);

    // Invalidate render queue binding cache so the next draw rebinds its own pipeline
    vulkanQueue_.InvalidateBindingCache();
}

void vkRenderer::ComputeShadowVPMatrices()
{
    // Arena bounds
    float minX = s_arenaBoundsLow[0];
    float minY = s_arenaBoundsLow[1];
    float maxX = s_arenaBoundsHigh[0];
    float maxY = s_arenaBoundsHigh[1];

    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    float halfW = (maxX - minX) * 0.5f;
    float halfH = (maxY - minY) * 0.5f;
    float arenaSize = std::max(halfW, halfH);
    // Ortho frustum: 1.8x arena size to avoid frustum edge clipping on floor.
    // Larger = no edge artifacts but lower shadow resolution per texel.
    float halfSize = arenaSize * 1.8f;

    // Position shadow lights just outside the arena, high up.
    // The height determines shadow length: higher = shorter shadows.
    // At height = arenaSize, rim wall shadows (height ~5-10 units) are ~5-10% of arena size.
    float lightHeight = arenaSize * 0.5f;
    float lightOffset = arenaSize * 0.3f; // slightly outside center

    // Light A: upper-right corner, high up (matches reddish light direction)
    glm::vec3 lightPosA(centerX + lightOffset, centerY + lightOffset * 0.75f, lightHeight);
    // Light B: lower-left corner, high up (matches bluish light direction)
    glm::vec3 lightPosB(centerX - lightOffset * 0.75f, centerY - lightOffset * 0.3f, lightHeight);
    const glm::vec3 lightPositions[2] = { lightPosA, lightPosB };

    for (int i = 0; i < 2; i++)
    {
        glm::vec3 center(centerX, centerY, 0.0f);
        glm::vec3 lightDir = glm::normalize(lightPositions[i] - center);
        // Place the ortho camera along the light direction, far enough to see everything
        glm::vec3 camPos = center + lightDir * halfSize * 2.0f;

        glm::vec3 up(0.0f, 0.0f, 1.0f);
        if (std::abs(glm::dot(lightDir, up)) > 0.99f)
            up = glm::vec3(0.0f, 1.0f, 0.0f);

        glm::mat4 view = glm::lookAt(camPos, center, up);
        glm::mat4 proj = glm::ortho(-halfSize, halfSize, -halfSize, halfSize,
                                     0.1f, halfSize * 4.0f);
        // Vulkan clip: Y inverted, depth [0,1] (GLM produces [-1,1])
        // Apply the same depth remap as uber.vert: z = (z + w) * 0.5
        // For ortho, w=1, so z_vulkan = (z_ndc + 1) * 0.5
        // GLM already provides glm::mat4 for this — use a manual correction:
        glm::mat4 clip(1.0f);
        clip[1][1] = -1.0f;    // flip Y
        clip[2][2] = 0.5f;     // remap depth from [-1,1] to [0,1]
        clip[3][2] = 0.5f;
        glm::mat4 vp = clip * proj * view;

        memcpy(shadowVP_[i], glm::value_ptr(vp), 64);
    }
}

void vkRenderer::RenderShadowPass(VkCommandBuffer cmd)
{
    if (!shadowMapsCreated_) return;

    ComputeShadowVPMatrices();

    const int size = SHADOW_MAP_SIZE;
    VkViewport viewport{};
    viewport.x = 0; viewport.y = 0;
    viewport.width = static_cast<float>(size);
    viewport.height = static_cast<float>(size);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(size), static_cast<uint32_t>(size)}};

    // Combine static (persistent rim walls) + dynamic (per-frame player walls) shadow geometry
    const auto& staticVerts = rRenderQueue::Instance().GetShadowStaticVertices();
    const auto& dynamicVerts = rRenderQueue::Instance().GetShadowDynamicVertices();
    size_t totalVerts = staticVerts.size() + dynamicVerts.size();
    if (totalVerts == 0) return;

    // Upload to per-frame staging buffer (reused, grown as needed)
    VkDeviceSize bufSize = totalVerts * sizeof(rVertex20);
    VkDevice device = context_.GetDevice();
    uint32_t frame = currentFrame_;

    if (shadowStagingBuf_[frame].size < bufSize)
    {
        // Destroy old (safe: fence for this frame slot was waited on in BeginFrame)
        rVulkanBufferManager::DestroyBuffer(context_.GetAllocator(), shadowStagingBuf_[frame]);

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType     = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size      = bufSize;
        bufInfo.usage     = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaAllocCI{};
        vmaAllocCI.usage  = VMA_MEMORY_USAGE_AUTO;
        vmaAllocCI.flags  = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vmaAllocInfo{};
        if (vmaCreateBuffer(context_.GetAllocator(), &bufInfo, &vmaAllocCI,
                            &shadowStagingBuf_[frame].buffer,
                            &shadowStagingBuf_[frame].allocation,
                            &vmaAllocInfo) != VK_SUCCESS) return;
        shadowStagingBuf_[frame].size   = bufSize;
        shadowStagingBuf_[frame].mapped = vmaAllocInfo.pMappedData;
    }

    // Copy vertex data (static first, then dynamic) — persistent mapping, no map/unmap needed
    void* mapped = shadowStagingBuf_[frame].mapped;
    if (!mapped) return;
    size_t staticBytes  = staticVerts.size()  * sizeof(rVertex20);
    size_t dynamicBytes = dynamicVerts.size() * sizeof(rVertex20);
    if (staticBytes  > 0) memcpy(mapped, staticVerts.data(), staticBytes);
    if (dynamicBytes > 0) memcpy(static_cast<char*>(mapped) + staticBytes, dynamicVerts.data(), dynamicBytes);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &shadowStagingBuf_[frame].buffer, &offset);

    for (int i = 0; i < SHADOW_MAP_COUNT; i++)
    {
        ShadowMapFBO& sm = shadowMaps_[i];

        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = shadowRenderPass_;
        rpBegin.framebuffer = sm.framebuffer;
        rpBegin.renderArea = {{0, 0}, {static_cast<uint32_t>(size), static_cast<uint32_t>(size)}};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push light VP matrix
        vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 64, shadowVP_[i]);

        // Draw all shadow-casting geometry (static + dynamic)
        vkCmdDraw(cmd, static_cast<uint32_t>(totalVerts), 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
}

void vkRenderer::BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h)
{
    if (index < 0 || index >= MAX_VIEWPORT_FBOS) return;
    if (totalViewports <= 1) return;  // single viewport: no FBO needed

    // Ensure the frame is started (BeginFrame creates the main render pass
    // that we need to end before starting the viewport FBO render pass).
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) [[unlikely]] return;

    // Create/resize FBO for the current frame slot.
    // Also eagerly pre-create the same FBO for every OTHER frame slot so that
    // later frames don't pay the creation cost on their first use — which
    // caused a visible flicker on the first round of multi-viewport mode.
    // Pre-creation is safe here: the fence wait in BeginFrame guarantees the
    // other slots' previous submissions are complete.
    if (!CreateViewportFBO(currentFrame_, index, w, h)) return;
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
        if (f != currentFrame_)
            CreateViewportFBO(f, index, w, h);  // best-effort; ignore failure
    viewportFBOCount_ = std::max(viewportFBOCount_, index + 1);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // End whatever render pass is active (main or previous viewport FBO)
    vkCmdEndRenderPass(cmd);

    // Begin viewport FBO render pass (clears color and depth)
    ViewportFBO& vfbo = viewportFBOs_[currentFrame_][index];
    VkClearValue clears[2]{};
    clears[0].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
    clears[1].depthStencil = {0.0f, 0};   // reverse-Z: clear to far

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = viewportRenderPass_;
    rpInfo.framebuffer = vfbo.framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vulkanQueue_.InvalidateBindingCache();  // new render pass — force pipeline/descriptor rebind

    // Pipeline manager needs to know about the render pass for pipeline cache
    pipelineManager_.SetRenderPass(viewportRenderPass_);

    // Set viewport/scissor to full FBO size (not screen sub-region).
    // Negative-height viewport (VK_KHR_maintenance1) — scene shaders no
    // longer apply a per-vertex Y-flip; the rasterizer does it via the
    // negative height. vp.y is the bottom of the FBO in framebuffer space.
    VkViewport vp{};
    vp.x = 0;
    vp.y = static_cast<float>(h);
    vp.width = static_cast<float>(w);
    vp.height = -static_cast<float>(h);
    vp.minDepth = 0; vp.maxDepth = 1;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Update cached viewport to FBO dimensions
    cachedViewport_[0] = 0;
    cachedViewport_[1] = 0;
    cachedViewport_[2] = w;
    cachedViewport_[3] = h;

    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);

    activeViewportFBO_ = index;
    sr_inViewportFBO = true;
}

void vkRenderer::EndViewportFBO()
{
    if (!frameStarted_ || activeViewportFBO_ < 0) return;
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // Reset texture matrix stack to identity before HUD flush. The 3D scene
    // (floor tiling, etc.) leaves non-identity values in the texture stack.
    // Cockpit widgets using Textured() state keys (without UseTexMatrix) read
    // from the stack, corrupting their UV mapping if not reset.
    TexMatrix();
    IdentityMatrix();
    ModelMatrix();

    // Clear depth before HUD flush — the 3D scene already wrote depth values,
    // and cockpit widgets use depth-tested state keys. Without clearing, cockpit
    // elements render behind the scene geometry.
    {
        VkClearAttachment depthClear{};
        depthClear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthClear.clearValue.depthStencil = {0.0f, 0};   // reverse-Z: clear to far
        VkClearRect rect{};
        rect.rect.extent = {static_cast<uint32_t>(viewportFBOs_[currentFrame_][activeViewportFBO_].width),
                            static_cast<uint32_t>(viewportFBOs_[currentFrame_][activeViewportFBO_].height)};
        rect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &depthClear, 1, &rect);
    }

    // Flush any pending HUD geometry (cycle names, cockpit widgets) into the
    // viewport FBO BEFORE ending its render pass.
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);

    // End viewport FBO render pass (triggers layout transition to SHADER_READ_ONLY)
    vkCmdEndRenderPass(cmd);

    // Resume main render pass so subsequent rendering (composite, overlays) works
    const bool ppActive = postProcess_.IsEnabled() && s_lastFrameWasInGame;
    VkRenderPass  mainRP = ppActive ? postProcess_.GetSceneRenderPass()
                                    : framebuffer_.GetRenderPass();
    VkFramebuffer mainFB = ppActive ? postProcess_.GetSceneFramebuffer()
                                    : framebuffer_.GetFramebuffer(currentImageIndex_);
    pipelineManager_.SetRenderPass(mainRP);

    // Always provide 3 clear values — safe for both 2 and 3-attachment render passes
    // (Vulkan ignores extra clear values beyond the attachment count).
    VkClearValue clears[3]{};
    clears[0].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
    clears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // emissive (ignored for 2-attachment RP)
    clears[2].depthStencil = {0.0f, 0};   // reverse-Z: clear to far

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = mainRP;
    rpInfo.framebuffer = mainFB;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchain_.GetExtent();
    rpInfo.clearValueCount = 3;
    rpInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vulkanQueue_.InvalidateBindingCache();  // new render pass — force pipeline/descriptor rebind

    // Restore viewport to full screen
    cachedViewport_[0] = 0;
    cachedViewport_[1] = 0;
    cachedViewport_[2] = static_cast<int>(swapchain_.GetExtent().width);
    cachedViewport_[3] = static_cast<int>(swapchain_.GetExtent().height);

    // Negative-height viewport (VK_KHR_maintenance1) — see other call sites.
    VkViewport vp{};
    vp.x = 0;
    vp.y = static_cast<float>(swapchain_.GetExtent().height);
    vp.width = static_cast<float>(swapchain_.GetExtent().width);
    vp.height = -static_cast<float>(swapchain_.GetExtent().height);
    vp.minDepth = 0; vp.maxDepth = 1;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, swapchain_.GetExtent()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    activeViewportFBO_ = -1;
    sr_inViewportFBO = false;

}

void vkRenderer::CompositeViewportFBOs(int count, const int viewportRects[][4], const int viewportRotations[])
{
    if (!frameStarted_ || count <= 0) return;

    // Disable depth for 2D composite
    depthTestEnabled_ = false;
    depthWriteEnabled_ = false;

    int screenW = static_cast<int>(swapchain_.GetExtent().width);
    int screenH = static_cast<int>(swapchain_.GetExtent().height);

    // UV lookup table for 0°, 90°, 180°, 270° rotations.
    // Each row: UVs for v0(BL), v1(BR), v2(TR), v3(TL) — 8 floats (u0,v0, u1,v1, u2,v2, u3,v3)
    static const float uvTable[4][8] = {
        { 0,1, 1,1, 1,0, 0,0 }, // 0°
        { 1,1, 1,0, 0,0, 0,1 }, // 90°
        { 1,0, 0,0, 0,1, 1,1 }, // 180°
        { 0,0, 0,1, 1,1, 1,0 }, // 270°
    };

    for (int i = 0; i < count && i < viewportFBOCount_; i++)
    {
        ViewportFBO& vfbo = viewportFBOs_[currentFrame_][i];
        if (!vfbo.colorTexId || vfbo.framebuffer == VK_NULL_HANDLE) continue;

        // Screen-space rect for this viewport (OpenGL coords: y from bottom)
        int x = viewportRects[i][0];
        int y = viewportRects[i][1];
        int w = viewportRects[i][2];
        int h = viewportRects[i][3];

        // Convert to NDC (-1..1) with 1-pixel overlap to fill rounding gaps
        float ndcL = (2.0f * std::max(x - 1, 0) / screenW) - 1.0f;
        float ndcR = (2.0f * std::min(x + w + 1, screenW) / screenW) - 1.0f;
        float ndcB = (2.0f * std::max(y - 1, 0) / screenH) - 1.0f;
        float ndcT = (2.0f * std::min(y + h + 1, screenH) / screenH) - 1.0f;

        // Select UV set based on rotation
        int rotDeg = (viewportRotations ? viewportRotations[i] : 0) % 360;
        if (rotDeg < 0) rotDeg += 360;
        int uvIdx = rotDeg / 90; // 0, 1, 2, or 3
        const float* uv = uvTable[uvIdx];

        // Textured quad: 2 triangles, UV rotated per viewport
        rVertex20 v0, v1, v2, v3;
        v0.SetPosition(ndcL, ndcB, 0); v0.SetColor(255,255,255,255); v0.SetTexCoord(uv[0], uv[1]);
        v1.SetPosition(ndcR, ndcB, 0); v1.SetColor(255,255,255,255); v1.SetTexCoord(uv[2], uv[3]);
        v2.SetPosition(ndcR, ndcT, 0); v2.SetColor(255,255,255,255); v2.SetTexCoord(uv[4], uv[5]);
        v3.SetPosition(ndcL, ndcT, 0); v3.SetColor(255,255,255,255); v3.SetTexCoord(uv[6], uv[7]);

        rRenderStateKey state = rRenderStateKey::HUD(vfbo.colorTexId, rBlendMode::Opaque);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);

        // Submit a nearly-invisible quad that references the depth texture.
        // This forces Metal/MoltenVK to preserve full D32_SFLOAT depth precision
        // by binding the depth texture in the composite render pass. Alpha=1 (out of
        // 255 ≈ 0.4%) prevents the shader compiler from dead-code-eliminating the
        // texture fetch. Without this, Metal uses lossy memoryless tile storage.
        if (vfbo.depthTexId) {
            rVertex20 d0, d1, d2, d3;
            d0.SetPosition(ndcL, ndcB, 0); d0.SetColor(0,0,0,1); d0.SetTexCoord(0,0);
            d1.SetPosition(ndcR, ndcB, 0); d1.SetColor(0,0,0,1); d1.SetTexCoord(1,0);
            d2.SetPosition(ndcR, ndcT, 0); d2.SetColor(0,0,0,1); d2.SetTexCoord(1,1);
            d3.SetPosition(ndcL, ndcT, 0); d3.SetColor(0,0,0,1); d3.SetTexCoord(0,1);
            rRenderStateKey depthState = rRenderStateKey::HUD(vfbo.depthTexId, rBlendMode::Alpha);
            rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, depthState, d0, d1, d2, d3);
        }
    }

    rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);

    // Re-enable depth for subsequent rendering
    depthTestEnabled_ = true;
    depthWriteEnabled_ = true;
}

// Global helpers
void sr_DrawInstancedModelMesh(uint64_t meshId,
                               const rInstanceData* instances, size_t instanceCount,
                               unsigned int textureId)
{
    if (s_vkRenderer) s_vkRenderer->DrawInstancedModelMesh(meshId, instances, instanceCount, textureId);
}

bool sr_IsModelMeshCached(uint64_t meshId)
{
    if (!s_vkRenderer || meshId == 0) return false;
    return s_vkRenderer->IsModelMeshCached(meshId);
}

bool vkRenderer::IsModelMeshCached(uint64_t meshId) const
{
    auto it = modelMeshCache_.find(meshId);
    return it != modelMeshCache_.end() && !it->second.litVerts.empty();
}

uint32_t sr_GetModelMeshCacheVersion_impl()
{
    return s_vkRenderer ? s_vkRenderer->GetModelMeshCacheVersion() : 0;
}

static int s_savedScreenW = 0, s_savedScreenH = 0;

void sr_BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h)
{
    // Increment multi-viewport guard so subsequent viewports don't re-accumulate wall segments
    sr_WallComputeOnViewportBegin();
    if (s_vkRenderer) s_vkRenderer->BeginViewportFBO(index, totalViewports, x, y, w, h);
    // Cockpit code uses sr_screenWidth/Height for sizing — expose FBO dimensions
    // so per-viewport widgets are sized relative to the FBO, not the full swapchain.
    s_savedScreenW = sr_screenWidth;
    s_savedScreenH = sr_screenHeight;
    sr_screenWidth  = w;
    sr_screenHeight = h;
}

void sr_EndViewportFBO()
{
    sr_screenWidth  = s_savedScreenW;
    sr_screenHeight = s_savedScreenH;
    if (s_vkRenderer) s_vkRenderer->EndViewportFBO();
}

void sr_CompositeViewportFBOs(int count, const int viewportRects[][4], const int viewportRotations[])
{
    if (s_vkRenderer) s_vkRenderer->CompositeViewportFBOs(count, viewportRects, viewportRotations);
}

// ============================================================================
// Wall GPU compute API (Sprint 3.1)
// ============================================================================

void sr_WallComputeOnViewportBegin()
{
    sg_wallAccumViewportN_++;
}

bool sr_IsWallComputeActive()
{
    return s_vkRenderer && s_vkRenderer->IsWallComputeActive();
}

void sr_AddWallComputeSegment(float p1x, float p1y, float p2x, float p2y,
                               float ta,  float te,  float r,   float g,   float b)
{
    if (!s_vkRenderer || !s_vkRenderer->IsWallComputeActive())
        return;
    if (sg_wallAccumViewportN_ != 0)
        return;  // only first viewport accumulates
    if (sg_wallSegsCurrent_.size() >= 65536u)  // matches vkRenderer::kMaxWallSegments
        return;  // SSBO full — excess segments fall back to CPU path

    // Pack color as RGBA8 (A = 255 for walls)
    uint32_t ri = static_cast<uint32_t>(std::min(255.0f, r * 255.0f + 0.5f));
    uint32_t gi = static_cast<uint32_t>(std::min(255.0f, g * 255.0f + 0.5f));
    uint32_t bi = static_cast<uint32_t>(std::min(255.0f, b * 255.0f + 0.5f));
    uint32_t packed = ri | (gi << 8u) | (bi << 16u) | (0xFFu << 24u);

    WallSegmentGPU seg;
    seg.p1x     = p1x;
    seg.p1y     = p1y;
    seg.p2x     = p2x;
    seg.p2y     = p2y;
    seg.u_start = ta;
    seg.u_end   = te;
    seg.color   = packed;
    seg.flags   = 1u;  // alive

    sg_wallSegsCurrent_.push_back(seg);

    // Expand persistent UV bounds (never shrink, prevents texture matrix pops)
    if (ta < sg_wallUMin_) sg_wallUMin_ = ta;
    if (te < sg_wallUMin_) sg_wallUMin_ = te;
    if (ta > sg_wallUMax_) sg_wallUMax_ = ta;
    if (te > sg_wallUMax_) sg_wallUMax_ = te;
}

uint32_t sr_GetWallComputeCurrentCount()
{
    return s_vkRenderer ? s_vkRenderer->GetWallComputeCurrentCount() : 0u;
}

void sr_DrawComputedWallsRange(unsigned int textureId, uint32_t startSeg, uint32_t segCount)
{
    if (s_vkRenderer) s_vkRenderer->DrawComputedWallsRange(textureId, startSeg, segCount);
}

// ============================================================================
// Fence sync
// ============================================================================

void* vkRenderer::CreateFence()
{
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence* fence = new VkFence;
    if (vkCreateFence(context_.GetDevice(), &info, nullptr, fence) != VK_SUCCESS)
    {
        delete fence;
        return nullptr;
    }
    userFences_.push_back(fence);
    return fence;
}

void vkRenderer::WaitFence(void* fencePtr)
{
    if (!fencePtr) return;
    VkFence fence = *static_cast<VkFence*>(fencePtr);
    vkWaitForFences(context_.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
}

void vkRenderer::DeleteFence(void* fencePtr)
{
    if (!fencePtr) return;
    VkFence* ptr = static_cast<VkFence*>(fencePtr);
    vkDestroyFence(context_.GetDevice(), *ptr, nullptr);
    userFences_.erase(std::remove(userFences_.begin(), userFences_.end(), ptr), userFences_.end());
    delete ptr;
}

// ============================================================================
// Lighting / Material / Clip planes (stubs — tracked but not yet rendered)
// ============================================================================

void vkRenderer::Light(int light, int pname, const REAL* params)
{
    int idx = light - 0x4000; // GL_LIGHT0 = 0x4000
    if (idx < 0 || idx >= 2) return;
    float p[4] = {static_cast<float>(params[0]), static_cast<float>(params[1]),
                   static_cast<float>(params[2]), static_cast<float>(params[3])};
    switch (pname)
    {
    case 0x1203: // GL_POSITION — transform by current modelview (matches OpenGL)
    {   // At light setup time, modelview = camera only (no cycle transform).
        // This puts light directions into view space.
        glm::mat4 mv = modelviewStack_.GetMat4();
        // Save inverse view for shadow geometry collection (model→world transform)
        if (idx == 0) shadowViewInverse_ = glm::inverse(mv);
        glm::vec4 pos(p[0], p[1], p[2], p[3]);
        glm::vec4 transformed = mv * pos;
        memcpy(lights_[idx].position, &transformed[0], 16);
        break;
    }
    case 0x1201: memcpy(lights_[idx].diffuse, p, 16); lightingDirty_ = true; break;  // GL_DIFFUSE
    case 0x1202: memcpy(lights_[idx].specular, p, 16); lightingDirty_ = true; break; // GL_SPECULAR
    }
    lightingDirty_ = true;
}

void vkRenderer::Material(int /*face*/, int pname, const REAL* params)
{
    float p[4] = {static_cast<float>(params[0]), static_cast<float>(params[1]),
                   static_cast<float>(params[2]), static_cast<float>(params[3])};
    switch (pname)
    {
    case 0x1201: memcpy(materialDiffuse_, p, 16); break;  // GL_DIFFUSE
    case 0x1202: memcpy(materialSpecular_, p, 16); break; // GL_SPECULAR
    }
    lightingDirty_ = true;
}

void vkRenderer::Normal(REAL, REAL, REAL) {}
// Dead code — zero call sites, kept as empty overrides for interface compliance
void vkRenderer::Hint(int, int) {}       // called but harmless (quality hints)
void vkRenderer::AlphaFunc(int, REAL) {} // called but replaced by shader discard
void vkRenderer::PrepareForVBODraw() {}

// Shared helper: build push constants for any draw batch type.
// Handles SDF, UseTexMatrix, normal rendering, and render context embedding.
void vkRenderer::BuildPushConstants(VkPushConstants& pc,
                                     const rRenderStateKey& state,
                                     bool lit)
{
    // MVP
    glm::mat4 mvp = projectionStack_.GetMat4() * modelviewStack_.GetMat4();
    memcpy(pc.mvp, glm::value_ptr(mvp), 64);

    // Normal matrix (lit geometry only)
    if (lit)
    {
        glm::mat4 normalMat = modelviewStack_.GetMat4();
        memcpy(pc.normalMatrix, glm::value_ptr(normalMat), 64);
    }

    // Texture matrix
    if (state.flags & rRenderStateKey::FontSDF)
    {
        memcpy(pc.texMatrix, state.texMatrix, sizeof(pc.texMatrix));
    }
    else if (state.flags & rRenderStateKey::UseTexMatrix)
    {
        memcpy(pc.texMatrix, state.texMatrix, 64);
        pc.texMatrix[8] = cachedFrameTime_;
        float stateCtx = state.texMatrix[9];
        pc.texMatrix[9] = (stateCtx != 0.0f) ? stateCtx : static_cast<float>(s_renderContextId);
        pc.texMatrix[10] = (state.flags & rRenderStateKey::AlphaTest) ? 1.0f : 0.0f;
    }
    else
    {
        memcpy(pc.texMatrix, textureStack_.Get(), 64);
        pc.texMatrix[8] = cachedFrameTime_;
        // Render context: prefer embedded (batched), fall back to global
        float stateCtx = state.GetRenderContext();
        pc.texMatrix[9] = (stateCtx != 0.0f) ? stateCtx : static_cast<float>(s_renderContextId);
        pc.texMatrix[10] = (state.flags & rRenderStateKey::AlphaTest) ? 1.0f : 0.0f;
        if (!(state.flags & rRenderStateKey::UseLighting))
            pc.texMatrix[11] = 0.0f;
    }

    // Lighting flag for lit geometry
    if (lit)
        pc.texMatrix[11] = 1.0f;

    // For unlit geometry, pack shader hook data into normalMatrix (unused for unlit):
    //   column 2 (indices 8-11): camera world position (for parallax)
    //   column 3 (indices 12-15): arena bounds (for floor/sky effects)
    // For lit geometry, normalMatrix carries the actual normal matrix and materialDiffuse.
    if (!lit)
    {
        // Camera world position — set directly from eCamera::Render via sr_SetCameraWorldPos
        pc.normalMatrix[8]  = s_cameraWorldPos[0];
        pc.normalMatrix[9]  = s_cameraWorldPos[1];
        pc.normalMatrix[10] = s_cameraWorldPos[2];
        pc.normalMatrix[11] = 0.0f;

        pc.normalMatrix[12] = s_arenaBoundsLow[0];
        pc.normalMatrix[13] = s_arenaBoundsLow[1];
        pc.normalMatrix[14] = s_arenaBoundsHigh[0];
        pc.normalMatrix[15] = s_arenaBoundsHigh[1];
    }
}

// Shared helper: look up texture descriptor, fall back to dummy white.
VkDescriptorSet vkRenderer::LookupTextureDescriptor(const rRenderStateKey& state)
{
    if ((state.flags & rRenderStateKey::UseTexture) && state.textureId != 0)
    {
        auto it = textures_.find(static_cast<unsigned int>(state.textureId));
        if (it != textures_.end() && it->second.view != VK_NULL_HANDLE)
        {
            VkDescriptorSet texSet = descriptorManager_.GetOrCreateTextureSet(
                it->second.view, it->second.sampler, it->second.descriptorLayout);
            if (texSet != VK_NULL_HANDLE)
                return texSet;
        }
    }
    return dummyDescriptorSet_;
}

void vkRenderer::DrawBatchTriangles(const void* vertices, size_t vertexCount,
                                    const void* stateKey)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) [[unlikely]] return;

    const rVertex20* verts = static_cast<const rVertex20*>(vertices);
    const rRenderStateKey& state = *static_cast<const rRenderStateKey*>(stateKey);
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkPushConstants pc{};
    BuildPushConstants(pc, state, false);
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    // Respect phase depth state: if the phase disabled depth (e.g. Sky phase
    // for floor rendering), strip the flags from the state key so the pipeline
    // has depth write/test OFF. Without this, floor geometry writes depth even
    // though the Sky phase says depthWrite=false, causing wall z-fighting.
    rRenderStateKey adjustedState = state;
    if (!depthTestEnabled_)
        adjustedState.flags &= ~rRenderStateKey::DepthTest;
    if (!depthWriteEnabled_)
        adjustedState.flags &= ~rRenderStateKey::DepthWrite;

    vulkanQueue_.DrawTriangles(cmd, verts, vertexCount, adjustedState,
                               pipelineManager_, descSet,
                               &pc, sizeof(pc), cullFaceEnabled_, frontFaceCW_, 0xF);
}

void vkRenderer::DrawBatchLitTriangles(const void* vertices, size_t vertexCount,
                                        const void* stateKey)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) [[unlikely]] return;

    const rRenderStateKey& state = *static_cast<const rRenderStateKey*>(stateKey);
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // Update lighting UBO only when state has changed (lights, materials, arena bounds)
    if (lightingDirty_ && lightingUBOMapped_[currentFrame_])
    {
        LightingUBO ubo{};
        for (int i = 0; i < 2; i++)
        {
            memcpy(ubo.lightPos[i], lights_[i].position, 16);
            memcpy(ubo.lightDiffuse[i], lights_[i].diffuse, 16);
            memcpy(ubo.lightSpecular[i], lights_[i].specular, 16);
        }
        memcpy(ubo.materialDiffuse, materialDiffuse_, 16);
        memcpy(ubo.materialSpecular, materialSpecular_, 16);
        ubo.lightingEnabled = 1;
        ubo.shadowEnabled = (sr_shadowMode == rSHADOW_MAP && shadowMapsCreated_) ? 1 : 0;
        ubo.arenaBBox[0] = s_arenaBoundsLow[0];
        ubo.arenaBBox[1] = s_arenaBoundsLow[1];
        ubo.arenaBBox[2] = s_arenaBoundsHigh[0];
        ubo.arenaBBox[3] = s_arenaBoundsHigh[1];
        if (ubo.shadowEnabled) { memcpy(ubo.shadowVP[0], shadowVP_[0], 64); memcpy(ubo.shadowVP[1], shadowVP_[1], 64); }
        memcpy(lightingUBOMapped_[currentFrame_], &ubo, sizeof(ubo));
        lightingDirty_ = false;
    }

    VkPushConstants pc{};
    BuildPushConstants(pc, state, true);
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    vulkanQueue_.DrawLitTriangles(cmd, vertices, vertexCount, state,
                                   pipelineManager_, descSet,
                                   &pc, sizeof(pc), cullFaceEnabled_, frontFaceCW_, 0xF);

    // Collect lit geometry for shadow pass: transform model-space → world-space
    if (rRenderQueue::Instance().IsShadowCollectionEnabled() && vertexCount >= 3)
    {
        const rVertexLit32* litVerts = static_cast<const rVertexLit32*>(vertices);
        glm::mat4 modelMatrix = shadowViewInverse_ * modelviewStack_.GetMat4();
        auto& shadowVerts = rRenderQueue::Instance().GetShadowDynamicVerticesMut();
        for (size_t i = 0; i < vertexCount; i++)
        {
            glm::vec4 wp = modelMatrix * glm::vec4(litVerts[i].position[0],
                                                    litVerts[i].position[1],
                                                    litVerts[i].position[2], 1.0f);
            rVertex20 sv;
            sv.SetPosition(wp.x, wp.y, wp.z);
            sv.SetColor(255, 255, 255, 255);
            shadowVerts.push_back(sv);
        }
    }
}

void vkRenderer::DrawModelMesh(uint64_t meshId,
                               const std::vector<rModelVertex>& vertices,
                               const std::vector<unsigned int>& indices,
                               unsigned int textureId)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) [[unlikely]] return;
    if (vertices.empty()) return;

    // ----- Cache: convert rModelVertex → rVertexLit32 once per unique mesh -----
    auto& entry = modelMeshCache_[meshId];

    if (entry.litVerts.empty())
    {
        // Find the maximum absolute texcoord so we can normalize to [-1, 1] for int16 packing
        float maxTC = 1.0f;
        auto scanTC = [&maxTC](const rModelVertex& v) {
            maxTC = std::max(maxTC, std::abs(v.texcoord[0]));
            maxTC = std::max(maxTC, std::abs(v.texcoord[1]));
        };
        if (!indices.empty())
        {
            for (unsigned int idx : indices)
                if (idx < vertices.size()) scanTC(vertices[idx]);
        }
        else
        {
            for (const auto& v : vertices) scanTC(v);
        }

        entry.texScale = maxTC;
        float invScale = 1.0f / maxTC;

        auto convert = [invScale](const rModelVertex& v) -> rVertexLit32 {
            rVertexLit32 rv;
            rv.SetPosition(v.position[0], v.position[1], v.position[2]);
            rv.SetNormal(v.normal[0], v.normal[1], v.normal[2]);
            rv.SetColor(255, 255, 255, 255);
            float su = v.texcoord[0] * invScale;
            float sv = v.texcoord[1] * invScale;
            rv.texcoord[0] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, su * 32767.0f)));
            rv.texcoord[1] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, sv * 32767.0f)));
            return rv;
        };

        if (!indices.empty())
        {
            entry.litVerts.reserve(indices.size());
            for (unsigned int idx : indices)
                if (idx < vertices.size())
                    entry.litVerts.push_back(convert(vertices[idx]));
        }
        else
        {
            entry.litVerts.reserve(vertices.size());
            for (const auto& v : vertices)
                entry.litVerts.push_back(convert(v));
        }
    }

    if (entry.litVerts.empty()) return;

    // ----- Update lighting UBO only when dirty -----
    if (lightingDirty_ && lightingUBOMapped_[currentFrame_])
    {
        LightingUBO ubo{};
        for (int i = 0; i < 2; i++)
        {
            memcpy(ubo.lightPos[i],     lights_[i].position, 16);
            memcpy(ubo.lightDiffuse[i], lights_[i].diffuse,  16);
            memcpy(ubo.lightSpecular[i],lights_[i].specular, 16);
        }
        memcpy(ubo.materialDiffuse,  materialDiffuse_,  16);
        memcpy(ubo.materialSpecular, materialSpecular_, 16);
        ubo.lightingEnabled = 1;
        ubo.shadowEnabled = (sr_shadowMode == rSHADOW_MAP && shadowMapsCreated_) ? 1 : 0;
        ubo.arenaBBox[0] = s_arenaBoundsLow[0];
        ubo.arenaBBox[1] = s_arenaBoundsLow[1];
        ubo.arenaBBox[2] = s_arenaBoundsHigh[0];
        ubo.arenaBBox[3] = s_arenaBoundsHigh[1];
        if (ubo.shadowEnabled) { memcpy(ubo.shadowVP[0], shadowVP_[0], 64); memcpy(ubo.shadowVP[1], shadowVP_[1], 64); }
        memcpy(lightingUBOMapped_[currentFrame_], &ubo, sizeof(ubo));
        lightingDirty_ = false;
    }

    // ----- Build push constants -----
    VkPushConstants pc{};
    glm::mat4 mvp = projectionStack_.GetMat4() * modelviewStack_.GetMat4();
    memcpy(pc.mvp, glm::value_ptr(mvp), 64);

    // Normal matrix: per-draw modelview for transforming normals to view space.
    glm::mat4 normalMat = modelviewStack_.GetMat4();
    memcpy(pc.normalMatrix, glm::value_ptr(normalMat), 64);

    // Per-draw material diffuse color → normalMatrix 4th column (indices 12-15).
    // The shader only uses mat3(normalMatrix) so the 4th column is free.
    // This MUST be in push constants because the lighting UBO is per-frame:
    // if multiple cycles render, the UBO would only keep the last-written color.
    memcpy(&pc.normalMatrix[12], materialDiffuse_, 16);

    // Texture matrix: diagonal UV scale so SNORM texcoords [−1,1] → [0, texScale]
    // Column-major: [col*4+row]. Set identity with UV scale on diagonal.
    pc.texMatrix[0]  = entry.texScale;   // col0, row0 (U scale)
    pc.texMatrix[5]  = entry.texScale;   // col1, row1 (V scale)
    pc.texMatrix[10] = 0.0f;             // col2, row2 — alpha discard OFF for lit meshes
                                          // Body textures may have alpha=0 (RGB-only formats
                                          // like BMP), which would discard all fragments and
                                          // leave depth holes the shadow renders through.
    pc.texMatrix[15] = 1.0f;             // col3, row3
    // Flag slots — must NOT be overwritten after this point:
    pc.texMatrix[8]  = cachedFrameTime_; // uTexMatrix[2][0] = time
    pc.texMatrix[9]  = static_cast<float>(s_renderContextId); // uTexMatrix[2][1] = render context ID
    pc.texMatrix[11] = 1.0f;             // uTexMatrix[2][3] = lighting flag

    // ----- Descriptor set -----
    VkDescriptorSet descSet = dummyDescriptorSet_;
    if (textureId != 0)
    {
        auto it = textures_.find(textureId);
        if (it != textures_.end() && it->second.view != VK_NULL_HANDLE)
        {
            VkDescriptorSet texSet = descriptorManager_.GetOrCreateTextureSet(
                it->second.view, it->second.sampler, it->second.descriptorLayout);
            if (texSet != VK_NULL_HANDLE)
                descSet = texSet;
        }
    }

    // ----- Submit -----
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    rRenderStateKey state = textureId ? rRenderStateKey::Textured(textureId, rBlendMode::Opaque)
                                      : rRenderStateKey::Colored(rBlendMode::Opaque);

    // Model meshes (ASE, .mod) have inconsistent face winding — never cull.
    vulkanQueue_.DrawLitTriangles(cmd, entry.litVerts.data(), entry.litVerts.size(),
                                   state, pipelineManager_, descSet,
                                   &pc, sizeof(pc),
                                   false, frontFaceCW_, 0xF);

    // Collect model mesh vertices for shadow pass (model-space → world-space)
    if (rRenderQueue::Instance().IsShadowCollectionEnabled() && !entry.litVerts.empty())
    {
        glm::mat4 modelMatrix = shadowViewInverse_ * modelviewStack_.GetMat4();
        auto& shadowVerts = rRenderQueue::Instance().GetShadowDynamicVerticesMut();
        for (const auto& lv : entry.litVerts)
        {
            glm::vec4 wp = modelMatrix * glm::vec4(lv.position[0], lv.position[1], lv.position[2], 1.0f);
            rVertex20 sv;
            sv.SetPosition(wp.x, wp.y, wp.z);
            sv.SetColor(255, 255, 255, 255);
            shadowVerts.push_back(sv);
        }
    }
}

void vkRenderer::DrawInstancedModelMesh(uint64_t meshId,
                                        const rInstanceData* instances, size_t instanceCount,
                                        unsigned int textureId)
{
    if (!frameStarted_ || meshId == 0 || instanceCount == 0) return;

    // Look up cached geometry by stable mesh ID
    auto it = modelMeshCache_.find(meshId);
    if (it == modelMeshCache_.end() || it->second.litVerts.empty())
    {
        static int s_warnCount = 0;
        if (s_warnCount++ < 10)
            std::cerr << "[Vulkan] DrawInstancedModelMesh: cache miss for meshId="
                      << meshId
                      << " (cache size=" << modelMeshCache_.size() << ")\n";
        return;
    }

    const auto& entry = it->second;

    // Update lighting UBO if dirty
    if (lightingDirty_ && lightingUBOMapped_[currentFrame_])
    {
        LightingUBO ubo{};
        for (int i = 0; i < 2; i++)
        {
            memcpy(ubo.lightPos[i],     lights_[i].position, 16);
            memcpy(ubo.lightDiffuse[i], lights_[i].diffuse,  16);
            memcpy(ubo.lightSpecular[i],lights_[i].specular, 16);
        }
        memcpy(ubo.materialDiffuse,  materialDiffuse_,  16);
        memcpy(ubo.materialSpecular, materialSpecular_, 16);
        ubo.lightingEnabled = 1;
        ubo.shadowEnabled = (sr_shadowMode == rSHADOW_MAP && shadowMapsCreated_) ? 1 : 0;
        ubo.arenaBBox[0] = s_arenaBoundsLow[0];
        ubo.arenaBBox[1] = s_arenaBoundsLow[1];
        ubo.arenaBBox[2] = s_arenaBoundsHigh[0];
        ubo.arenaBBox[3] = s_arenaBoundsHigh[1];
        if (ubo.shadowEnabled) { memcpy(ubo.shadowVP[0], shadowVP_[0], 64); memcpy(ubo.shadowVP[1], shadowVP_[1], 64); }
        memcpy(lightingUBOMapped_[currentFrame_], &ubo, sizeof(ubo));
        lightingDirty_ = false;
    }

    // Push constants: VP (not MVP) — Model comes per-instance
    VkPushConstants pc{};
    glm::mat4 vp = projectionStack_.GetMat4() * modelviewStack_.GetMat4();
    memcpy(pc.mvp, glm::value_ptr(vp), 64);

    // Normal matrix: View only (not ModelView) — combined with instance model in shader
    glm::mat4 viewMat = modelviewStack_.GetMat4();
    memcpy(pc.normalMatrix, glm::value_ptr(viewMat), 64);

    // Per-draw material diffuse → normalMatrix 4th column (indices 12-15).
    // Set to white — the per-instance team color comes via aInstanceColor in the shader.
    pc.normalMatrix[12] = 1.0f;
    pc.normalMatrix[13] = 1.0f;
    pc.normalMatrix[14] = 1.0f;
    pc.normalMatrix[15] = 1.0f;

    // Texture matrix
    pc.texMatrix[0]  = entry.texScale;
    pc.texMatrix[5]  = entry.texScale;
    pc.texMatrix[10] = 0.0f;  // no alpha discard
    pc.texMatrix[15] = 1.0f;
    pc.texMatrix[8]  = cachedFrameTime_;
    pc.texMatrix[9]  = static_cast<float>(s_renderContextId);
    pc.texMatrix[11] = 1.0f;  // lighting enabled

    // Descriptor set for texture
    VkDescriptorSet descSet = dummyDescriptorSet_;
    if (textureId != 0)
    {
        auto texIt = textures_.find(textureId);
        if (texIt != textures_.end() && texIt->second.view != VK_NULL_HANDLE)
        {
            VkDescriptorSet texSet = descriptorManager_.GetOrCreateTextureSet(
                texIt->second.view, texIt->second.sampler, texIt->second.descriptorLayout);
            if (texSet != VK_NULL_HANDLE)
                descSet = texSet;
        }
    }

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    rRenderStateKey state = textureId ? rRenderStateKey::Textured(textureId, rBlendMode::Opaque)
                                      : rRenderStateKey::Colored(rBlendMode::Opaque);

    vulkanQueue_.DrawInstancedLitTriangles(cmd,
        entry.litVerts.data(), entry.litVerts.size(),
        instances, instanceCount,
        state, pipelineManager_, descSet,
        &pc, sizeof(pc),
        false, frontFaceCW_);

    // Collect instanced geometry for shadow pass: for each instance, transform
    // the shared mesh vertices by the instance's model matrix (already world-space)
    if (rRenderQueue::Instance().IsShadowCollectionEnabled() && !entry.litVerts.empty())
    {
        auto& shadowVerts = rRenderQueue::Instance().GetShadowDynamicVerticesMut();
        for (size_t inst = 0; inst < instanceCount; inst++)
        {
            glm::mat4 model = glm::make_mat4(instances[inst].modelMatrix);
            for (const auto& lv : entry.litVerts)
            {
                glm::vec4 wp = model * glm::vec4(lv.position[0], lv.position[1], lv.position[2], 1.0f);
                rVertex20 sv;
                sv.SetPosition(wp.x, wp.y, wp.z);
                sv.SetColor(255, 255, 255, 255);
                shadowVerts.push_back(sv);
            }
        }
    }
}

void vkRenderer::DrawBatchLines(const void* vertices, size_t vertexCount,
                                const void* stateKey)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) [[unlikely]] return;

    const rVertex20* verts = static_cast<const rVertex20*>(vertices);
    const rRenderStateKey& state = *static_cast<const rRenderStateKey*>(stateKey);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkPushConstants pc{};
    BuildPushConstants(pc, state, false);
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    // Respect phase depth state (same as DrawBatchTriangles)
    rRenderStateKey adjustedState = state;
    if (!depthTestEnabled_)
        adjustedState.flags &= ~rRenderStateKey::DepthTest;
    if (!depthWriteEnabled_)
        adjustedState.flags &= ~rRenderStateKey::DepthWrite;

    vulkanQueue_.DrawLines(cmd, verts, vertexCount, adjustedState,
                           pipelineManager_, descSet,
                           &pc, sizeof(pc), cullFaceEnabled_, frontFaceCW_, 0xF);
}
void vkRenderer::SetTextureEnabled(bool e) { textureEnabled_ = e; }
void vkRenderer::SetLightingEnabled(bool e) { lightingEnabled_ = e; }

void vkRenderer::ReallySetFlag(flag f, bool c)
{
    switch (f)
    {
    case ALPHA_BLEND: blendEnabled_ = c; break;
    case DEPTH_TEST: depthTestEnabled_ = c; break;
    case BACKFACE_CULL: cullFaceEnabled_ = c; break;
    default: break;
    }
}

// ============================================================================
// Vertex arrays (legacy, stubbed)
// ============================================================================

// These are all inherited with default no-op from rRenderer base class.

// ============================================================================
// Shader reload (for moviepack changes)
// ============================================================================

void vkRenderer::ReloadShaders()
{
    VK_LOG_INFO("[Vulkan] ReloadShaders called" << std::endl);
    // Always defer to the next BeginFrame boundary. This coalesces multiple
    // rapid reload requests (e.g. moviepack deactivate + activate = 2 calls)
    // into a single pipeline rebuild at a clean frame boundary.
    // Safe to set even before init — DoReloadShaders() checks IsInitialized().
    pendingShaderReload_ = true;
    VK_LOG_INFO("[Vulkan] ReloadShaders: deferred to next BeginFrame" << std::endl);
}

void vkRenderer::DoReloadShaders()
{
    VK_LOG_INFO("[Vulkan] DoReloadShaders: executing" << std::endl);
    if (!IsInitialized()) return;

    // Load new shader modules first — do NOT touch the live shaders until
    // we have valid replacements.
    VkDevice device = context_.GetDevice();
    VkShaderModule newVert          = VK_NULL_HANDLE;
    VkShaderModule newVertInstanced = VK_NULL_HANDLE;
    VkShaderModule newFrag          = VK_NULL_HANDLE;
    VkShaderModule newFragEmissive  = VK_NULL_HANDLE;

#ifdef HAVE_SHADERC_SHADERC_HPP
    // Runtime GLSL compilation with moviepack override support.
    std::vector<std::string> includePaths;

    tString mvHooks = tDirectories::Data().GetReadPath("moviepack/shaders/uber_hooks.glsl");
    std::cerr << "[Vulkan] ReloadShaders: mvHooks='" << static_cast<const char*>(mvHooks) << "' len=" << mvHooks.Len() << std::endl;
    if (mvHooks.Len() > 1)
    {
        std::string p = static_cast<const char*>(mvHooks);
        auto slash = p.find_last_of('/');
        if (slash != std::string::npos) includePaths.push_back(p.substr(0, slash));
    }

    tString sysHooks = tDirectories::Data().GetReadPath("shaders/uber_hooks.glsl");
    std::cerr << "[Vulkan] ReloadShaders: sysHooks='" << static_cast<const char*>(sysHooks) << "'" << std::endl;
    if (sysHooks.Len() > 1)
    {
        std::string p = static_cast<const char*>(sysHooks);
        auto slash = p.find_last_of('/');
        if (slash != std::string::npos) includePaths.push_back(p.substr(0, slash));
    }

    std::cerr << "[Vulkan] ReloadShaders: includePaths =";
    for (const auto& p : includePaths) VK_LOG_INFO(" [" << p << "]");
    VK_LOG_INFO("\n");

    tString vertSrcPath = tDirectories::Data().GetReadPath("moviepack/shaders/uber.vert");
    if (vertSrcPath.Len() <= 1)
        vertSrcPath = tDirectories::Data().GetReadPath("shaders/uber.vert");
    if (vertSrcPath.Len() > 1)
    {
        std::string err;
        newVert = rVulkanShader::CompileFromFile(
            device, static_cast<const char*>(vertSrcPath),
            rVulkanShader::Stage::Vertex, includePaths, &err);
        if (newVert == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: vertex compile failed:\n" << err << "\n";
    }

    tString vertInstancedSrcPath = tDirectories::Data().GetReadPath("moviepack/shaders/uber_instanced.vert");
    if (vertInstancedSrcPath.Len() <= 1)
        vertInstancedSrcPath = tDirectories::Data().GetReadPath("shaders/uber_instanced.vert");
    if (vertInstancedSrcPath.Len() > 1)
    {
        std::string err;
        newVertInstanced = rVulkanShader::CompileFromFile(
            device, static_cast<const char*>(vertInstancedSrcPath),
            rVulkanShader::Stage::Vertex, includePaths, &err);
        if (newVertInstanced == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: instanced vertex compile failed:\n" << err << "\n";
    }

    tString fragSrcPath = tDirectories::Data().GetReadPath("moviepack/shaders/uber.frag");
    if (fragSrcPath.Len() <= 1)
        fragSrcPath = tDirectories::Data().GetReadPath("shaders/uber.frag");
    if (fragSrcPath.Len() > 1)
    {
        const std::string fragPathStr = static_cast<const char*>(fragSrcPath);

        std::string err;
        auto spvPlain = rVulkanShader::CompileGLSLFromFile(
            fragPathStr, rVulkanShader::Stage::Fragment, includePaths, {}, &err);
        if (!spvPlain.empty())
            newFrag = rVulkanShader::LoadFromMemory(device, spvPlain);
        if (newFrag == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: fragment compile failed (plain):\n" << err << "\n";

        std::string errEm;
        auto spvEmissive = rVulkanShader::CompileGLSLFromFile(
            fragPathStr, rVulkanShader::Stage::Fragment, includePaths,
            {{"USE_EMISSIVE_OUT", "1"}}, &errEm);
        if (!spvEmissive.empty())
            newFragEmissive = rVulkanShader::LoadFromMemory(device, spvEmissive);
        if (newFragEmissive == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: fragment compile failed (emissive):\n" << errEm << "\n";
    }

    VK_LOG_INFO("[Vulkan] ReloadShaders: vert=" << static_cast<const char*>(vertSrcPath)
              << " frag=" << static_cast<const char*>(fragSrcPath)
              << " newVert=" << newVert << " newVertInstanced=" << newVertInstanced
              << " newFrag=" << newFrag
              << " newFragEmissive=" << newFragEmissive << std::endl);
#else
    // No shaderc (iOS/Android): load pre-compiled SPIR-V.
    // Try moviepack directory first (extracted ZIP has compiled .spv files),
    // then fall back to system shaders bundled with the app.
    {
        auto loadSPV = [&](const char* moviepackPath, const char* systemPath) -> VkShaderModule {
            tString mvpPath = tDirectories::Data().GetReadPath(moviepackPath);
            if (mvpPath.Len() > 1)
                return rVulkanShader::LoadFromFile(device, static_cast<const char*>(mvpPath));
            tString sysPath = tDirectories::Data().GetReadPath(systemPath);
            if (sysPath.Len() > 1)
                return rVulkanShader::LoadFromFile(device, static_cast<const char*>(sysPath));
            return rVulkanShader::LoadFromFile(device, systemPath);
        };
        newVert          = loadSPV("moviepack/shaders/uber.vert.spv",           "shaders/uber.vert.spv");
        newVertInstanced = loadSPV("moviepack/shaders/uber_instanced.vert.spv", "shaders/uber_instanced.vert.spv");
        newFrag          = loadSPV("moviepack/shaders/uber.frag.spv",           "shaders/uber.frag.spv");
        newFragEmissive  = loadSPV("moviepack/shaders/uber.frag.emissive.spv",  "shaders/uber.frag.emissive.spv");
    }
#endif

    if (!newVert || !newVertInstanced || !newFrag || !newFragEmissive)
    {
        std::cerr << "[Vulkan] ReloadShaders: shader compile failed — keeping current shaders\n";
        if (newVert)          vkDestroyShaderModule(device, newVert, nullptr);
        if (newVertInstanced) vkDestroyShaderModule(device, newVertInstanced, nullptr);
        if (newFrag)          vkDestroyShaderModule(device, newFrag, nullptr);
        if (newFragEmissive)  vkDestroyShaderModule(device, newFragEmissive, nullptr);
        return; // leave current shaders and pipeline intact
    }

    // New shaders loaded successfully.  Now swap them in.
    vkDeviceWaitIdle(device);

    // Flush all cached and deferred descriptor sets.  The device is idle so
    // all in-flight command buffers are done; cached sets referencing old
    // image views (and the views of any FBOs/textures we're about to rebuild)
    // must be freed now.  Without this, RecreateSwapchain (triggered later
    // by PP reload) calls FlushAllDeferred and frees sets that were already
    // recorded into the current frame's command buffer → VUID errors.
    descriptorManager_.FlushAllDeferred();
    // Rebuild dummy descriptor set — it lives outside the cache and is now freed.
    dummyDescriptorSet_ = descriptorManager_.GetOrCreateTextureSet(
        dummyTexture_.view, dummyTexture_.sampler);

    vulkanQueue_.CompactIfNeeded(device);

    // Invalidate model mesh cache — models may have changed (moviepack switch)
    // and the pointer-based cache keys could alias freed memory.
    modelMeshCache_.clear();
    sr_modelCacheVersion++;
    ++modelMeshCacheVersion_;

    pipelineManager_.Destroy();
    if (vertShader_          != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertShader_, nullptr);
    if (vertShaderInstanced_ != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertShaderInstanced_, nullptr);
    if (fragShader_          != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShader_, nullptr);
    if (fragShaderEmissive_  != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShaderEmissive_, nullptr);
    vertShader_          = newVert;
    vertShaderInstanced_ = newVertInstanced;
    fragShader_          = newFrag;
    fragShaderEmissive_  = newFragEmissive;

    VkDescriptorSetLayout reloadSetLayouts[2] = { descriptorManager_.GetLayout(), lightingUBOLayout_ };
    VkPipelineLayout layout;
    bool initOk = pipelineManager_.Init(device, framebuffer_.GetRenderPass(),
                               vertShader_, fragShader_, fragShaderEmissive_,
                               reloadSetLayouts, 2, &layout,
                               &context_.GetDeviceProperties());
    if (!initOk)
    {
        std::cerr << "[Vulkan] ReloadShaders: pipelineManager_.Init failed" << std::endl;
    }
    pipelineManager_.SetInstancedVertShader(vertShaderInstanced_);

    // Pre-warm the pipeline cache against the new shaders so the first post-reload
    // frame doesn't stall while NVIDIA/AMD drivers JIT-compile every state combo on
    // first use. Mirrors the initial Init's prewarm at line ~506.
    pipelineManager_.PrewarmCommonPipelines();

    // If post-process is active, also prewarm against the PP scene render pass.
    // In-game frames render into the offscreen target whose render pass differs
    // from the swapchain's, so without this the first PP-active draw after a
    // moviepack switch JIT-compiles a second full set of pipelines on demand.
    // BeginFrame's reorder (PP rebuild before reload) guarantees ppRP is current
    // and registered with the pipeline manager via BuildOffscreen→RegisterRenderPass.
    if (postProcess_.IsEnabled())
    {
        VkRenderPass ppRP = postProcess_.GetSceneRenderPass();
        if (ppRP != VK_NULL_HANDLE)
        {
            pipelineManager_.SetRenderPass(ppRP);
            pipelineManager_.PrewarmCommonPipelines();
            pipelineManager_.SetRenderPass(framebuffer_.GetRenderPass());
        }
    }

    VK_LOG_INFO("[Vulkan] ReloadShaders: complete" << std::endl);
}

// ============================================================================
// Arena bounds setter (called from sr_SetArenaBounds in rGL3Render.cpp)
// ============================================================================

// Forward decl: defined in rVulkanPostProcess.cpp so Execute() can read it.
extern void sr_vkPostProcessSetArenaBounds(float minX, float minY, float maxX, float maxY);

void sr_vkSetArenaBounds(float lowX, float lowY, float highX, float highY)
{
    // If bounds changed (new round/arena), invalidate static shadow geometry
    if (s_arenaBoundsLow[0] != lowX || s_arenaBoundsLow[1] != lowY ||
        s_arenaBoundsHigh[0] != highX || s_arenaBoundsHigh[1] != highY)
    {
        rRenderQueue::Instance().InvalidateShadowStatic();
    }
    s_arenaBoundsLow[0]  = lowX;
    s_arenaBoundsLow[1]  = lowY;
    s_arenaBoundsHigh[0] = highX;
    s_arenaBoundsHigh[1] = highY;
    sr_vkPostProcessSetArenaBounds(lowX, lowY, highX, highY);
}

void sr_vkSetCameraWorldPos(float x, float y, float z)
{
    s_cameraWorldPos[0] = x;
    s_cameraWorldPos[1] = y;
    s_cameraWorldPos[2] = z;
}

void sr_vkGetCameraWorldPos(float& x, float& y, float& z)
{
    x = s_cameraWorldPos[0];
    y = s_cameraWorldPos[1];
    z = s_cameraWorldPos[2];
}

// Called from sr_SetRenderContext whenever the render context changes.
// Stores the full rRenderContext enum value; the shader reads it from push constants
// to dispatch per-component hook functions. Also tracks whether any in-game
// 3D content has been drawn this frame, so BeginFrame can decide next frame
// whether to route through the post-process offscreen target.
//
// rRenderContext enum values (from rRendererState.h):
//   3 = Game3D generic, 4 = Sky, 5 = Floor, 6 = RimWalls,
//   7 = PlayerWalls, 8 = Cycles, 9 = Zones, 10 = Effects
void sr_vkSetRenderContext(int contextId)
{
    s_renderContextId = contextId;
    if (contextId >= 3 && contextId <= 10)
        s_currentFrameIsInGame = true;
}

// ============================================================================
// Global init
// ============================================================================

void sr_vkRendererInit()
{
    // Just create the renderer object — Vulkan init is deferred until
    // the SDL window is available (called from sr_InitDisplay path).
    s_vkRenderer = new vkRenderer();
}

//! Called after SDL window is created to complete Vulkan initialization
void sr_vkRendererLateInit()
{
    extern SDL_Window* sr_screen;

    if (s_vkRenderer && sr_screen && !s_vkRenderer->IsInitialized())
    {
        if (!s_vkRenderer->Init(sr_screen))
        {
            std::cerr << "[Vulkan] Failed to initialize Vulkan renderer" << std::endl;
        }
    }
}

void sr_vkSetAppInBackground(bool inBackground)
{
    if (s_vkRenderer)
        s_vkRenderer->SetAppInBackground(inBackground);
}

void sr_vkRequestSwapchainRecreation()
{
    if (s_vkRenderer)
        s_vkRenderer->SetNeedsSwapchainRecreation();
}

void sr_vkRendererReloadShaders()
{
    if (s_vkRenderer)
        s_vkRenderer->ReloadShaders();
}

bool sr_vkIsFrameStarted()
{
    return s_vkRenderer && s_vkRenderer->IsFrameStarted();
}

void sr_vkWaitIdle()
{
    if (s_vkRenderer && s_vkRenderer->IsInitialized())
    {
        // End the current frame if one is in progress, then wait for GPU idle.
        // This ensures no in-flight command buffers reference resources that
        // are about to be freed (textures, models, etc.).
        if (s_vkRenderer->IsFrameStarted())
            s_vkRenderer->EndFrame();
        vkDeviceWaitIdle(s_vkRenderer->GetDevice());
    }
}

//! Called from gMoviepackManager::ActivateMoviepack after resources have
//! been reloaded. Rebinds the post-process subsystem's effect registry to
//! the newly-active moviepack (effects get re-loaded from the moviepack's
//! shader directory on next use) and loads MVP parameter overrides from
//! the per-moviepack cfg file.
void sr_vkPostProcessOnMoviepackActivated(const char* moviepackName,
                                            const char* newEffectName)
{
    if (s_vkRenderer)
        s_vkRenderer->GetPostProcess().OnMoviepackActivated(
            moviepackName  ? std::string(moviepackName)  : std::string(),
            newEffectName  ? std::string(newEffectName)  : std::string());
}

//! Called from gMoviepackManager::DeactivateMoviepack before resources are
//! reloaded from the base game. Writes current MVP values to the old
//! moviepack's cfg file, unregisters the dynamic tSettingItems, and
//! destroys cached effects so they get rebuilt from the base game path.
void sr_vkPostProcessOnMoviepackDeactivated()
{
    if (s_vkRenderer)
        s_vkRenderer->GetPostProcess().OnMoviepackDeactivated();
}

//! Activate post-processing with the given effect (called by moviepack system and Lua aa_pp_enable).
//! Thread: render thread only.
void sr_vkPostProcessActivate(const char* effectName)
{
    s_pendingPPEnabled = true;
    s_pendingPPEffect  = effectName ? effectName : "";
    s_pendingPPDirty   = true;
}

//! Deactivate post-processing (called by moviepack system and Lua aa_pp_disable).
//! On macOS, PP infrastructure stays enabled (passthrough) for depth preservation.
//! Thread: render thread only.
void sr_vkPostProcessDeactivate()
{
    s_pendingPPEnabled = false;
    s_pendingPPEffect.clear();
    s_pendingPPDirty   = true;
}

#endif // DEDICATED
