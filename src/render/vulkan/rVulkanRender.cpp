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
#include <cstring>
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

// Post-processing config. The renderer object reads these on each frame via
// getters below, so ordering between config load and renderer init doesn't
// matter — whichever comes first wins, and the other catches up.
static bool    sr_postProcessEnabled = false;
static tString sr_postProcessEffect  = tString("passthrough");

// Forward-declared callback — actual renderer state is applied by the
// renderer itself on the next frame. We just flip a sticky dirty flag here.
static bool s_postProcessConfigDirty = true;
static void sr_postProcessConfigChanged() { s_postProcessConfigDirty = true; }

static tConfItem<bool> sr_postProcessEnabledCI(
    "POST_PROCESS_ENABLED", sr_postProcessEnabled, &sr_postProcessConfigChanged);
static tConfItemLine sr_postProcessEffectCI(
    "POST_PROCESS_EFFECT", sr_postProcessEffect, &sr_postProcessConfigChanged);

// Renderer-side accessors (used from vkRenderer::BeginFrame to pick up changes).
static bool sr_vkPostProcessEnabled() {
#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // On macOS MoltenVK, post-process must always be active during in-game
    // rendering. The offscreen render pass with STORE+SAMPLED_BIT on depth,
    // followed by a composite pass that binds the depth texture, forces Metal
    // to preserve full D32_SFLOAT precision. Without this, Metal uses lossy
    // memoryless depth storage, causing z-fighting on Apple Silicon.
    return true;
#else
    return sr_postProcessEnabled;
#endif
}
static const char* sr_vkPostProcessEffect() { return sr_postProcessEffect; }
static bool sr_vkPostProcessConfigTakeDirty()
{
    bool d = s_postProcessConfigDirty;
    s_postProcessConfigDirty = false;
    return d;
}

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
    , depthFunc_(rGLConst::Less)
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
        {
            if (shadowStagingBuf_[i]) vkDestroyBuffer(device, shadowStagingBuf_[i], nullptr);
            if (shadowStagingMem_[i]) vkFreeMemory(device, shadowStagingMem_[i], nullptr);
            shadowStagingBuf_[i] = VK_NULL_HANDLE;
            shadowStagingMem_[i] = VK_NULL_HANDLE;
        }

        // Erase viewport FBO texture entries from textures_ map so the texture
        // loop below doesn't double-destroy resources owned by ViewportFBO structs.
        for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
            for (int i = 0; i < MAX_VIEWPORT_FBOS; i++) {
                ViewportFBO& vfbo = viewportFBOs_[f][i];
                if (vfbo.colorTexId) textures_.erase(vfbo.colorTexId);
                if (vfbo.depthTexId) textures_.erase(vfbo.depthTexId);
            }

        // Descriptor sets are gone; safe to destroy textures and their samplers.
        for (auto& [id, tex] : textures_)
        {
            VK_DESTROY(vkDestroySampler, device, tex.sampler);
            VK_DESTROY(vkDestroyImageView, device, tex.view);
            VK_DESTROY(vkDestroyImage, device, tex.image);
            VK_FREE_MEMORY(device, tex.memory);
        }

        // Flush deferred texture deletions
        auto destroyTexInfo = [](VkDevice dev, VkTextureInfo& tex) {
            VK_DESTROY(vkDestroySampler, dev, tex.sampler);
            VK_DESTROY(vkDestroyImageView, dev, tex.view);
            VK_DESTROY(vkDestroyImage, dev, tex.image);
            VK_FREE_MEMORY(dev, tex.memory);
        };
        pendingDeleteTextures_.drainAllWith(device, destroyTexInfo);

        // Destroy dummy texture
        VK_DESTROY(vkDestroySampler, device, dummyTexture_.sampler);
        VK_DESTROY(vkDestroyImageView, device, dummyTexture_.view);
        VK_DESTROY(vkDestroyImage, device, dummyTexture_.image);
        VK_FREE_MEMORY(device, dummyTexture_.memory);

        // Drain any remaining deferred sampler deletions
        pendingDeleteSamplers_.drainAll<vkDestroySampler>(device);

        // Destroy per-frame lighting UBO buffers (layouts/pools already destroyed above)
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (lightingUBOMapped_[i]) {
                vkUnmapMemory(device, lightingUBOMemory_[i]);
                lightingUBOMapped_[i] = nullptr;
            }
            VK_DESTROY(vkDestroyBuffer, device, lightingUBOBuffer_[i]);
            VK_FREE_MEMORY(device, lightingUBOMemory_[i]);
        }

        DestroyViewportFBOs();
        vulkanQueue_.Destroy();

        rVulkanShader::Destroy(device, vertShader_);
        rVulkanShader::Destroy(device, vertShaderInstanced_);
        rVulkanShader::Destroy(device, fragShader_);
        rVulkanShader::Destroy(device, fragShaderEmissive_);

        postProcess_.Destroy();
        pipelineManager_.Destroy();

        // Command pool destroyed LAST — after all resources that command buffers
        // referenced. Destroying it earlier causes Apple's kosmickrisp driver to
        // leak internal CmdBindDescriptorSets allocations.
        if (commandPool_)
            vkDestroyCommandPool(device, commandPool_, nullptr);

        framebuffer_.Destroy(device);
        swapchain_.Destroy(device);
        context_.Shutdown();
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
                fragShader_ = rVulkanShader::LoadFromMemory(
                    device, spvPlain.data(), spvPlain.size() * sizeof(uint32_t));
            if (fragShader_ == VK_NULL_HANDLE)
                std::cerr << "[Vulkan] Fragment compile failed (plain):\n" << fragErr << "\n";

            VK_LOG_INFO("[Vulkan] Compiling frag (emissive): " << fragPathStr << std::endl);
            std::string fragErrEm;
            auto spvEmissive = rVulkanShader::CompileGLSLFromFile(
                fragPathStr, rVulkanShader::Stage::Fragment, includePaths,
                {{"USE_EMISSIVE_OUT", "1"}}, &fragErrEm);
            if (!spvEmissive.empty())
                fragShaderEmissive_ = rVulkanShader::LoadFromMemory(
                    device, spvEmissive.data(), spvEmissive.size() * sizeof(uint32_t));
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
        std::cerr << "[Vulkan] GPU maxPushConstantsSize="
                  << context_.GetDeviceProperties().limits.maxPushConstantsSize
                  << " < 192 required" << std::endl;
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
                               pipelineSetLayouts, 2, &layout))
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
            if (vkCreateBuffer(context_.GetDevice(), &bufInfo, nullptr, &lightingUBOBuffer_[i]) != VK_SUCCESS)
                return false;

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(context_.GetDevice(), lightingUBOBuffer_[i], &memReqs);
            VkMemoryAllocateInfo memAlloc{};
            memAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAlloc.allocationSize  = memReqs.size;
            memAlloc.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(context_.GetDevice(), &memAlloc, nullptr, &lightingUBOMemory_[i]) != VK_SUCCESS)
                return false;
            vkBindBufferMemory(context_.GetDevice(), lightingUBOBuffer_[i], lightingUBOMemory_[i], 0);
            vkMapMemory(context_.GetDevice(), lightingUBOMemory_[i], 0, sizeof(LightingUBO), 0, &lightingUBOMapped_[i]);
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

    VK_LOG_INFO("[Vulkan] Renderer initialized: " << context_.GetDeviceName() << std::endl);
    return true;
}

