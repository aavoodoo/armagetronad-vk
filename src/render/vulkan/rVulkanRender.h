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

#ifndef RVULKANRENDER_H
#define RVULKANRENDER_H

#ifndef DEDICATED

#include "rRender.h"
#include "rVertex.h"
#include "rModelMesh.h"
#include "rMatrixStack.h"
#include "rVulkanContext.h"
#include "rVulkanRAII.h"
#include "rVulkanSwapchain.h"
#include "rVulkanFramebuffer.h"
#include "rVulkanBuffer.h"
#include "rVulkanPipeline.h"
#include "rVulkanDescriptor.h"
#include "rVulkanShader.h"
#include "rVulkanRenderQueue.h"
#include "rVulkanPostProcess.h"
#include <vector>
#include <unordered_map>
#include "rCycleRenderer.h"

//! Maximum lights (matches GL3 renderer).
//! Raising this above 2 requires: (1) expanding LightingUBO arrays (breaking std140 layout),
//! (2) recompiling uber.frag (loop count hardcoded to VK_MAX_LIGHTS), and
//! (3) updating any callers that pass light indices.
static constexpr int VK_MAX_LIGHTS = 2;
static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

//! Push constant data sent per-draw (192 bytes; MoltenVK supports 4096)
//!
//! texMatrix doubles as a per-draw flag carrier (slots not used by the texture
//! matrix are repurposed). In GLSL these are read as uTexMatrix[row][col]:
//!
//!   uTexMatrix[2][0]  — uTime          : float, current frame time in seconds
//!   uTexMatrix[2][1]  — uRenderContext  : int cast to float; identifies the draw
//!                                         context (e.g. Game3D_Floor=5) for hook dispatch
//!   uTexMatrix[2][2]  — uAlphaTest      : >0.5 enables alpha-test discard (font/HUD)
//!   uTexMatrix[2][3]  — uLightingFlag   : >0.5 selects lit vertex path; uses normalMatrix[3].rgb
//!                                         as per-draw material color
//!   uTexMatrix[3][3]  — uFontMode       : -fontMode (negative sentinel); 0=off, 1=SDF mono,
//!                                         2=MSDF, 3=MTSDF; uTexMatrix[0][0]=screenPxRange
//!
//! Adding a new per-draw flag: use a currently-unused cell in uTexMatrix[2..3][*].
//! If those run out, uTexMatrix[1][3] and uTexMatrix[0][3] are also available
//! (row 3 of a pure texture matrix is always (0,0,0,1)).
struct VkPushConstants
{
    float mvp[16];          // 64 bytes — model-view-projection matrix
    float texMatrix[16];    // 64 bytes — texture matrix + per-draw flags (see above)
    float normalMatrix[16]; // 64 bytes — modelview for normal transform (lit path only)
};

//! Vulkan renderer implementing the rRenderer abstract interface.
//! Drop-in replacement for gl3Renderer.
class vkRenderer : public rRenderer
{
public:
    vkRenderer();
    virtual ~vkRenderer();

    //! Initialize the Vulkan renderer
    [[nodiscard]] bool Init(SDL_Window* window);

    //! Reload shaders (e.g. when moviepack changes). Waits for GPU idle,
    //! destroys the current pipeline+shaders, searches for moviepack overrides,
    //! then recreates the pipeline. Safe to call between frames.
    void ReloadShaders();

    //! Begin a new frame (acquire swapchain image, begin command buffer)
    void BeginFrame() override;

    //! End the frame (end command buffer, submit to GPU, present)
    void EndFrame() override;

    //! Swap buffers (end frame + present)
    void SwapBuffers() override;

    //! Check if a frame is in progress
    bool IsFrameStarted() const { return frameStarted_; }

    //! Check if Vulkan is initialized
    bool IsInitialized() const { return context_.IsValid(); }

    //! Access the Vulkan context (for compute SDF generation, etc.)
    rVulkanContext& GetContext() { return context_; }

    //! Pause / resume rendering (iOS: called on WILL_ENTER_BACKGROUND / DID_ENTER_FOREGROUND)
    void SetAppInBackground(bool inBackground) { appInBackground_ = inBackground; }

