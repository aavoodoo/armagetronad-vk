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

#ifndef ArmageTron_RENDER_H
#define ArmageTron_RENDER_H

#include "defs.h"
#include "rRenderEnums.h"
#include <vector>

class rRenderer{
public:
    rRenderer();
    virtual ~rRenderer();

    virtual void Vertex(REAL x, REAL y)                 = 0;
    virtual void Vertex(REAL x, REAL y, REAL z)         = 0;
    virtual void Vertex3(REAL *x)                       = 0;
    virtual void Vertex(REAL x, REAL y, REAL z, REAL w) = 0;

    virtual void TexCoord(REAL u, REAL v)                 = 0;
    virtual void TexCoord(REAL u, REAL v, REAL w)         = 0;
    virtual void TexCoord(REAL u, REAL v, REAL w, REAL t) = 0;

    virtual void Color(REAL r, REAL g, REAL b)        = 0;
    virtual void Color(REAL r, REAL g, REAL b,REAL a) = 0;

    virtual void End(bool force=true)   = 0;

    //! Prepare for external VBO drawing (call before custom VBO renders)
    virtual void PrepareForVBODraw() {}

    //! Invalidate shader state cache - call after external shader usage
    //! Forces uniform re-upload on next draw

    //! Set texture enabled state for VBO rendering
    virtual void SetTextureEnabled(bool enabled) {}

    //! Set lighting enabled state for VBO rendering
    virtual void SetLightingEnabled(bool enabled) {}

    //! Frame lifecycle — called by the swap mechanism
    virtual void BeginFrame() {}
    virtual void EndFrame() {}
    virtual void SwapBuffers() {}

    //! Backend-agnostic batch draw — called by rRenderBucket instead of direct GL/Vulkan
    //! The bucket sets up the texture matrix on the stack before calling.
    virtual void DrawBatchTriangles(const void* vertices, size_t vertexCount,
                                    const void* stateKey) {}
    virtual void DrawBatchLines(const void* vertices, size_t vertexCount,
                                const void* stateKey) {}
    //! Draw lit triangles (rVertexLit32 with normals) — only for models with lighting
    virtual void DrawBatchLitTriangles(const void* vertices, size_t vertexCount,
                                       const void* stateKey) {}

    //! Draw a model mesh (rModelVertex array + optional index array).
    //! The renderer owns vertex conversion and caching. Called from rModelMesh::Render().
    virtual void DrawModelMesh(const std::vector<struct rModelVertex>& vertices,
                               const std::vector<unsigned int>& indices,
                               unsigned int textureId) {}

    virtual void BeginLines()      = 0;
    virtual void BeginTriangles()  = 0;
    virtual void BeginQuads()      = 0;

    virtual void BeginLineStrip()      = 0;
    virtual void BeginTriangleStrip()  = 0;
    virtual void BeginQuadStrip()      = 0;

    virtual void BeginTriangleFan()    = 0;
    virtual void BeginLineLoop()      = 0;
    virtual void BeginPolygon()       = 0;

    virtual void ProjMatrix()     = 0;
    virtual void ModelMatrix()    = 0;
    virtual void TexMatrix()      = 0;

    virtual void PushMatrix()     = 0;
    virtual void PopMatrix()      = 0;
    virtual void MultMatrix(REAL mdata[4][4]) = 0;
    virtual void MultMatrixFlat(const REAL* mdata) = 0;
    virtual void LoadMatrixFlat(const REAL* mdata) = 0;

    virtual void IdentityMatrix()                       = 0;
    virtual void ScaleMatrix(REAL f)                    = 0;
    virtual void ScaleMatrix(REAL f1, REAL f2, REAL f3) = 0;

    virtual void TranslateMatrix(REAL x1, REAL x2, REAL x3) = 0;
    virtual void RotateMatrix(REAL angle, REAL x, REAL y, REAL z) = 0;

