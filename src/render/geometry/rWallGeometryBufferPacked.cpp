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
#include "rWallGeometryBufferPacked.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rRenderQueue.h"
#include "rRendererState.h"
#include "rVertex.h"
#include <algorithm>
#include <cstring>

rWallGeometryBufferPacked::rWallGeometryBufferPacked(rBufferUsage usage)
    : usage_(usage)
    , uploaded_(false)
{
}

rWallGeometryBufferPacked::~rWallGeometryBufferPacked()
{
    Release();
}

void rWallGeometryBufferPacked::Reserve(size_t quadVertexCount, size_t lineVertexCount)
{
    quadVertices_.reserve(quadVertexCount);
    lineVertices_.reserve(lineVertexCount);
}

void rWallGeometryBufferPacked::Clear()
{
    quadVertices_.clear();
    lineVertices_.clear();
    uploaded_ = false;
    vkQuadCacheDirty_ = true;
    vkLineCacheDirty_ = true;
    if (usage_ == rBufferUsage::Stream)
    {
        persistMinU_ = 1e30f; persistMaxU_ = -1e30f;
        persistMinV_ = 1e30f; persistMaxV_ = -1e30f;
    }
}

void rWallGeometryBufferPacked::AddQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                                      const rPackedWallVertex& v2, const rPackedWallVertex& v3)
{
    // Convert quad to 2 triangles (CCW winding)
    quadVertices_.push_back(v0);
    quadVertices_.push_back(v1);
    quadVertices_.push_back(v2);

    quadVertices_.push_back(v0);
    quadVertices_.push_back(v2);
    quadVertices_.push_back(v3);

    uploaded_ = false;
}

void rWallGeometryBufferPacked::AddLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1)
{
    lineVertices_.push_back(v0);
    lineVertices_.push_back(v1);
    uploaded_ = false;
}

void rWallGeometryBufferPacked::AddQuadStrip(const rPackedWallVertex* vertices, size_t count)
{
    if (count < 4 || count % 2 != 0)
    {
        return;
    }

    for (size_t i = 0; i + 3 < count; i += 2)
    {
        const rPackedWallVertex& bl = vertices[i];
        const rPackedWallVertex& tl = vertices[i + 1];
        const rPackedWallVertex& br = vertices[i + 2];
        const rPackedWallVertex& tr = vertices[i + 3];

        AddQuad(bl, tl, tr, br);
    }
}

void rWallGeometryBufferPacked::AddLineStrip(const rPackedLineVertex* vertices, size_t count)
{
    if (count < 2)
    {
        return;
    }

    for (size_t i = 0; i + 1 < count; i++)
    {
        AddLine(vertices[i], vertices[i + 1]);
    }
}

bool rWallGeometryBufferPacked::Upload()
{
    uploaded_ = true;
    return true;
}

bool rWallGeometryBufferPacked::AppendVertices(const std::vector<rPackedWallVertex>& quads,
                                             const std::vector<rPackedLineVertex>& lines)
{
    // Incremental append is only meaningful for static buffers.
    if (usage_ != rBufferUsage::Static)
    {
        return false;
    }

    quadVertices_.insert(quadVertices_.end(), quads.begin(), quads.end());
    lineVertices_.insert(lineVertices_.end(), lines.begin(), lines.end());
    if (!quads.empty()) vkQuadCacheDirty_ = true;
    if (!lines.empty()) vkLineCacheDirty_ = true;
    return true;
}

bool rWallGeometryBufferPacked::IsReady() const
{
    return uploaded_ && (!quadVertices_.empty() || !lineVertices_.empty());
}