    //! Request swapchain recreation at the start of the next frame.
    //! Called after returning from background so the Metal surface is re-queried.
    void SetNeedsSwapchainRecreation() { needsSwapchainRecreation_ = true; }

    //! Access the post-processing subsystem. Used by sr_vkPostProcess*
    //! free functions exposed to the moviepack manager.
    rVulkanPostProcess& GetPostProcess() { return postProcess_; }

    //! Get current command buffer (for recording draw commands)
    VkCommandBuffer GetCurrentCommandBuffer() const { return frameStarted_ ? commandBuffers_[currentFrame_] : VK_NULL_HANDLE; }

    // === Vertex operations (immediate mode emulation) ===
    void Vertex(REAL x, REAL y) override;
    void Vertex(REAL x, REAL y, REAL z) override;
    void Vertex3(REAL* x) override;
    void Vertex(REAL x, REAL y, REAL z, REAL w) override;
    void TexCoord(REAL u, REAL v) override;
    void TexCoord(REAL u, REAL v, REAL w) override;
    void TexCoord(REAL u, REAL v, REAL w, REAL t) override;
    void Color(REAL r, REAL g, REAL b) override;
    void Color(REAL r, REAL g, REAL b, REAL a) override;
    void End(bool force = true) override;

    // === Primitive begin ===
    void BeginLines() override;
    void BeginTriangles() override;
    void BeginQuads() override;
    void BeginLineStrip() override;
    void BeginTriangleStrip() override;
    void BeginQuadStrip() override;
    void BeginTriangleFan() override;
    void BeginLineLoop() override;
    void BeginPolygon() override;

    // === Matrix operations ===
    void ProjMatrix() override;
    void ModelMatrix() override;
    void TexMatrix() override;
    void PushMatrix() override;
    void PopMatrix() override;
    void MultMatrix(REAL mdata[4][4]) override;
    void MultMatrixFlat(const REAL* mdata) override;
    void LoadMatrixFlat(const REAL* mdata) override;
    void IdentityMatrix() override;
    void ScaleMatrix(REAL f) override;
    void ScaleMatrix(REAL f1, REAL f2, REAL f3) override;
    void TranslateMatrix(REAL x1, REAL x2, REAL x3) override;
    void RotateMatrix(REAL angle, REAL x, REAL y, REAL z) override;

    // === Projection ===
    void Ortho(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar) override;
    void Frustum(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar) override;
    void Perspective(REAL fovy, REAL aspect, REAL zNear, REAL zFar) override;
    void LookAt(REAL eyeX, REAL eyeY, REAL eyeZ,
                REAL centerX, REAL centerY, REAL centerZ,
                REAL upX, REAL upY, REAL upZ) override;

    // === State management ===
    void EnableState(int capability) override;
    void DisableState(int capability) override;
    void BlendFunc(int sfactor, int dfactor) override;
    void DepthFunc(int func) override;
    void DepthMask(bool write) override;
    void FrontFace(int mode) override;
    void PolygonOffset(REAL factor, REAL units) override;

    // === Viewport/Scissor ===
    void Viewport(int x, int y, int width, int height) override;
    void GetViewport(int viewport[4]) override;
    void Scissor(int x, int y, int width, int height) override;

    // === Clear ===
    void Clear(bool color, bool depth, bool stencil = false) override;
    void ClearColor(REAL r, REAL g, REAL b, REAL a) override;
    // === Lighting (software emulation) ===
    void Light(int light, int pname, const REAL* params) override;
    void Material(int face, int pname, const REAL* params) override;
    void Normal(REAL x, REAL y, REAL z) override;

    // === State queries ===
    void GetModelviewMatrix(float* matrix) override;
    void GetProjectionMatrix(float* matrix) override;
    void GetMVPMatrix(float* matrix) override;
    void GetColor(float* color) override;
    bool IsEnabled(int capability) override;

    // === Hints & Alpha test ===
    void Hint(int target, int mode) override;
    void AlphaFunc(int func, REAL ref) override;