    // Projection setup (S6.2)
    virtual void Ortho(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar) = 0;
    virtual void Frustum(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar) = 0;
    virtual void Perspective(REAL fovy, REAL aspect, REAL zNear, REAL zFar) = 0;
    virtual void LookAt(REAL eyeX, REAL eyeY, REAL eyeZ,
                        REAL centerX, REAL centerY, REAL centerZ,
                        REAL upX, REAL upY, REAL upZ) = 0;

    // State management (S6.1)
    virtual void EnableState(int capability) = 0;
    virtual void DisableState(int capability) = 0;
    virtual void BlendFunc(int sfactor, int dfactor) = 0;
    virtual void DepthFunc(int func) = 0;
    virtual void DepthMask(bool write) = 0;
    virtual void FrontFace(int mode) = 0;
    virtual void PolygonOffset(REAL factor, REAL units) = 0;

    // Viewport/Scissor (S6.3)
    virtual void Viewport(int x, int y, int width, int height) = 0;
    virtual void GetViewport(int viewport[4]) = 0;
    virtual void Scissor(int x, int y, int width, int height) = 0;
    //! Set the viewport depth range (Vulkan minDepth/maxDepth).
    // Clear operations (S6.4)
    virtual void Clear(bool color, bool depth, bool stencil = false) = 0;
    virtual void ClearColor(REAL r, REAL g, REAL b, REAL a) = 0;
    // Fixed-function lighting (S7A.0) - stubs with default no-op for dedicated/ES
    virtual void Light(int /*light*/, int /*pname*/, const REAL* /*params*/) {}
    virtual void Material(int /*face*/, int /*pname*/, const REAL* /*params*/) {}
    virtual void Normal(REAL /*x*/, REAL /*y*/, REAL /*z*/) {}

    // Vertex arrays (S7A.0)
    virtual void EnableClientState(int /*array*/) {}
    virtual void DisableClientState(int /*array*/) {}
    virtual void VertexPointer(int /*size*/, int /*type*/, int /*stride*/, const void* /*ptr*/) {}
    virtual void ColorPointer(int /*size*/, int /*type*/, int /*stride*/, const void* /*ptr*/) {}
    virtual void DrawArrays(int /*mode*/, int /*first*/, int /*count*/) {}

    // State queries (S7A.1)
    virtual void GetModelviewMatrix(float* /*matrix*/) {}
    virtual void GetProjectionMatrix(float* /*matrix*/) {}
    virtual void GetMVPMatrix(float* /*matrix*/) {}
    virtual void GetColor(float* /*color*/) {}
    virtual bool IsEnabled(int /*capability*/) { return false; }

    // Hints (S7B.4)
    virtual void Hint(int /*target*/, int /*mode*/) {}

    // Alpha test (S7B.4)
    virtual void AlphaFunc(int /*func*/, REAL /*ref*/) {}

    // Texture operations (S9.0) - abstract texture state management
    virtual void TexParameter(int /*target*/, int /*pname*/, int /*param*/) {}
    virtual void BindTexture(int /*target*/, unsigned int /*texture*/) {}

    // Texture lifecycle (S9.1) - abstract texture creation/upload
    virtual unsigned int GetBoundTexture2D() { return 0; }
    virtual int GetMaxTextureSize() { return 256; }
    virtual unsigned int GenTexture() { return 0; }
    virtual void DeleteTexture(unsigned int /*texture*/) {}
    virtual void TexImage2D(int /*target*/, int /*level*/, int /*internalFormat*/,
                            int /*width*/, int /*height*/, int /*border*/,
                            int /*format*/, int /*type*/, const void* /*data*/) {}
    virtual void TexSubImage2D(int /*target*/, int /*level*/,
                               int /*xoffset*/, int /*yoffset*/,
                               int /*width*/, int /*height*/,
                               int /*format*/, int /*type*/, const void* /*data*/) {}
    virtual void GenerateMipmap(int /*target*/) {}
    virtual void PixelStorei(int /*pname*/, int /*param*/) {}
    virtual int GetTexLevelParameteriv(int /*target*/, int /*level*/, int /*pname*/) { return 0; }