bool vkRenderer::RecreateSwapchain(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    vkDeviceWaitIdle(context_.GetDevice());
    vulkanQueue_.CompactIfNeeded(context_.GetDevice());

    // Destroy viewport FBOs — they're sized to viewport dimensions which change
    DestroyViewportFBOs();

    // Save old render pass handle before destroying — we need to invalidate
    // only those cached pipelines, not the entire cache.
    VkRenderPass oldRenderPass = framebuffer_.GetRenderPass();

    // Clear stale render pass copies in the post-process BEFORE destroying
    // the framebuffer — the PP's composite passes and swapchainRenderPass_
    // may hold the same handle that framebuffer_.Destroy() is about to free.
    postProcess_.ClearRenderPassRefs(oldRenderPass);

    framebuffer_.Destroy(context_.GetDevice());

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
    if (postProcess_.IsEnabled())
    {
        const char* effectName = sr_vkPostProcessEffect();
        if (effectName && *effectName)
            postProcess_.SetActiveEffect(effectName);
    }

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
    if (vkCreateImage(device, &imageInfo, nullptr, &dummyTexture_.image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, dummyTexture_.image, &memReqs);
    uint32_t memType = context_.FindMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &dummyTexture_.memory) != VK_SUCCESS)
    {
        vkDestroyImage(device, dummyTexture_.image, nullptr);
        dummyTexture_.image = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(device, dummyTexture_.image, dummyTexture_.memory, 0);

    // Upload via staging buffer
    rVulkanBuffer staging;
    rVulkanBufferManager::CreateStagingBuffer(context_, whitePixel, 4, staging);

    VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = dummyTexture_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, dummyTexture_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
    rVulkanBufferManager::DestroyBuffer(device, staging);

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
        vkFreeMemory(device, dummyTexture_.memory, nullptr);
        vkDestroyImage(device, dummyTexture_.image, nullptr);
        dummyTexture_.memory = VK_NULL_HANDLE;
        dummyTexture_.image  = VK_NULL_HANDLE;
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
        vkDestroyImageView(device, dummyTexture_.view, nullptr);
        vkFreeMemory(device, dummyTexture_.memory, nullptr);
        vkDestroyImage(device, dummyTexture_.image, nullptr);
        dummyTexture_.view   = VK_NULL_HANDLE;
        dummyTexture_.memory = VK_NULL_HANDLE;
        dummyTexture_.image  = VK_NULL_HANDLE;
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

    // Honour deferred shader reload. Multiple requests (moviepack deactivate +
    // activate) are coalesced into one rebuild here at the frame boundary.
    if (pendingShaderReload_)
    {
        pendingShaderReload_ = false;
        DoReloadShaders();
        if (!IsInitialized()) return;
    }

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

    // Pick up any pending post-process config changes from POST_PROCESS_ENABLED /
    // POST_PROCESS_EFFECT. The dirty flag is set by tConfItem callbacks on the
    // console thread; we apply on the render thread at a safe point (before
    // acquiring the next swapchain image).
    if (sr_vkPostProcessConfigTakeDirty())
    {
        const char* effectName = sr_vkPostProcessEffect();
        if (effectName && *effectName)
            postProcess_.SetActiveEffect(effectName);
        postProcess_.SetEnabled(sr_vkPostProcessEnabled());
    }
#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // On macOS, force post-process ON every frame. The offscreen target needs
    // valid dimensions, so we pass the current swapchain extent. SetEnabled's
    // early return prevents redundant rebuilds once it's built.
    if (!postProcess_.IsEnabled())
    {
        auto ext = swapchain_.GetExtent();
        postProcess_.OnSwapchainResized(context_, ext.width, ext.height,
                                        framebuffer_.GetRenderPass());
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
        std::cerr << "[Vulkan] vkWaitForFences error: " << fenceResult << "\n";
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

    // Drain textures deferred for deletion — this slot's previous frame has now completed.
    pendingDeleteTextures_.drainWith(device, currentFrame_, [this](VkDevice dev, VkTextureInfo& tex) {
        // Invalidate cached descriptor sets first — they reference the
        // sampler and view.
        if (tex.view) descriptorManager_.InvalidateCache(tex.view);
        VK_DESTROY(vkDestroySampler, dev, tex.sampler);
        VK_DESTROY(vkDestroyImageView, dev, tex.view);
        VK_DESTROY(vkDestroyImage, dev, tex.image);
        VK_FREE_MEMORY(dev, tex.memory);
    });

    // Drain deferred sampler deletions (from TexParameter sampler changes)
    pendingDeleteSamplers_.drain<vkDestroySampler>(device, currentFrame_);

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
        clearValues[2].depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = 3;
    }
    else
    {
        clearValues[1].depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = 2;  // swapchain: 2 attachments
    }
    rpInfo.pClearValues = clearValues;

    // === Shadow map pass (before main render pass) ===
    // Uses shadow vertices collected from the PREVIOUS frame (one-frame lag, imperceptible).
    // Must run before the main render pass so shadow maps are ready for sampling.
    if (sr_shadowMode == rSHADOW_MAP)
    {
        if (!shadowMapsCreated_)
            CreateShadowMaps();
        if (shadowMapsCreated_)
        {
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

        VkViewport vp{};
        vp.x = static_cast<float>(x);
        vp.y = static_cast<float>(vkY);
        vp.width = static_cast<float>(w);
        vp.height = static_cast<float>(h);
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
    if (!frameStarted_) return;

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    vkCmdEndRenderPass(cmd);

    // Run the post-process pass ONLY if the scene actually rendered into the
    // offscreen target this frame. BeginFrame sets this based on
    // s_lastFrameWasInGame — we must use the same gate here so menu/title
    // frames that rendered straight to the swapchain don't also try to run
    // post-process (which would sample an uninitialized offscreen image).
    const bool postProcessActive = postProcess_.IsEnabled() && s_lastFrameWasInGame;
    if (postProcessActive)
    {
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

void vkRenderer::Ortho(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar)
{
    currentStack_->Ortho(left, right, bottom, top, zNear, zFar);
}

void vkRenderer::Frustum(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar)
{
    // GLM frustum
    glm::mat4 f = glm::frustum(static_cast<float>(left), static_cast<float>(right),
                                static_cast<float>(bottom), static_cast<float>(top),
                                static_cast<float>(zNear), static_cast<float>(zFar));
    currentStack_->Mult(glm::value_ptr(f));
}

void vkRenderer::Perspective(REAL fovy, REAL aspect, REAL zNear, REAL zFar)
{
    currentStack_->Perspective(fovy, aspect, zNear, zFar);
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
void vkRenderer::DepthFunc(int func) { depthFunc_ = func; }
void vkRenderer::DepthMask(bool write) { depthWriteEnabled_ = write; }
// Match GL convention directly: game-CW → Vulkan CW, game-CCW → Vulkan CCW.
// Even though the vertex shader applies gl_Position.y = -gl_Position.y, the
// Y-flip does NOT swap effective winding for culling purposes in practice:
// passing CCW through unchanged produces correct front-face selection for
// this project's meshes. (Verified empirically — inverting produced the
// wrong culling side.)
void vkRenderer::FrontFace(int mode) { frontFaceCW_ = (mode == 0x0900); } // GL_CW (0x0900) → Vulkan CW

// Dead code — zero call sites, kept as empty overrides for interface compliance
void vkRenderer::PolygonOffset(REAL factor, REAL units)
{
    if (!frameStarted_) return;
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    // OpenGL: depthBias = factor * maxSlope + units * minResolvable
    // Vulkan: depthBias = slopeFactor * maxSlope + constantFactor * minResolvable [+ clamp]
    // Mapping: slopeFactor = factor, constantFactor = units
    vkCmdSetDepthBias(cmd, static_cast<float>(units), 0.0f, static_cast<float>(factor));
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

        VkViewport vp{};
        vp.x = static_cast<float>(x);
        vp.y = static_cast<float>(vkY);
        vp.width = static_cast<float>(w);
        vp.height = static_cast<float>(h);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        // Keep scissor in sync with viewport, clamped to valid Vulkan bounds
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
    if (!frameStarted_) return;
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
    if (!frameStarted_) return;
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
    // leaving stale depth=1.0 that passes LEQUAL for any subsequent draw.
    if (depth)
    {
        VkClearAttachment clr{};
        clr.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clr.clearValue.depthStencil = {1.0f, 0};

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

    // Defer GPU resource destruction until the current frame slot's previous
    // use is guaranteed finished (after MAX_FRAMES_IN_FLIGHT frames).
    pendingDeleteTextures_.queue(std::move(it->second), currentFrame_);
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
        VkDevice device = context_.GetDevice();

        // Invalidate cached descriptor set that references the old sampler
        if (tex.view != VK_NULL_HANDLE)
            descriptorManager_.InvalidateCache(tex.view);

        // Defer sampler destruction — in-flight command buffers may still reference it
        if (tex.sampler != VK_NULL_HANDLE)
            pendingDeleteSamplers_.queue(tex.sampler, currentFrame_);

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = tex.magFilter;
        si.minFilter = tex.minFilter;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = tex.wrapS;
        si.addressModeV = tex.wrapT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        // If the min filter doesn't request mipmapping (GL_LINEAR or GL_NEAREST
        // without _MIPMAP_), clamp maxLod to 0 to prevent sampling stale higher
        // mip levels. This is critical for font atlases that use TexSubImage2D
        // to update only mip level 0 — higher levels contain stale/zero data.
        si.maxLod = tex.usesMipmapFilter
            ? ((tex.mipLevels > 1) ? static_cast<float>(tex.mipLevels - 1) : 1.0f)
            : 0.25f;
        vkCreateSampler(device, &si, nullptr, &tex.sampler);
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
            // until the current frame slot has cycled through all in-flight frames.
            if (it->second.view)
                descriptorManager_.InvalidateCache(it->second.view);
            pendingDeleteTextures_.queue(std::move(it->second), currentFrame_);
            textures_.erase(it);
        }
    }

    // Build all Vulkan resources into LOCAL variables (not map references)
    VkImage newImage = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    VkImageView newView = VK_NULL_HANDLE;
    VkSampler newSampler = VK_NULL_HANDLE;

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

    if (vkCreateImage(device, &imageInfo, nullptr, &newImage) != VK_SUCCESS)
        return;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, newImage, &memReqs);
    uint32_t memType = context_.FindMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) { vkDestroyImage(device, newImage, nullptr); return; }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &newMemory) != VK_SUCCESS)
    { vkDestroyImage(device, newImage, nullptr); return; }
    vkBindImageMemory(device, newImage, newMemory, 0);

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
        rVulkanBufferManager::CreateStagingBuffer(context_, rgbaData.data(), imageSize, staging);

        VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

        // Transition ALL mip levels UNDEFINED → TRANSFER_DST
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = newImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy staging buffer → mip level 0
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Generate mip levels by blitting each level from the previous
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
        rVulkanBufferManager::DestroyBuffer(device, staging);
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
        vkDestroyImage(device, newImage, nullptr);
        vkFreeMemory(device, newMemory, nullptr);
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

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = savedMagFilter;
    samplerInfo.minFilter = savedMinFilter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = savedWrapS;
    samplerInfo.addressModeV = savedWrapT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = savedUsesMipmap
        ? static_cast<float>(mipLevels - 1)
        : 0.25f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &newSampler) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create texture sampler for texId=" << texId << std::endl;
        vkDestroyImageView(device, newView, nullptr);
        vkDestroyImage(device, newImage, nullptr);
        vkFreeMemory(device, newMemory, nullptr);
        return;
    }

    // NOW write everything to the map with a FRESH lookup (safe from rehash)
    VkTextureInfo newInfo{};
    newInfo.image = newImage;
    newInfo.memory = newMemory;
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
    // Reuse the already-converted RGBA data from the staging upload (avoids double conversion)
    newInfo.cpuData = std::move(rgbaData);
    textures_[texId] = std::move(newInfo);
}