    // === Texture operations ===
    void TexParameter(int target, int pname, int param) override;
    void BindTexture(int target, unsigned int texture) override;

    // === Texture lifecycle ===
    unsigned int GetBoundTexture2D() override;
    int GetMaxTextureSize() override;
    unsigned int GenTexture() override;
    void DeleteTexture(unsigned int texture) override;
    void TexImage2D(int target, int level, int internalFormat,
                    int width, int height, int border,
                    int format, int type, const void* data) override;
    void TexSubImage2D(int target, int level,
                       int xoffset, int yoffset,
                       int width, int height,
                       int format, int type, const void* data) override;
    void GenerateMipmap(int target) override;
    void PixelStorei(int pname, int param) override;
    int GetTexLevelParameteriv(int target, int level, int pname) override;

    // === System operations ===
    void Finish() override;
    void Flush() override;
    void ReadPixels(int x, int y, int width, int height,
                    int format, int type, void* data) override;
    const char* GetRendererString(int name) override;

    // === Fence sync ===
    void* CreateFence() override;
    void WaitFence(void* fence) override;
    void DeleteFence(void* fence) override;

    // === Batch draw (called from rRenderBucket) ===
    void DrawBatchTriangles(const void* vertices, size_t vertexCount,
                            const void* stateKey) override;
    void DrawBatchLitTriangles(const void* vertices, size_t vertexCount,
                               const void* stateKey) override;
    void DrawBatchLines(const void* vertices, size_t vertexCount,
                        const void* stateKey) override;

    // === Model mesh draw (called from rModelMesh::Render) ===
    void DrawModelMesh(const std::vector<struct rModelVertex>& vertices,
                       const std::vector<unsigned int>& indices,
                       unsigned int textureId) override;

    // === Instanced cycle rendering ===
    //! Draw multiple instances of the same model mesh in one draw call.
    //! @param geometryKey Cache key (rModelVertex data pointer) identifying the mesh
    //! @param instances Array of rInstanceData (model matrix + color, 80 bytes each)
    //! @param instanceCount Number of instances
    //! @param textureId Texture to bind
    void DrawInstancedModelMesh(const void* geometryKey,
                                const struct rInstanceData* instances, size_t instanceCount,
                                unsigned int textureId);

    // === VBO helpers ===
    void PrepareForVBODraw() override;
    void SetTextureEnabled(bool enabled) override;
    void SetLightingEnabled(bool enabled) override;

    // === Flag management ===
    void ReallySetFlag(flag f, bool c) override;

private:
    // DrawBatch shared helpers
    void BuildPushConstants(VkPushConstants& pc, const rRenderStateKey& state, bool lit);
    VkDescriptorSet LookupTextureDescriptor(const rRenderStateKey& state);

    // Matrix stacks (reuse from shared code)
    rMatrixStack projectionStack_;
    rMatrixStack modelviewStack_;
    rMatrixStack textureStack_;
    rMatrixStack* currentStack_;

    // Current color state
    float currentColor_[4];
    float currentTexCoord_[2];

    // Cached viewport
    int cachedViewport_[4];

    // Render state
    bool textureEnabled_;
    bool lightingEnabled_;
    bool depthTestEnabled_;
    bool depthWriteEnabled_;
    bool blendEnabled_;
    bool cullFaceEnabled_;
    bool frontFaceCW_;       // true = CW front face (default for Y-flipped Vulkan)
    int blendSrc_, blendDst_;
    int depthFunc_;
    float cachedFrameTime_;  // tSysTimeFloat() cached once per frame

    // Bound texture (shadow cache)
    unsigned int boundTexture2D_;

    // Clear values
    float clearColor_[4];

    // Vulkan infrastructure
    rVulkanContext       context_;
    rVulkanSwapchain     swapchain_;
    rVulkanFramebuffer   framebuffer_;
    rVulkanPipelineManager pipelineManager_;
    rVulkanDescriptorManager descriptorManager_;
    rVulkanPostProcess     postProcess_;