    // System operations (S10.0) - GPU sync, queries, screenshots
    virtual void Finish() {}
    virtual void Flush() {}
    virtual void ReadPixels(int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                            int /*format*/, int /*type*/, void* /*data*/) {}
    virtual const char* GetRendererString(int /*name*/) { return ""; }

    // Fence sync (S12.0)
    virtual void* CreateFence() { return nullptr; }
    virtual void WaitFence(void* /*fence*/) {}
    virtual void DeleteFence(void* /*fence*/) {}

    typedef enum {BACKFACE_CULL=0, ALPHA_BLEND, ALPHA_TEST, DEPTH_TEST,
                  SMOOTH_SHADE, Z_OFFSET,
                  FLAG_END} flag;

    void PushFlags();
    void PopFlags();
    void SetFlag(flag f, bool c);

protected:
    virtual void ReallySetFlag(flag f, bool c) = 0;
    void         ChangeFlags(int before, int after) const;

#define STACK_DEPTH 100

    int flagstack[STACK_DEPTH];
    int stackpos;
};

/**
 * THREADING MODEL (GL3 Renderer):
 *
 * RENDER THREAD AFFINITY:
 * - All renderer calls MUST occur on the thread that created the GL context
 * - The global `renderer` pointer is accessed WITHOUT locks (intentional)
 * - OpenGL is single-threaded by design - this is enforced
 *
 * LAZY INITIALIZATION (Thread-Safe):
 * - Shaders: Protected by std::call_once in PrepareForDraw()
 * - UBOs: Protected by std::call_once in PrepareForDraw()
 * - Buffers: Recreated lazily on first use after context loss
 *
 * CONTEXT LOSS RECOVERY:
 * - OnContextLost(): Releases GPU resources, resets std::once_flag
 * - OnContextRestored(): Marks context valid, triggers lazy reload
 * - Deferred deletion queue is cleared on context loss
 *
 * RESOURCE LIFETIME:
 * - VBOs/VAOs: Deferred deletion after 2 frames via rResourceDeferredDelete
 * - Textures: Reference counted via rResourceTexture::Use()/Release()
 * - Shaders: Singleton manager, recreated on context restore
 *
 * LIFECYCLE:
 * - The renderer registers itself in the constructor and unregisters in the destructor
 * - Do not create temporary renderer objects as they will unregister the active renderer
 *
 * INITIALIZATION & CLEANUP:
 * - Use sr_InitRenderer() to properly initialize the renderer
 * - Use sr_RendererCleanup() to properly clean up the renderer
 *
 * SAFETY ASSERTIONS (DEBUG only):
 * - sr_SetRenderContext(): Asserts single-threaded access
 * - PrepareForDraw(): ThreadSanitizer clean
 */
extern rRenderer *renderer;

inline void Vertex(REAL x, REAL y){
    renderer->Vertex(x,y);
}

inline void Vertex(REAL x, REAL y, REAL z){
    renderer->Vertex(x,y,z);
}

inline void Vertex3(REAL *x){
    renderer->Vertex3(x);
}

inline void Vertex(REAL x, REAL y, REAL z, REAL w){
    renderer->Vertex(x,y,z,w);
}

inline void TexCoord(REAL u, REAL v){
    renderer->TexCoord(u,v);
}

inline void TexCoord(REAL u, REAL v, REAL w){
    renderer->TexCoord(u,v,w);
}

inline void TexCoord(REAL u, REAL v, REAL w, REAL t){
    renderer->TexCoord(u,v,w,t);
}

inline void Color(REAL r, REAL g, REAL b){
    renderer->Color(r,g,b);
}

inline void Color(REAL r, REAL g, REAL b,REAL a){
    renderer->Color(r,g,b,a);
}