// Known limitation: TexSubImage2D re-uploads the entire texture even for small
// sub-region updates. It patches cpuData_ then stages and transfers the full image.
// This is acceptable for the current use cases (font atlas pages, small UI textures)
// where sub-updates are infrequent. If truly dynamic textures are ever needed,
// replace this with a proper sub-region staging copy using VkBufferImageCopy with
// the exact imageOffset/imageExtent matching the dirty rectangle.
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

    // Extract the updated sub-region into a tightly-packed staging buffer.
    std::vector<uint8_t> subData(subSize);
    for (int row = 0; row < height; ++row)
        memcpy(&subData[row * width * 4],
               &tex.cpuData[((yoffset + row) * texW + xoffset) * 4],
               static_cast<size_t>(width) * 4);

    rVulkanBuffer staging;
    if (!rVulkanBufferManager::CreateStagingBuffer(context_, subData.data(), subSize, staging))
        return;

    VkCommandBuffer cmd = rVulkanBufferManager::BeginSingleTimeCommands(device, commandPool_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {xoffset, yoffset, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    rVulkanBufferManager::EndSingleTimeCommands(device, commandPool_, context_.GetGraphicsQueue(), cmd);
    rVulkanBufferManager::DestroyBuffer(device, staging);
    tex.dirty = false;

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

    // Create staging buffer
    VkDeviceSize bufferSize = width * height * 4; // RGBA
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        return;
    }
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

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
    void* mapped;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &mapped);
    const uint8_t* src = static_cast<const uint8_t*>(mapped);
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
    vkUnmapMemory(device, stagingMemory);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
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
    if (vfbo.colorView)   { vkDestroyImageView(device, vfbo.colorView, nullptr);   vfbo.colorView = VK_NULL_HANDLE; }
    if (vfbo.colorImage)  { vkDestroyImage(device, vfbo.colorImage, nullptr);       vfbo.colorImage = VK_NULL_HANDLE; }
    if (vfbo.colorMemory) { vkFreeMemory(device, vfbo.colorMemory, nullptr);        vfbo.colorMemory = VK_NULL_HANDLE; }
    if (vfbo.emissiveView)   { vkDestroyImageView(device, vfbo.emissiveView, nullptr);   vfbo.emissiveView = VK_NULL_HANDLE; }
    if (vfbo.emissiveImage)  { vkDestroyImage(device, vfbo.emissiveImage, nullptr);       vfbo.emissiveImage = VK_NULL_HANDLE; }
    if (vfbo.emissiveMemory) { vkFreeMemory(device, vfbo.emissiveMemory, nullptr);        vfbo.emissiveMemory = VK_NULL_HANDLE; }
    if (vfbo.depthView)   { vkDestroyImageView(device, vfbo.depthView, nullptr);   vfbo.depthView = VK_NULL_HANDLE; }
    if (vfbo.depthImage)  { vkDestroyImage(device, vfbo.depthImage, nullptr);       vfbo.depthImage = VK_NULL_HANDLE; }
    if (vfbo.depthMemory) { vkFreeMemory(device, vfbo.depthMemory, nullptr);        vfbo.depthMemory = VK_NULL_HANDLE; }
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
    if (vkCreateImage(device, &colorInfo, nullptr, &vfbo.colorImage) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, vfbo.colorImage, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &vfbo.colorMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, vfbo.colorImage, vfbo.colorMemory, 0);

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
        if (vkCreateImage(device, &emInfo, nullptr, &vfbo.emissiveImage) != VK_SUCCESS) return false;
        vkGetImageMemoryRequirements(device, vfbo.emissiveImage, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &vfbo.emissiveMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, vfbo.emissiveImage, vfbo.emissiveMemory, 0);
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
    if (vkCreateImage(device, &depthInfo, nullptr, &vfbo.depthImage) != VK_SUCCESS) return false;

    vkGetImageMemoryRequirements(device, vfbo.depthImage, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &vfbo.depthMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, vfbo.depthImage, vfbo.depthMemory, 0);

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
            if (vfbo.framebuffer) vkDestroyFramebuffer(device, vfbo.framebuffer, nullptr);
            if (vfbo.colorView)   vkDestroyImageView(device, vfbo.colorView, nullptr);
            if (vfbo.colorImage)  vkDestroyImage(device, vfbo.colorImage, nullptr);
            if (vfbo.colorMemory) vkFreeMemory(device, vfbo.colorMemory, nullptr);
            if (vfbo.emissiveView)   vkDestroyImageView(device, vfbo.emissiveView, nullptr);
            if (vfbo.emissiveImage)  vkDestroyImage(device, vfbo.emissiveImage, nullptr);
            if (vfbo.emissiveMemory) vkFreeMemory(device, vfbo.emissiveMemory, nullptr);
            if (vfbo.depthView)   vkDestroyImageView(device, vfbo.depthView, nullptr);
            if (vfbo.depthImage)  vkDestroyImage(device, vfbo.depthImage, nullptr);
            if (vfbo.depthMemory) vkFreeMemory(device, vfbo.depthMemory, nullptr);
            if (vfbo.sampler)      vkDestroySampler(device, vfbo.sampler, nullptr);
            if (vfbo.depthSampler) vkDestroySampler(device, vfbo.depthSampler, nullptr);
            if (vfbo.colorTexId) {
                if (vfbo.colorView) descriptorManager_.InvalidateCache(vfbo.colorView);
                textures_.erase(vfbo.colorTexId);
            }
            if (vfbo.depthTexId) {
                if (vfbo.depthView) descriptorManager_.InvalidateCache(vfbo.depthView);
                textures_.erase(vfbo.depthTexId);
            }
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
        if (vkCreateImage(device, &imgInfo, nullptr, &sm.depthImage) != VK_SUCCESS) return false;

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, sm.depthImage, &memReqs);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &sm.depthMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, sm.depthImage, sm.depthMemory, 0);

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
        vkAllocateCommandBuffers(device, &allocCmdInfo, &transCmd);

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
        if (sm.depthImage)  { vkDestroyImage(device, sm.depthImage, nullptr);         sm.depthImage = VK_NULL_HANDLE; }
        if (sm.depthMemory) { vkFreeMemory(device, sm.depthMemory, nullptr);          sm.depthMemory = VK_NULL_HANDLE; }
        if (sm.sampler)     { vkDestroySampler(device, sm.sampler, nullptr);          sm.sampler = VK_NULL_HANDLE; }
    }

    if (shadowPipeline_)       { vkDestroyPipeline(device, shadowPipeline_, nullptr);             shadowPipeline_ = VK_NULL_HANDLE; }
    if (shadowPipelineLayout_) { vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr);  shadowPipelineLayout_ = VK_NULL_HANDLE; }
    if (shadowRenderPass_)     { vkDestroyRenderPass(device, shadowRenderPass_, nullptr);          shadowRenderPass_ = VK_NULL_HANDLE; }
    rVulkanShader::Destroy(device, shadowVertShader_);
    rVulkanShader::Destroy(device, shadowFragShader_);

    shadowMapsCreated_ = false;
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

    if (shadowStagingSize_[frame] < bufSize)
    {
        // Destroy old buffer (safe: fence for this frame slot was waited on in BeginFrame)
        if (shadowStagingBuf_[frame]) vkDestroyBuffer(device, shadowStagingBuf_[frame], nullptr);
        if (shadowStagingMem_[frame]) vkFreeMemory(device, shadowStagingMem_[frame], nullptr);
        shadowStagingBuf_[frame] = VK_NULL_HANDLE;
        shadowStagingMem_[frame] = VK_NULL_HANDLE;

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = bufSize;
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufInfo, nullptr, &shadowStagingBuf_[frame]) != VK_SUCCESS) return;

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, shadowStagingBuf_[frame], &memReqs);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_.FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &shadowStagingMem_[frame]) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, shadowStagingBuf_[frame], nullptr);
            shadowStagingBuf_[frame] = VK_NULL_HANDLE;
            return;
        }
        vkBindBufferMemory(device, shadowStagingBuf_[frame], shadowStagingMem_[frame], 0);
        shadowStagingSize_[frame] = bufSize;
    }

    // Copy vertex data (static first, then dynamic)
    void* mapped;
    vkMapMemory(device, shadowStagingMem_[frame], 0, bufSize, 0, &mapped);
    size_t staticBytes = staticVerts.size() * sizeof(rVertex20);
    size_t dynamicBytes = dynamicVerts.size() * sizeof(rVertex20);
    if (staticBytes > 0)
        memcpy(mapped, staticVerts.data(), staticBytes);
    if (dynamicBytes > 0)
        memcpy(static_cast<char*>(mapped) + staticBytes, dynamicVerts.data(), dynamicBytes);
    vkUnmapMemory(device, shadowStagingMem_[frame]);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &shadowStagingBuf_[frame], &offset);

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
    if (!frameStarted_) return;

    // Create/resize FBO if needed (per-frame to avoid race with in-flight frames)
    if (!CreateViewportFBO(currentFrame_, index, w, h)) return;
    viewportFBOCount_ = std::max(viewportFBOCount_, index + 1);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // End whatever render pass is active (main or previous viewport FBO)
    vkCmdEndRenderPass(cmd);

    // Begin viewport FBO render pass (clears color and depth)
    ViewportFBO& vfbo = viewportFBOs_[currentFrame_][index];
    VkClearValue clears[2]{};
    clears[0].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
    clears[1].depthStencil = {1.0f, 0};

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

    // Set viewport/scissor to full FBO size (not screen sub-region)
    VkViewport vp{};
    vp.width = static_cast<float>(w);
    vp.height = static_cast<float>(h);
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
        depthClear.clearValue.depthStencil = {1.0f, 0};
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
    clears[2].depthStencil = {1.0f, 0};

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

    VkViewport vp{};
    vp.width = static_cast<float>(swapchain_.GetExtent().width);
    vp.height = static_cast<float>(swapchain_.GetExtent().height);
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
void sr_DrawInstancedModelMesh(const void* geometryKey,
                               const rInstanceData* instances, size_t instanceCount,
                               unsigned int textureId)
{
    if (s_vkRenderer) s_vkRenderer->DrawInstancedModelMesh(geometryKey, instances, instanceCount, textureId);
}