    // Per-frame command resources
    VkCommandPool   commandPool_;
    VkCommandBuffer commandBuffers_[MAX_FRAMES_IN_FLIGHT];
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    VkFence         inFlightFences_[MAX_FRAMES_IN_FLIGHT];
    uint32_t        currentFrame_;
    uint32_t        currentImageIndex_;
    uint32_t        acquireSemaphoreIndex_;
    bool            frameStarted_;
    bool            pendingShaderReload_;


    // Lighting state (stored for shader-side lighting via UBO)
    struct LightParams {
        float position[4];  // xyz + w (0=directional), already transformed by camera modelview
        float diffuse[4];
        float specular[4];
    } lights_[2];
    float materialDiffuse_[4];
    float materialSpecular_[4];
    bool lightingDirty_ = true; // set when lights/materials/arena bounds change

    // Lighting UBO (std140 layout, written per-frame, lives in descriptor set 1)
    struct LightingUBO {
        float lightPos[2][4];       // 32
        float lightDiffuse[2][4];   // 32
        float lightSpecular[2][4];  // 32
        float materialDiffuse[4];   // 16
        float materialSpecular[4];  // 16
        int   lightingEnabled;      // 4  (normalMatrix moved to push constants)
        int   pad[3];               // 12 padding to 16-byte alignment (vec4 boundary)
        float arenaBBox[4];         // 16 — (minX, minY, maxX, maxY) for shader effects
    };
    // Per-frame buffers: frame N writes to slot N, GPU reads slot N without aliasing
    VkBuffer        lightingUBOBuffer_[MAX_FRAMES_IN_FLIGHT]  = {};
    VkDeviceMemory  lightingUBOMemory_[MAX_FRAMES_IN_FLIGHT]  = {};
    void*           lightingUBOMapped_[MAX_FRAMES_IN_FLIGHT]  = {};
    VkDescriptorSet lightingDescSet_[MAX_FRAMES_IN_FLIGHT]    = {};
    // Set 1 descriptor layout + pool (UBO-only, separate from the texture pool in set 0)
    VkDescriptorSetLayout lightingUBOLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      lightingUBOPool_   = VK_NULL_HANDLE;

    // Texture management
    struct VkTextureInfo
    {
        VkImage       image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView   view = VK_NULL_HANDLE;
        VkSampler     sampler = VK_NULL_HANDLE;
        int width = 0, height = 0;
        uint32_t mipLevels = 1;
        // Tracked sampler state (so TexParameter can recreate)
        VkFilter      minFilter = VK_FILTER_LINEAR;
        VkFilter      magFilter = VK_FILTER_LINEAR;
        VkSamplerAddressMode wrapS = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode wrapT = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkImageLayout descriptorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // CPU-side RGBA copy for TexSubImage2D (update CPU, then re-upload full image)
        std::vector<uint8_t> cpuData;
        bool dirty = false;
    };
    std::unordered_map<unsigned int, VkTextureInfo> textures_;
    unsigned int nextTextureId_;

    // Shader modules. Two fragment variants are compiled from the same
    // uber.frag source: the "plain" one writes only location=0 and is used
    // for render passes with a single color attachment (the swapchain pass);
    // the "emissive" one is compiled with USE_EMISSIVE_OUT and adds a
    // location=1 output for the 2-attachment post-process offscreen pass.
    // The pipeline manager picks the right variant per render pass at
    // pipeline creation time.
    VkShaderModule vertShader_;
    VkShaderModule vertShaderInstanced_;  // uber_instanced.vert for cycle instancing
    VkShaderModule fragShader_;
    VkShaderModule fragShaderEmissive_;

    // Default 1x1 white texture (used when no texture is bound)
    VkTextureInfo dummyTexture_;
    VkDescriptorSet dummyDescriptorSet_;

    [[nodiscard]] bool CreateDummyTexture();

    //! Recreate swapchain + framebuffer at the given pixel size.
    //! Waits for GPU idle first. Safe to call at any point between frames.
    bool RecreateSwapchain(int width, int height);