//! Call before rendering from custom VBOs to set up shader/uniforms
inline void sr_PrepareForVBODraw(){
    if (renderer) renderer->PrepareForVBODraw();
}

//! Set texture enabled state for VBO rendering
inline void sr_SetTextureEnabled(bool enabled){
    if (renderer) renderer->SetTextureEnabled(enabled);
}

//! Set lighting enabled state for VBO rendering
inline void sr_SetLightingEnabled(bool enabled){
    if (renderer) renderer->SetLightingEnabled(enabled);
}

inline void BeginLines(){
    renderer->BeginLines();
}

inline void BeginTriangles(){
    renderer->BeginTriangles();
}

inline void BeginQuads(){
    renderer->BeginQuads();
}

inline void BeginLineStrip(){
    renderer->BeginLineStrip();
}

inline void BeginLineLoop(){
    renderer->BeginLineLoop();
}

inline void BeginTriangleStrip(){
    renderer->BeginTriangleStrip();
}

inline void BeginQuadStrip(){
    renderer->BeginQuadStrip();
}

inline void BeginTriangleFan(){
    renderer->BeginTriangleFan();
}

inline void BeginPolygon(){
    renderer->BeginPolygon();
}


inline void ProjMatrix(){
    renderer->ProjMatrix();
}

inline void ModelMatrix(){
    renderer->ModelMatrix();
}

inline void TexMatrix(){
    renderer->TexMatrix();
}


inline void PushMatrix(){
    renderer->PushMatrix();
}

inline void PopMatrix(){
    renderer->PopMatrix();
}


inline void MultMatrix(REAL mdata[4][4]){
    renderer->MultMatrix(mdata);
}

inline void MultMatrix(const REAL* mdata){
    renderer->MultMatrixFlat(mdata);
}

inline void LoadMatrix(const REAL* mdata){
    renderer->LoadMatrixFlat(mdata);
}


inline void IdentityMatrix(){
    renderer->IdentityMatrix();
}

inline void ScaleMatrix(REAL f){
    renderer->ScaleMatrix(f);
}

inline void ScaleMatrix(REAL f1, REAL f2, REAL f3){
    renderer->ScaleMatrix(f1,f2,f3);
}


inline void TranslateMatrix(REAL x1, REAL x2, REAL x3){
    renderer->TranslateMatrix(x1,x2,x3);
}

inline void RotateMatrix(REAL angle, REAL x, REAL y, REAL z){
    renderer->RotateMatrix(angle,x,y,z);
}

// Projection setup wrappers (S6.2)
inline void Ortho(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar){
    renderer->Ortho(left,right,bottom,top,zNear,zFar);
}

inline void Frustum(REAL left, REAL right, REAL bottom, REAL top, REAL zNear, REAL zFar){
    renderer->Frustum(left,right,bottom,top,zNear,zFar);
}

inline void Perspective(REAL fovy, REAL aspect, REAL zNear, REAL zFar){
    renderer->Perspective(fovy,aspect,zNear,zFar);
}

inline void LookAt(REAL eyeX, REAL eyeY, REAL eyeZ,
                   REAL centerX, REAL centerY, REAL centerZ,
                   REAL upX, REAL upY, REAL upZ){
    renderer->LookAt(eyeX,eyeY,eyeZ,centerX,centerY,centerZ,upX,upY,upZ);
}

// State management wrappers (S6.1)
inline void RenderEnableState(int capability){
    renderer->EnableState(capability);
}

inline void RenderDisableState(int capability){
    renderer->DisableState(capability);
}

inline void RenderBlendFunc(int sfactor, int dfactor){
    renderer->BlendFunc(sfactor,dfactor);
}

inline void RenderDepthFunc(int func){
    renderer->DepthFunc(func);
}

inline void RenderDepthMask(bool write){
    renderer->DepthMask(write);
}

inline void RenderFrontFace(int mode){
    renderer->FrontFace(mode);
}