void rWallGeometryBufferPacked::EnsureQuadCache()
{
    if (!vkQuadCacheDirty_) return;

    // Scan current frame's UV bounds
    float minU = quadVertices_[0].texcoord[0], maxU = minU;
    float minV = quadVertices_[0].texcoord[1], maxV = minV;
    for (const auto& v : quadVertices_)
    {
        minU = std::min(minU, v.texcoord[0]); maxU = std::max(maxU, v.texcoord[0]);
        minV = std::min(minV, v.texcoord[1]); maxV = std::max(maxV, v.texcoord[1]);
    }

    // Expand persistent bounds (never shrink). This prevents texture matrix
    // pops when boundary segments shift the min/max range frame-to-frame.
    persistMinU_ = std::min(persistMinU_, minU);
    persistMaxU_ = std::max(persistMaxU_, maxU);
    persistMinV_ = std::min(persistMinV_, minV);
    persistMaxV_ = std::max(persistMaxV_, maxV);

    float rangeU = std::max(persistMaxU_ - persistMinU_, 1e-6f);
    float rangeV = std::max(persistMaxV_ - persistMinV_, 1e-6f);

    vkCachedQuadVerts_.resize(quadVertices_.size());
    for (size_t i = 0; i < quadVertices_.size(); i++)
    {
        const auto& v = quadVertices_[i];
        rVertex20& rv = vkCachedQuadVerts_[i];
        rv.position[0] = v.position[0]; rv.position[1] = v.position[1]; rv.position[2] = v.position[2];
        rv.color[0] = v.color[0]; rv.color[1] = v.color[1]; rv.color[2] = v.color[2]; rv.color[3] = v.color[3];
        float nu = (v.texcoord[0] - persistMinU_) / rangeU;
        float nv = (v.texcoord[1] - persistMinV_) / rangeV;
        rv.texcoord[0] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, nu * 32767.0f)));
        rv.texcoord[1] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, nv * 32767.0f)));
    }
    std::memset(vkQuadTexMatrix_, 0, sizeof(vkQuadTexMatrix_));
    vkQuadTexMatrix_[0]  = rangeU;
    vkQuadTexMatrix_[5]  = rangeV;
    vkQuadTexMatrix_[10] = 1.0f;
    vkQuadTexMatrix_[12] = persistMinU_;
    vkQuadTexMatrix_[13] = persistMinV_;
    vkQuadTexMatrix_[15] = 1.0f;
    vkQuadCacheDirty_ = false;
}

void rWallGeometryBufferPacked::RenderQuads()
{
    if (quadVertices_.empty()) return;
    EnsureQuadCache();

    // Stable wall quads are opaque — depth test cleanly resolves overlapping
    // walls from different players at grinding distance (no bleed-through).
    unsigned int texId = RenderGetBoundTexture2D();
    rRenderStateKey state = texId ? rRenderStateKey::Textured(texId, rBlendMode::Opaque)
                                  : rRenderStateKey::Colored(rBlendMode::Opaque);
    state.SetTexMatrix(vkQuadTexMatrix_);
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().Submit(rRenderPhase::OpaqueDynamic, state,
                                    vkCachedQuadVerts_.data(), vkCachedQuadVerts_.size());
}

void rWallGeometryBufferPacked::RenderQuadsBegin()
{
    if (quadVertices_.empty()) return;
    EnsureQuadCache();

    // Begin-gradient segments carry a per-vertex alpha fade (1 at the junction
    // with the static wall, 0 at the cycle tip). The wall surface itself is a
    // real solid object — only the visible color is faded for style.
    //
    // Submit through OpaqueDynamic so depth-write stays ON: the wall records
    // its own surface in the depth buffer, matching the static portion. This
    // is critical for depth-based post-process (cel-shading's Sobel ink): with
    // depth-write OFF the depth at streaming pixels would be whatever was
    // rendered behind (the floor), producing a sharp depth step at the
    // static/streaming seam → a fat black outline.
    //
    // Blend mode stays Alpha so the visible color fade still composites. In
    // Vulkan the blend mode is pipeline-baked from state.blendMode and is
    // independent of the phase config's `blend` bool (which only drives the
    // legacy GL state setter). Phase config controls depthTest/depthWrite —
    // here we want both ON, which OpaqueDynamic provides.
    //
    // The emissive attachment shares the same blend mode by default; the
    // shader compensates for the resulting src_alpha multiplication so bloom
    // and cel-shading see the opaque-equivalent emissive intensity.
    unsigned int texId = RenderGetBoundTexture2D();
    rRenderStateKey state = texId ? rRenderStateKey::Textured(texId, rBlendMode::Alpha)
                                  : rRenderStateKey::Colored(rBlendMode::Alpha);
    state.SetTexMatrix(vkQuadTexMatrix_);
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().Submit(rRenderPhase::OpaqueDynamic, state,
                                    vkCachedQuadVerts_.data(), vkCachedQuadVerts_.size());
}

