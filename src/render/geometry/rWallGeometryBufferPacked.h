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

#ifndef RWALLGEOMETRYBUFFERPACKED_H
#define RWALLGEOMETRYBUFFERPACKED_H

#include "rWallGeometryBuffer.h"
#include "rVertex.h"

#ifndef DEDICATED

//! Wall geometry buffer: stores packed wall vertices and submits them
//! to the Vulkan render queue on demand. The "GL3" suffix in the name is
//! a historical remnant from the GL3 implementation; all GL code has been
//! removed.
class rWallGeometryBufferPacked : public rWallGeometryBuffer
{
public:
    explicit rWallGeometryBufferPacked(rBufferUsage usage);
    ~rWallGeometryBufferPacked() override;

    // rWallGeometryBuffer interface
    void Reserve(size_t quadVertexCount, size_t lineVertexCount) override;
    void Clear() override;
    void AddQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                 const rPackedWallVertex& v2, const rPackedWallVertex& v3) override;
    void AddLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1) override;
    void AddQuadStrip(const rPackedWallVertex* vertices, size_t count) override;
    void AddLineStrip(const rPackedLineVertex* vertices, size_t count) override;
    bool Upload() override;
    bool IsReady() const override;
    void RenderQuads() override;
    void RenderQuadsBegin() override;
    void RenderQuadsTransparent() override;
    void RenderQuadsHead(uint32_t headSegCount) override;
    void RenderLines() override;
    void Release() override;
    size_t GetQuadCount() const override;
    size_t GetLineCount() const override;
    size_t GetGPUMemoryUsage() const override;

    //! Append new vertices without full rebuild (for incremental static updates)
    bool AppendVertices(const std::vector<rPackedWallVertex>& quads,
                        const std::vector<rPackedLineVertex>& lines);

private:
    void EnsureQuadCache();
    rBufferUsage usage_;

    std::vector<rPackedWallVertex> quadVertices_;
    std::vector<rPackedLineVertex> lineVertices_;

    bool uploaded_;

    // Cached rVertex20 conversion (rebuilt when dirty)
    std::vector<rVertex20> vkCachedQuadVerts_;
    float vkQuadTexMatrix_[16] = {};
    bool vkQuadCacheDirty_ = true;

    // Persistent UV bounds — only expand, never shrink, to prevent texture
    // matrix pops when boundary segments shift the min/max range.
    float persistMinU_ = 1e30f, persistMaxU_ = -1e30f;
    float persistMinV_ = 1e30f, persistMaxV_ = -1e30f;

    // Cached line conversion (avoids per-frame heap allocation)
    std::vector<rVertex20> vkCachedLineVerts_;
    bool vkLineCacheDirty_ = true;
};

#endif // DEDICATED

#endif // RWALLGEOMETRYBUFFERPACKED_H