inline void RenderPolygonOffset(REAL factor, REAL units){
    renderer->PolygonOffset(factor,units);
}

// Viewport/Scissor wrappers (S6.3)
inline void RenderViewport(int x, int y, int width, int height){
    renderer->Viewport(x,y,width,height);
}

inline void RenderGetViewport(int viewport[4]){
    renderer->GetViewport(viewport);
}

inline void RenderScissor(int x, int y, int width, int height){
    renderer->Scissor(x,y,width,height);
}

// Clear wrappers (S6.4)
inline void RenderClear(bool color, bool depth, bool stencil = false){
    renderer->Clear(color,depth,stencil);
}

inline void RenderClearColor(REAL r, REAL g, REAL b, REAL a){
    renderer->ClearColor(r,g,b,a);
}

// Fixed-function lighting wrappers (S7A.0)
inline void RenderLight(int light, int pname, const REAL* params){
    renderer->Light(light,pname,params);
}

inline void RenderMaterial(int face, int pname, const REAL* params){
    renderer->Material(face,pname,params);
}

inline void RenderNormal(REAL x, REAL y, REAL z){
    renderer->Normal(x,y,z);
}

// Vertex array wrappers (S7A.0)
inline void RenderEnableClientState(int array){
    renderer->EnableClientState(array);
}

inline void RenderDisableClientState(int array){
    renderer->DisableClientState(array);
}

inline void RenderVertexPointer(int size, int type, int stride, const void* ptr){
    renderer->VertexPointer(size,type,stride,ptr);
}

inline void RenderColorPointer(int size, int type, int stride, const void* ptr){
    renderer->ColorPointer(size,type,stride,ptr);
}

inline void RenderDrawArrays(int mode, int first, int count){
    renderer->DrawArrays(mode,first,count);
}

inline void RenderGetModelviewMatrix(float* matrix){
    renderer->GetModelviewMatrix(matrix);
}

inline void RenderGetProjectionMatrix(float* matrix){
    renderer->GetProjectionMatrix(matrix);
}

inline void RenderGetMVPMatrix(float* matrix){
    renderer->GetMVPMatrix(matrix);
}

inline void RenderGetColor(float* color){
    renderer->GetColor(color);
}

inline bool RenderIsEnabled(int capability){
    return renderer->IsEnabled(capability);
}

inline void RenderHint(int target, int mode){
    renderer->Hint(target, mode);
}

inline void RenderAlphaFunc(int func, REAL ref){
    renderer->AlphaFunc(func, ref);
}

// Enum-based overloads (for type-safe, backend-neutral calls)
#ifndef DEDICATED
inline void RenderEnableState(rCapability capability){
    renderer->EnableState(rCapabilityToInt(capability));
}

inline void RenderDisableState(rCapability capability){
    renderer->DisableState(rCapabilityToInt(capability));
}

inline void RenderBlendFunc(rBlendFactor sfactor, rBlendFactor dfactor){
    renderer->BlendFunc(rBlendFactorToInt(sfactor), rBlendFactorToInt(dfactor));
}

inline void RenderDepthFunc(rCompareFunc func){
    renderer->DepthFunc(rCompareFuncToInt(func));
}

inline void RenderAlphaFunc(rCompareFunc func, REAL ref){
    renderer->AlphaFunc(rCompareFuncToInt(func), ref);
}

inline void RenderFrontFace(rFrontFace face){
    renderer->FrontFace(rFrontFaceToInt(face));
}

inline void RenderMaterial(rMaterialFace face, rMaterialProperty pname, const REAL* params){
    renderer->Material(rMaterialFaceToInt(face), rMaterialPropertyToInt(pname), params);
}

inline void RenderLight(int light, rLightParam pname, const REAL* params){
    renderer->Light(light, rLightParamToInt(pname), params);
}

inline bool RenderIsEnabled(rCapability capability){
    return renderer->IsEnabled(rCapabilityToInt(capability));
}