void rWallGeometryBufferPacked::RenderQuadsTransparent()
{
    if (quadVertices_.empty()) return;
    EnsureQuadCache();

    // Death-fade segments are short-lived alpha-fading effects. Keep them in
    // the Transparent phase (depth-write OFF) so a nearly-invisible fragment
    // doesn't depth-cull cycles or zones that drive behind a dying wall.
    unsigned int texId = RenderGetBoundTexture2D();
    rRenderStateKey state = texId ? rRenderStateKey::Textured(texId, rBlendMode::Alpha)
                                  : rRenderStateKey::Colored(rBlendMode::Alpha);
    state.SetTexMatrix(vkQuadTexMatrix_);
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().Submit(rRenderPhase::Transparent, state,
                                    vkCachedQuadVerts_.data(), vkCachedQuadVerts_.size());
}

void rWallGeometryBufferPacked::RenderQuadsHead(uint32_t headSegCount)
{
    if (quadVertices_.empty() || headSegCount == 0) return;
    EnsureQuadCache();

    size_t vertCount = std::min(static_cast<size_t>(headSegCount) * 6, vkCachedQuadVerts_.size());
    if (vertCount == 0) return;

    // Streaming head (begin gradient) needs alpha for the fade-in effect.
    // Submit to Transparent phase for correct back-to-front sorting.
    unsigned int texId = RenderGetBoundTexture2D();
    rRenderStateKey state = texId ? rRenderStateKey::Textured(texId, rBlendMode::Alpha)
                                  : rRenderStateKey::Colored(rBlendMode::Alpha);
    state.SetTexMatrix(vkQuadTexMatrix_);
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().Submit(rRenderPhase::Transparent, state,
                                    vkCachedQuadVerts_.data(), vertCount);
}

void rWallGeometryBufferPacked::RenderLines()
{
    if (lineVertices_.empty()) return;

    if (vkLineCacheDirty_)
    {
        vkCachedLineVerts_.resize(lineVertices_.size());
        for (size_t i = 0; i < lineVertices_.size(); i++)
        {
            const auto& v = lineVertices_[i];
            rVertex20& rv = vkCachedLineVerts_[i];
            rv.position[0] = v.position[0];
            rv.position[1] = v.position[1];
            rv.position[2] = v.position[2];
            rv.color[0] = v.color[0];
            rv.color[1] = v.color[1];
            rv.color[2] = v.color[2];
            rv.color[3] = v.color[3];
            rv.texcoord[0] = 0;
            rv.texcoord[1] = 0;
        }
        vkLineCacheDirty_ = false;
    }

    rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Alpha);
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().SubmitLines(rRenderPhase::OpaqueDynamic, state,
                                         vkCachedLineVerts_.data(), vkCachedLineVerts_.size());
}

void rWallGeometryBufferPacked::Release()
{
    quadVertices_.clear();
    lineVertices_.clear();
    vkCachedQuadVerts_.clear();
    vkCachedLineVerts_.clear();
    uploaded_ = false;
    vkQuadCacheDirty_ = true;
    vkLineCacheDirty_ = true;
    persistMinU_ = 1e30f; persistMaxU_ = -1e30f;
    persistMinV_ = 1e30f; persistMaxV_ = -1e30f;
}

size_t rWallGeometryBufferPacked::GetQuadCount() const
{
    // Each quad is 6 vertices (2 triangles), so divide by 6
    return quadVertices_.size() / 6;
}

size_t rWallGeometryBufferPacked::GetLineCount() const
{
    return lineVertices_.size() / 2;
}

size_t rWallGeometryBufferPacked::GetGPUMemoryUsage() const
{
    return quadVertices_.size() * sizeof(rPackedWallVertex)
         + lineVertices_.size() * sizeof(rPackedLineVertex);
}

// Factory function implementation
rWallGeometryBuffer* CreateWallGeometryBuffer(rBufferUsage usage)
{
    return new rWallGeometryBufferPacked(usage);
}

#else // DEDICATED

// Dedicated server build: no graphics
rWallGeometryBuffer* CreateWallGeometryBuffer(rBufferUsage /*usage*/)
{
    return nullptr;
}

#endif