bool sr_IsModelMeshCached(const void* geometryKey)
{
    if (!s_vkRenderer || !geometryKey) return false;
    return s_vkRenderer->IsModelMeshCached(geometryKey);
}

bool vkRenderer::IsModelMeshCached(const void* geometryKey) const
{
    auto key = reinterpret_cast<uintptr_t>(geometryKey);
    auto it = modelMeshCache_.find(key);
    return it != modelMeshCache_.end() && !it->second.litVerts.empty();
}

uint32_t sr_GetModelMeshCacheVersion_impl()
{
    return s_vkRenderer ? s_vkRenderer->GetModelMeshCacheVersion() : 0;
}

static int s_savedScreenW = 0, s_savedScreenH = 0;

void sr_BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h)
{
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
    if (!frameStarted_) return;

    const rVertex20* verts = static_cast<const rVertex20*>(vertices);
    const rRenderStateKey& state = *static_cast<const rRenderStateKey*>(stateKey);
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkPushConstants pc{};
    BuildPushConstants(pc, state, false);
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    vulkanQueue_.DrawTriangles(cmd, verts, vertexCount, state,
                               pipelineManager_, descSet,
                               &pc, sizeof(pc), cullFaceEnabled_, frontFaceCW_, 0xF);
}

void vkRenderer::DrawBatchLitTriangles(const void* vertices, size_t vertexCount,
                                        const void* stateKey)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) return;

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