inline void RenderHint(rHintTarget target, rHintMode mode){
    renderer->Hint(rHintTargetToInt(target), rHintModeToInt(mode));
}

#endif // DEDICATED

// Texture wrappers (S9.0)
inline void RenderTexParameter(int target, int pname, int param){
    renderer->TexParameter(target, pname, param);
}

inline void RenderBindTexture(int target, unsigned int texture){
    renderer->BindTexture(target, texture);
}

// Texture lifecycle wrappers (S9.1)
inline unsigned int RenderGetBoundTexture2D(){
    return renderer ? renderer->GetBoundTexture2D() : 0;
}

inline int RenderGetMaxTextureSize(){
    return renderer ? renderer->GetMaxTextureSize() : 256;
}

inline unsigned int RenderGenTexture(){
    return renderer ? renderer->GenTexture() : 0;
}

inline void RenderDeleteTexture(unsigned int texture){
    if (renderer) renderer->DeleteTexture(texture);
}

inline void RenderTexImage2D(int target, int level, int internalFormat,
                             int width, int height, int border,
                             int format, int type, const void* data){
    if (renderer) renderer->TexImage2D(target, level, internalFormat, width, height, border, format, type, data);
}

inline void RenderTexSubImage2D(int target, int level,
                                int xoffset, int yoffset,
                                int width, int height,
                                int format, int type, const void* data){
    if (renderer) renderer->TexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, data);
}

inline void RenderGenerateMipmap(int target){
    if (renderer) renderer->GenerateMipmap(target);
}

inline void RenderPixelStorei(int pname, int param){
    if (renderer) renderer->PixelStorei(pname, param);
}

inline int RenderGetTexLevelParameteriv(int target, int level, int pname){
    return renderer ? renderer->GetTexLevelParameteriv(target, level, pname) : 0;
}

// System operation wrappers (S10.0)
inline void RenderFinish(){
    if (renderer) renderer->Finish();
}

inline void RenderFlush(){
    if (renderer) renderer->Flush();
}

inline void RenderReadPixels(int x, int y, int width, int height,
                             int format, int type, void* data){
    if (renderer) renderer->ReadPixels(x, y, width, height, format, type, data);
}

inline const char* RenderGetRendererString(int name){
    return renderer ? renderer->GetRendererString(name) : "";
}

// Fence sync wrappers (S12.0)
inline void* RenderCreateFence(){
    return renderer ? renderer->CreateFence() : nullptr;
}

inline void RenderWaitFence(void* fence){
    if (renderer) renderer->WaitFence(fence);
}

inline void RenderDeleteFence(void* fence){
    if (renderer) renderer->DeleteFence(fence);
}

void sr_RendererCleanup();
void sr_vkRendererInit();
void sr_vkSetAppInBackground(bool inBackground);
void sr_vkRequestSwapchainRecreation();

//! Per-viewport FBO management for split-screen depth isolation.
//! Each viewport gets its own color+depth framebuffer, composited onto the swapchain.
extern bool sr_inViewportFBO;  //!< True while rendering into a viewport FBO
void sr_BeginViewportFBO(int index, int totalViewports, int x, int y, int w, int h);
void sr_EndViewportFBO();
void sr_CompositeViewportFBOs(int count, const int viewportRects[][4], const int viewportRotations[] = nullptr);

//! Draw instanced model mesh (cycle batching). Called from rEndCycleRendering().
struct rInstanceData;
void sr_DrawInstancedModelMesh(const void* geometryKey,
                               const rInstanceData* instances, size_t instanceCount,
                               unsigned int textureId);

//! Incremented when the model mesh cache is invalidated (e.g. shader reload).
//! Cycle renderers compare against their last priming version to re-prime.
extern int sr_modelCacheVersion;

//! Check if a model mesh is already in the instancing cache.
bool sr_IsModelMeshCached(const void* geometryKey);

//! Initialize the renderer
void sr_InitRenderer(bool useGL3 = false);

#endif