    // Textures pending destruction, deferred by MAX_FRAMES_IN_FLIGHT frames
    // so in-flight frames can finish sampling before resources are freed.
    DeferredQueue<VkTextureInfo> pendingDeleteTextures_;
    // Samplers pending destruction (deferred for same reason as textures)
    DeferredQueue<VkSampler> pendingDeleteSamplers_;

    // Vulkan render queue (draw command recording)
    rVulkanRenderQueue vulkanQueue_;

    // SDL window (retained for size queries on swapchain recreation)
    SDL_Window* window_ = nullptr;

    // Set when the swapchain is out of date and must be recreated before the next frame
    bool needsSwapchainRecreation_ = false;
    // Set after the first successful vkQueuePresentKHR — guards proactive resize detection
    bool firstFramePresented_      = false;
    // Set while the app is in the iOS background — BeginFrame skips acquisition to avoid
    // blocking on an unavailable Metal drawable (VK_ERROR_SURFACE_LOST_KHR / timeout).
    bool appInBackground_          = false;

    // User-created fences (tracked for cleanup)
    std::vector<VkFence*> userFences_;

    // === Model mesh cache (rModelVertex → rVertexLit32, built once per unique mesh) ===
    struct ModelMeshEntry {
        std::vector<rVertexLit32> litVerts;  // Expanded, index-flattened vertices
        float texScale = 1.0f;               // UV normalization scale (for tex matrix)
    };
    // Key: address of the vertex vector's data pointer (stable for the lifetime of rModelMesh)
    std::unordered_map<uintptr_t, ModelMeshEntry> modelMeshCache_;

    // === Per-viewport FBOs (split-screen depth isolation) ===
    static constexpr int MAX_VIEWPORT_FBOS = 4;
    struct ViewportFBO {
        VkImage        colorImage  = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageView    colorView   = VK_NULL_HANDLE;
        VkImage        emissiveImage  = VK_NULL_HANDLE;  // dummy emissive (matches PP 3-attachment layout)
        VkDeviceMemory emissiveMemory = VK_NULL_HANDLE;
        VkImageView    emissiveView   = VK_NULL_HANDLE;
        VkImage        depthImage  = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView    depthView   = VK_NULL_HANDLE;
        VkFramebuffer  framebuffer   = VK_NULL_HANDLE;
        VkSampler      sampler       = VK_NULL_HANDLE;  // for color texture
        VkSampler      depthSampler  = VK_NULL_HANDLE;  // for depth texture (separate to avoid double-destroy)
        unsigned int   colorTexId    = 0;   // registered in textures_ for binding
        unsigned int   depthTexId    = 0;   // registered for depth sampling (z-fighting fix)
        int width = 0, height = 0;
    };
    ViewportFBO viewportFBOs_[MAX_FRAMES_IN_FLIGHT][MAX_VIEWPORT_FBOS];
    VkRenderPass viewportRenderPass_ = VK_NULL_HANDLE;
    int viewportFBOCount_ = 0;
    int activeViewportFBO_ = -1;  // -1 = not using viewport FBOs

    bool CreateViewportFBO(int frame, int index, int width, int height);
    void DestroyViewportFBOs();
public:
    void BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h);
    void EndViewportFBO();
    void CompositeViewportFBOs(int count, const int viewportRects[][4], const int viewportRotations[]);
private:

};

//! Initialize Vulkan renderer
void sr_vkRendererInit();

//! Per-viewport FBO helpers for split-screen depth isolation
void sr_BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h);
void sr_EndViewportFBO();
void sr_CompositeViewportFBOs(int count, const int viewportRects[][4], const int viewportRotations[]);

//! Called on SDL_EVENT_WILL_ENTER_BACKGROUND — suspends frame acquisition so
//! vkAcquireNextImageKHR/vkWaitForFences cannot block on an unavailable Metal drawable.
void sr_vkSetAppInBackground(bool inBackground);

//! Called on SDL_EVENT_DID_ENTER_FOREGROUND — forces swapchain recreation because
//! the Metal surface dimensions may have changed while in the background.
void sr_vkRequestSwapchainRecreation();

#endif // DEDICATED
#endif // RVULKANRENDER_H
