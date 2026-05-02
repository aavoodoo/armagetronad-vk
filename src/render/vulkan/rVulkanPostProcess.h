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

#ifndef RVULKANPOSTPROCESS_H
#define RVULKANPOSTPROCESS_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "rFileWatcher.h"
#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

class rVulkanContext;
class rVulkanSwapchain;
class rVulkanPipelineManager;

//! Metadata for a single tunable shader parameter declared in the effect's .lua script.
struct rPostProcessParam
{
    enum Type { Float, Int, Vec4 };
    std::string name;
    std::string description;
    Type        type = Float;
    int         slot = 0;       // index into fparams[] or iparams[]
    float       defaults[4] = {0,0,0,0}; // vec4 uses [0..3], scalar uses [0]
    float       minVal = 0.0f;
    float       maxVal = 1.0f;
};

//! Maximum samplers any single post-process pass can bind at set 0.
//! Covers all realistic effects (bloom composite is the heaviest at 3-4).
//! Raise if a future effect needs more.
static constexpr int PP_MAX_SAMPLERS_PER_PASS = 4;

//! Maximum number of post-process passes a single effect can declare.
//! Covers multi-level bloom pyramids comfortably.
static constexpr int PP_MAX_PASSES_PER_EFFECT = 16;

//! Format of a framegraph intermediate resource.
enum class rPostProcessFormat
{
    RGBA8,    //!< VK_FORMAT_R8G8B8A8_UNORM  — default for LDR bloom, DoF, etc.
    RGBA16F,  //!< VK_FORMAT_R16G16B16A16_SFLOAT — HDR intermediate
    R8,       //!< VK_FORMAT_R8_UNORM — single-channel mask
};

//! One intermediate render target declared by an effect's .lua script.
//! Resolution is expressed as a fraction of the swapchain extent, so the
//! pool automatically reallocates on window resize without the effect
//! having to know about pixel sizes.
struct rPostProcessResourceDecl
{
    std::string        name;
    rPostProcessFormat format = rPostProcessFormat::RGBA8;
    float              scale  = 1.0f; // swapchain-relative (0.5 = half res)
};

//! One sampler binding declaration from an effect's .lua script.
//! `source` is either an intermediate resource name declared earlier in the
//! script, or one of the built-in names (SCENE_COLOR, SCENE_EMISSIVE,
//! SCENE_DEPTH). Resolved to a concrete VkImageView at effect load time.
struct rPostProcessSamplerDecl
{
    int         binding = 0;
    std::string source;
};

//! One pass declared by an effect's .lua script.
//! `shader` is a basename resolved to shaders/postprocess/<effect>/<shader>.frag
//! `target` is either an intermediate resource name or "SWAPCHAIN" (the final
//! composite pass).
struct rPostProcessPassDecl
{
    std::string shader;
    std::string target;
    std::vector<rPostProcessSamplerDecl> samplers;
};

//! Fully built effect descriptor, produced by running the effect's .lua script.
struct rPostProcessPipelineDesc
{
    std::vector<rPostProcessResourceDecl> resources;
    std::vector<rPostProcessPassDecl>     passes;
};

//! Owns a set of named intermediate render targets for a post-process effect.
//! The pool is populated from the effect's .lua RESOURCE declarations at
//! effect load time and reallocated on swapchain resize. Each resource owns its
//! VkImage, memory, and view. Destruction is idempotent — safe to call multiple
//! times and on partially-built pools after an allocation failure.
class rPostProcessResourcePool
{
public:
    struct Resource
    {
        VkImage       image      = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
        VkExtent2D     extent = {0, 0};
        VkFormat       format = VK_FORMAT_UNDEFINED;
    };

    rPostProcessResourcePool() = default;
    ~rPostProcessResourcePool() = default;

    //! Allocate (or reallocate) all declared resources. Destroys any existing
    //! allocations first. `baseExtent` is the swapchain extent — all resource
    //! extents are computed by multiplying by each declaration's scale.
    bool Allocate(rVulkanContext& ctx,
                  const std::vector<rPostProcessResourceDecl>& decls,
                  VkExtent2D baseExtent);