void vkRenderer::DrawModelMesh(const std::vector<rModelVertex>& vertices,
                               const std::vector<unsigned int>& indices,
                               unsigned int textureId)
{
    if (!frameStarted_) BeginFrame();
    if (!frameStarted_) return;
    if (vertices.empty()) return;

    // ----- Cache: convert rModelVertex → rVertexLit32 once per unique mesh -----
    auto cacheKey = reinterpret_cast<uintptr_t>(vertices.data());
    auto& entry = modelMeshCache_[cacheKey];

    if (entry.litVerts.empty())
    {
        // Find the maximum absolute texcoord so we can normalize to [-1, 1] for int16 packing
        float maxTC = 1.0f;
        auto scanTC = [&maxTC](const rModelVertex& v) {
            maxTC = std::max(maxTC, std::abs(v.texcoord[0]));
            maxTC = std::max(maxTC, std::abs(v.texcoord[1]));
        };
        if (!indices.empty())
            for (unsigned int idx : indices)
                if (idx < vertices.size()) scanTC(vertices[idx]);
        else
            for (const auto& v : vertices) scanTC(v);

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
    // This MUST be in push constants (not UBO) because each model part (body, wheels)
    // has a different modelview, and the UBO is shared across all draws in a frame.
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

    vulkanQueue_.DrawLitTriangles(cmd, entry.litVerts.data(), entry.litVerts.size(),
                                   state, pipelineManager_, descSet,
                                   &pc, sizeof(pc),
                                   cullFaceEnabled_, frontFaceCW_, 0xF);

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

void vkRenderer::DrawInstancedModelMesh(const void* geometryKey,
                                        const rInstanceData* instances, size_t instanceCount,
                                        unsigned int textureId)
{
    if (!frameStarted_ || !geometryKey || instanceCount == 0) return;

    // Look up cached geometry by key
    auto cacheKey = reinterpret_cast<uintptr_t>(geometryKey);
    auto it = modelMeshCache_.find(cacheKey);
    if (it == modelMeshCache_.end() || it->second.litVerts.empty())
    {
        static int s_warnCount = 0;
        if (s_warnCount++ < 10)
            std::cerr << "[Vulkan] DrawInstancedModelMesh: cache miss for key 0x"
                      << std::hex << cacheKey << std::dec
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
        cullFaceEnabled_, frontFaceCW_);

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
    if (!frameStarted_) return;

    const rVertex20* verts = static_cast<const rVertex20*>(vertices);
    const rRenderStateKey& state = *static_cast<const rRenderStateKey*>(stateKey);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkPushConstants pc{};
    BuildPushConstants(pc, state, false);
    VkDescriptorSet descSet = LookupTextureDescriptor(state);

    vulkanQueue_.DrawLines(cmd, verts, vertexCount, state,
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
    if (!IsInitialized())
    {
        VK_LOG_INFO("[Vulkan] ReloadShaders: not initialized, ignoring" << std::endl);
        return;
    }
    // Always defer to the next BeginFrame boundary. This coalesces multiple
    // rapid reload requests (e.g. moviepack deactivate + activate = 2 calls)
    // into a single pipeline rebuild at a clean frame boundary.
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
    VkShaderModule newVert         = VK_NULL_HANDLE;
    VkShaderModule newFrag         = VK_NULL_HANDLE;
    VkShaderModule newFragEmissive = VK_NULL_HANDLE;

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
            newFrag = rVulkanShader::LoadFromMemory(
                device, spvPlain.data(), spvPlain.size() * sizeof(uint32_t));
        if (newFrag == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: fragment compile failed (plain):\n" << err << "\n";

        std::string errEm;
        auto spvEmissive = rVulkanShader::CompileGLSLFromFile(
            fragPathStr, rVulkanShader::Stage::Fragment, includePaths,
            {{"USE_EMISSIVE_OUT", "1"}}, &errEm);
        if (!spvEmissive.empty())
            newFragEmissive = rVulkanShader::LoadFromMemory(
                device, spvEmissive.data(), spvEmissive.size() * sizeof(uint32_t));
        if (newFragEmissive == VK_NULL_HANDLE)
            std::cerr << "[Vulkan] ReloadShaders: fragment compile failed (emissive):\n" << errEm << "\n";
    }

    VK_LOG_INFO("[Vulkan] ReloadShaders: vert=" << static_cast<const char*>(vertSrcPath)
              << " frag=" << static_cast<const char*>(fragSrcPath)
              << " newVert=" << newVert << " newFrag=" << newFrag
              << " newFragEmissive=" << newFragEmissive << std::endl);
#else
    // No shaderc (Android): load pre-compiled SPIR-V from APK assets.
    newVert         = rVulkanShader::LoadFromFile(device, "shaders/uber.vert.spv");
    newFrag         = rVulkanShader::LoadFromFile(device, "shaders/uber.frag.spv");
    newFragEmissive = rVulkanShader::LoadFromFile(device, "shaders/uber.frag.emissive.spv");
#endif

    if (!newVert || !newFrag || !newFragEmissive)
    {
        std::cerr << "[Vulkan] ReloadShaders: shader compile failed — keeping current shaders\n";
        if (newVert)         vkDestroyShaderModule(device, newVert, nullptr);
        if (newFrag)         vkDestroyShaderModule(device, newFrag, nullptr);
        if (newFragEmissive) vkDestroyShaderModule(device, newFragEmissive, nullptr);
        return; // leave current shaders and pipeline intact
    }

    // New shaders loaded successfully.  Now swap them in.
    vkDeviceWaitIdle(device);
    vulkanQueue_.CompactIfNeeded(device);

    // Invalidate model mesh cache — models may have changed (moviepack switch)
    // and the pointer-based cache keys could alias freed memory.
    modelMeshCache_.clear();
    sr_modelCacheVersion++;
    ++modelMeshCacheVersion_;

    pipelineManager_.Destroy();
    if (vertShader_         != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertShader_, nullptr);
    if (fragShader_         != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShader_, nullptr);
    if (fragShaderEmissive_ != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShaderEmissive_, nullptr);
    vertShader_         = newVert;
    fragShader_         = newFrag;
    fragShaderEmissive_ = newFragEmissive;

    VkDescriptorSetLayout reloadSetLayouts[2] = { descriptorManager_.GetLayout(), lightingUBOLayout_ };
    VkPipelineLayout layout;
    bool initOk = pipelineManager_.Init(device, framebuffer_.GetRenderPass(),
                               vertShader_, fragShader_, fragShaderEmissive_,
                               reloadSetLayouts, 2, &layout);
    if (!initOk)
    {
        std::cerr << "[Vulkan] ReloadShaders: pipelineManager_.Init failed" << std::endl;
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
void sr_vkPostProcessOnMoviepackActivated(const char* moviepackName)
{
    if (s_vkRenderer)
        s_vkRenderer->GetPostProcess().OnMoviepackActivated(
            moviepackName ? std::string(moviepackName) : std::string());
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

#endif // DEDICATED