    //! Destroy all allocations. Safe to call on an empty pool.
    void Destroy(VmaAllocator allocator, VkDevice device);

    //! Look up a resource by name. Returns nullptr if not found.
    [[nodiscard]] const Resource* Get(const std::string& name) const;

    //! Expose the map for iteration (descriptor set binding, etc.).
    [[nodiscard]] const std::unordered_map<std::string, Resource>& All() const { return resources_; }

private:
    std::unordered_map<std::string, Resource> resources_;
};

//! One executable pass of a post-process effect. Built from a `rPostProcessPassDecl`
//! at effect load time; owned by the effect and destroyed with it.
//!
//! A pass has its own render pass, framebuffer, pipeline, and per-frame descriptor
//! set (because each pass's set 0 bindings reference different image views). The
//! pipeline layout is unique to this pass because the descriptor set layout depends
//! on the number of sampler bindings.
struct rPostProcessPass
{
    VkRenderPass          renderPass     = VK_NULL_HANDLE; // 1 color attachment, no depth
    VkFramebuffer         framebuffer    = VK_NULL_HANDLE; // points at the target resource
    VkShaderModule        fragShader     = VK_NULL_HANDLE; // fragment SPIR-V
    VkDescriptorSetLayout set0Layout     = VK_NULL_HANDLE; // sampler set, bindings 0..N
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE; // set0 + set1 (params) + pc
    VkPipeline            pipeline       = VK_NULL_HANDLE; // fragment + fullscreen vert
    VkDescriptorSet       descSet[2]     = {VK_NULL_HANDLE, VK_NULL_HANDLE}; // per frame-in-flight
    VkExtent2D            extent         = {0, 0};
    bool                  targetsSwapchain = false;
    uint32_t              samplerCount   = 0; // number of bindings populated

    std::string           debugShader;
    std::string           debugTarget;
};

//! Post-processing manager: renders the scene to an offscreen color+depth
//! target, then runs a fullscreen-triangle pass sampling that target through
//! a user-selected fragment shader, writing the result to the swapchain.
//!
//! When disabled (default), the scene renders directly to the swapchain with
//! zero overhead — the rVulkanPostProcess object exists but holds no GPU
//! resources until the first time the feature is enabled.
//!
//! This is the M1 skeleton: passthrough effect only, no parameter system,
//! no dynamic effect loading. The wider plan (JSON metadata, MVP_* params,
//! bloom/celshading effects, menu integration, moviepack integration) lands
//! in follow-up milestones.
class rVulkanPostProcess
{
public:
    rVulkanPostProcess() = default;
    ~rVulkanPostProcess() = default;

    //! Supply the scene pipeline manager so the post-process subsystem can
    //! register its offscreen render pass as having multiple color
    //! attachments. Required before the first SetEnabled(true) or visual
    //! corruption will result (pipelines created for the wrong attachment
    //! count). Typically called immediately after Init().
    void SetPipelineManager(rVulkanPipelineManager* pm) { pipelineManager_ = pm; }

    //! One-time init. Safe to call before the feature is ever enabled — only
    //! allocates minimal state. GPU resources are lazy-created on Enable().
    bool Init(rVulkanContext& ctx, VkFormat swapchainFormat, VkFormat depthFormat,
              VkRenderPass swapchainRenderPass);

    //! Tear down all resources.
    void Destroy();

    //! Destroy only descriptor pools (called before sampler destruction at shutdown)
    void DestroyDescriptorPools();

    //! Toggle post-processing on/off. Safe to call between frames.
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_ && offscreenBuilt_; }

    //! Clear all references to a render pass handle that's about to be destroyed.
    //! Called BEFORE framebuffer_.Destroy() to prevent double-free.
    void ClearRenderPassRefs(VkRenderPass rp);

    //! Called from RecreateSwapchain to rebuild offscreen images at the new size.
    //! If the feature is disabled, this is a no-op.
    //! @param oldSwapchainRP The render pass that was destroyed by framebuffer_.Destroy()
    //!        before this call — effect passes may hold stale copies of it.
    bool OnSwapchainResized(rVulkanContext& ctx, uint32_t width, uint32_t height,
                            VkRenderPass swapchainRenderPass,
                            VkRenderPass oldSwapchainRP = VK_NULL_HANDLE);

    //! Set the active effect by name. Unknown name falls back to passthrough.
    //! The first time a given effect is activated it gets lazily loaded from
    //! shaders/postprocess/<name>/ (fragment shader + optional .meta file).
    void SetActiveEffect(const std::string& name);
    const std::string& GetActiveEffect() const { return activeEffect_; }

    //! List of parameters exposed by the active effect (for menu integration).
    const std::vector<rPostProcessParam>& GetParams() const;

    //! Internal: called from the tSettingItem change callback thread when a
    //! user tunes an MVP_<EFFECT>_<PARAM> value. Just sets a sticky dirty
    //! flag — the next BeginFrame on the render thread re-applies the
    //! registry to currentParams_. Public only because the callback is a
    //! plain C function pointer, not a member function.
    void NotifyMvpChanged() { mvpDirty_ = true; }

    //! Called by the moviepack manager when a moviepack is activated. Scans
    //! the effects directory for .meta files, registers dynamic tSettingItems
    //! named MVP_<EFFECT>_<PARAM> for each declared parameter, and loads
    //! per-moviepack overrides from <userConfig>/moviepack_<name>.cfg.
    //!
    //! `moviepackName` is the moviepack directory name (e.g., "customBloom")
    //! or empty for the base game (persistence file becomes postprocess.cfg).
    //!
    //! These settings use tSettingItem (not tConfItem) so they never get
    //! written to user.cfg and cause no "unknown setting" warnings on launch.
    void OnMoviepackActivated(const std::string& moviepackName);

    //! Called by the moviepack manager when a moviepack is deactivated. Writes
    //! current MVP values to the per-moviepack cfg file, deletes all
    //! dynamically-registered tSettingItems, and clears the MVP registry.
    void OnMoviepackDeactivated();

    //! Accessors used by vkRenderer to redirect scene rendering.
    VkRenderPass  GetSceneRenderPass()  const { return offscreenRenderPass_; }
    VkFramebuffer GetSceneFramebuffer() const { return offscreenFramebuffer_; }

    //! Layout tracking: call when the scene render pass is about to begin.
    //! The offscreen render pass declares initialLayout=UNDEFINED, so we
    //! record that the images' content is discarded at this point.
    void NotifySceneRenderPassBeginning()
    {
        offscreenColorLayout_    = VK_IMAGE_LAYOUT_UNDEFINED;
        offscreenEmissiveLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        offscreenDepthLayout_    = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    //! Layout tracking: call after vkCmdEndRenderPass for the scene render pass.
    //! The offscreen render pass's finalLayout transitions each image to its
    //! read-optimal layout, ready to be sampled by the PP passes.
    void NotifySceneRenderPassEnded()
    {
        offscreenColorLayout_    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        offscreenEmissiveLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        offscreenDepthLayout_    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }

    //! Query tracked layouts (for barrier insertion and assertions).
    VkImageLayout GetOffscreenColorLayout()    const { return offscreenColorLayout_; }
    VkImageLayout GetOffscreenEmissiveLayout() const { return offscreenEmissiveLayout_; }
    VkImageLayout GetOffscreenDepthLayout()    const { return offscreenDepthLayout_; }

    //! VkImage handles for render graph resource registration.
    VkImage GetOffscreenColorImage()    const { return offscreenColorImage_; }
    VkImage GetOffscreenEmissiveImage() const { return offscreenEmissiveImage_; }
    VkImage GetOffscreenDepthImage()    const { return offscreenDepthImage_; }

    //! Execute the post-process pass. Called from EndFrame() after the scene
    //! render pass has ended. Transitions the offscreen color image into a
    //! shader-read layout (done implicitly by the offscreen render pass),
    //! begins the swapchain render pass, draws the fullscreen triangle, ends
    //! the swapchain render pass.
    //!
    //! `cmd` must be in a recording state with no active render pass.
    //! `swapchainFramebuffer` is the framebuffer for the current acquired image.
    void Execute(VkCommandBuffer cmd,
                 uint32_t frameInFlight,
                 VkFramebuffer swapchainFramebuffer,
                 VkExtent2D extent,
                 float time);

private:
    // Create all resources that depend on swapchain extent (offscreen images,
    // offscreen render pass, offscreen framebuffer, descriptor set pointing
    // at the offscreen color view). Called lazily on Enable() and on resize.
    bool BuildOffscreen(rVulkanContext& ctx, uint32_t width, uint32_t height);
    void DestroyOffscreen();

    // Create the pipeline resources that don't depend on swapchain extent
    // (descriptor set layouts, descriptor pool, pipeline layout, fullscreen
    // vertex shader, passthrough fragment shader, passthrough pipeline).
    // Called once from Init().
    bool BuildPipeline(rVulkanContext& ctx, VkRenderPass swapchainRenderPass);
    void DestroyPipeline();

    rVulkanContext*         ctx_ = nullptr;
    rVulkanPipelineManager* pipelineManager_ = nullptr; // not owned
    VkDevice device_ = VK_NULL_HANDLE;

    bool enabled_ = false;
    bool offscreenBuilt_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE; // borrowed, not owned

    // Offscreen color target
    VkImage       offscreenColorImage_      = VK_NULL_HANDLE;
    VmaAllocation offscreenColorAllocation_ = VK_NULL_HANDLE;
    VkImageView   offscreenColorView_       = VK_NULL_HANDLE;
    VkSampler      offscreenSampler_     = VK_NULL_HANDLE;
    VkImageLayout  offscreenColorLayout_    = VK_IMAGE_LAYOUT_UNDEFINED;

    // Offscreen emissive target — second color attachment written by the
    // uber fragment shader's emissive hooks. Default hook returns vec4(0),
    // so this is the accumulated "which pixels glow and how much" signal
    // that bloom samples instead of the (often dim) color buffer.
    VkImage       offscreenEmissiveImage_      = VK_NULL_HANDLE;
    VmaAllocation offscreenEmissiveAllocation_ = VK_NULL_HANDLE;
    VkImageView   offscreenEmissiveView_       = VK_NULL_HANDLE;
    VkImageLayout  offscreenEmissiveLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Offscreen depth target
    VkImage       offscreenDepthImage_      = VK_NULL_HANDLE;
    VmaAllocation offscreenDepthAllocation_ = VK_NULL_HANDLE;
    VkImageView   offscreenDepthView_       = VK_NULL_HANDLE;
    VkImageLayout  offscreenDepthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Offscreen render pass + framebuffer (scene renders here)
    VkRenderPass  offscreenRenderPass_  = VK_NULL_HANDLE;
    VkFramebuffer offscreenFramebuffer_ = VK_NULL_HANDLE;

    // Post-process pipeline (stable across swapchain resizes)
    //
    // The sampler set layout has PP_MAX_SAMPLERS_PER_PASS bindings. Every
    // pass uses this same layout regardless of how many samplers its shader
    // declares — unused bindings are filled with a placeholder view in
    // WritePassDescriptors. This means all passes can share one
    // VkPipelineLayout and one descriptor set layout, which keeps the code
    // simple and pipeline layout management cheap.
    VkDescriptorSetLayout samplerSetLayout_ = VK_NULL_HANDLE; // set 0: PP_MAX_SAMPLERS_PER_PASS samplers
    VkDescriptorSetLayout paramsSetLayout_  = VK_NULL_HANDLE; // set 1: params UBO
    VkPipelineLayout      pipelineLayout_   = VK_NULL_HANDLE; // set 0 + set 1 + push constants
    VkDescriptorPool      globalDescPool_   = VK_NULL_HANDLE; // owns paramsDescSet_
    VkShaderModule        fullscreenVert_   = VK_NULL_HANDLE;

    // --- Per-effect state ---

    //! One loaded effect: descriptor built from .lua script, allocated intermediate
    //! render targets, per-pass GPU resources, tunable parameters, and a
    //! per-effect descriptor pool owning the pass descriptor sets.
    struct Effect
    {
        rPostProcessPipelineDesc      desc;          // built from .lua script
        rPostProcessResourcePool      pool;          // intermediate render targets
        std::vector<rPostProcessPass> passes;        // one per pass, execution order
        std::vector<rPostProcessParam> params;       // declared in .lua script
        VkDescriptorPool              descriptorPool = VK_NULL_HANDLE;
    };

    // Copy an effect's parameter defaults into currentParams_. Extracted so
    // both SetActiveEffect and the deferred-load path in SetEnabled can use it.
    void SeedEffectDefaults(const Effect& ef);

    // Load an effect on demand. Returns a pointer into the effects_ map, or
    // nullptr on failure (script not found, syntax error, or GPU resource
    // allocation failure). The loader:
    //   1. Runs the effect's .lua script via tLuaState
    //   2. Allocates intermediate render targets from the pool
    //   3. Builds one rPostProcessPass per declared pass
    Effect* EnsureEffectLoaded(const std::string& name);

    // Build one pass (shader + render pass + framebuffer + pipeline + descriptor
    // set) from a PassDecl. `pool` is the effect's resource pool (used to
    // resolve intermediate target/source names). `descPool` is the effect's
    // descriptor pool. Returns false on any failure; the caller must call
    // DestroyPass on the output to clean up partial state.
    bool BuildPass(const std::string& effectName,
                   const rPostProcessPassDecl& decl,
                   rPostProcessResourcePool& pool,
                   VkDescriptorPool descPool,
                   rPostProcessPass& outPass);

    // Write the sampler bindings for a pass's descriptor set. Unused binding
    // slots are filled with offscreenColorView_ as a safe placeholder so
    // Vulkan validation doesn't complain about undefined descriptor contents.
    void WritePassDescriptors(rPostProcessPass& pass,
                              const rPostProcessPassDecl& decl,
                              const rPostProcessResourcePool& pool);

    // Create a fullscreen-triangle pipeline for a fragment shader targeting
    // a specific render pass. The pipeline layout, vertex shader, and render
    // state are all standard across all post-process passes.
    VkPipeline CreatePassPipeline(VkShaderModule frag, VkRenderPass renderPass);

    // Destroy a single pass's owned Vulkan objects. The descriptor set is
    // freed with the pool, not here. The render pass is only destroyed if
    // the pass doesn't target the swapchain (swapchain render pass is borrowed).
    void DestroyPass(rPostProcessPass& pass);

    // Destroy a single effect's owned Vulkan objects and clear its state.
    void DestroyEffect(Effect& ef);

    std::unordered_map<std::string, Effect> effects_;
    std::string activeEffect_ = "passthrough";
    Effect*     activeEffectPtr_ = nullptr;  // cached lookup (invalidated on Destroy)

    // --- Hot-reload (development only) ---
    rFileWatcher                      scriptWatcher_;
    std::unordered_set<std::string>   pendingReloads_;

    //! Force-reload a named effect: destroy cached version, re-run EnsureEffectLoaded.
    //! If reload fails, the effect is gone — Execute will fall back to passthrough.
    void HotReloadEffect(const std::string& name);

    // Parameter values currently applied to the active effect (written to UBO).
    //
    // CRITICAL std140 layout note: scalar arrays (float[N], int[N]) in std140
    // have 16-byte STRIDE per element — a `float fparams[32]` in GLSL
    // occupies 512 bytes, not 128. To keep C++ packing identical to GLSL
    // packing without wasting 75% of the UBO, we use vec4/ivec4 arrays on
    // both sides. The shader exposes them via FP(slot)/IP(slot) macros so
    // authors still see linear slot indexing [0..31] / [0..15].
    //
    //   fparams[8] vec4   = 128 B (float params, 32 scalar slots)
    //   iparams[4] ivec4  =  64 B (int params,   16 scalar slots)
    //   reserved[4] vec4  =  64 B
    //   total             = 256 B
    struct ParamsUBO
    {
        float fparams[8][4];  // std140-compatible: each row is a vec4
        int   iparams[4][4];  // std140-compatible: each row is an ivec4
        float _reserved[4][4];
    };
    static_assert(sizeof(ParamsUBO) == 256, "PostProcess params UBO must be 256 bytes");

    ParamsUBO      currentParams_{};
    VkBuffer      paramsBuffer_[2]     = {VK_NULL_HANDLE, VK_NULL_HANDLE};  // one per frame-in-flight
    VmaAllocation paramsAllocation_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    void*          paramsMapped_[2] = {nullptr, nullptr};
    VkDescriptorSet paramsDescSet_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    // --- MVP (moviepack variable) dynamic config items ---
    //
    // For each effect found in the active moviepack (or the base game if no
    // moviepack), we register dynamic tSettingItem<float>/<int> instances
    // named MVP_<EFFECT>_<PARAM>. Users can tune them via the in-game
    // console or (future) the post-processing menu.
    //
    // These are tSettingItems, not tConfItems, so they never get written to
    // user.cfg on shutdown. We persist them ourselves to a per-moviepack
    // config file on deactivation, and load them back on activation.
    //
    // The heap-allocated tSettingItems are stored as opaque `void*` here to
    // avoid including tConfiguration.h from this header — the casts happen
    // in the .cpp where the full type is available.
    struct MvpEntry
    {
        std::string effectName;  // e.g. "bloom"
        std::string paramName;   // e.g. "intensity"
        int         slot = 0;    // slot in the effect's UBO layout
        enum { Float, Int } type = Float;
        float       fValue = 0.0f;   // backing storage for tSettingItem<float>
        int         iValue = 0;      //                  for tSettingItem<int>
        float       minVal = 0.0f;
        float       maxVal = 1.0f;
        void*       confItem = nullptr; // heap-allocated tSettingItem<float>/<int>
    };
    // Map from "MVP_<EFFECT>_<PARAM>" (the tSettingItem title) → entry.
    std::unordered_map<std::string, MvpEntry> mvpRegistry_;

    //! Set by the MVP change callback (invoked from the config system thread
    //! when the user types an MVP_ value at the console). Picked up in
    //! BeginFrame to re-apply the registry to currentParams_ on the render
    //! thread without touching Vulkan state from the config thread.
    std::atomic<bool> mvpDirty_{false};

    //! Active moviepack name, stored so OnMoviepackDeactivated knows which
    //! file to write. Empty string means "no moviepack" / base game.
    std::string mvpMoviepackName_;

    // Register MVP settings for one effect (called when the effect loads
    // for the first time). Skips params that already have an entry in
    // mvpRegistry_ (from a previous effect load).
    void RegisterEffectMvp(const std::string& effectName,
                           const std::vector<rPostProcessParam>& params);

    // Destroy all MVP tSettingItems and clear the registry. Called from
    // OnMoviepackDeactivated and Destroy.
    void UnregisterAllMvp();

    // Apply the current MVP registry values to currentParams_ for the
    // active effect. Called from BeginFrame when mvpDirty_ is set.
    void ApplyMvpToCurrentParams();

    // Load MVP values from a per-moviepack config file.
    void LoadMvpConfigFile(const std::string& moviepackName);

    // Save MVP values to a per-moviepack config file.
    void SaveMvpConfigFile(const std::string& moviepackName) const;

    // Compute the cfg file path for a given moviepack.
    static std::string GetMvpConfigPath(const std::string& moviepackName);
};

#endif // DEDICATED
#endif // RVULKANPOSTPROCESS_H
