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

#ifndef RWALLGEOMETRYBUFFER_H
#define RWALLGEOMETRYBUFFER_H

#include "defs.h"
#include "rPackedVertex.h"
#include <vector>

//! Buffer usage hint for GPU memory management
enum class rBufferUsage
{
    Static,    //!< Data rarely changes (GL_STATIC_DRAW) - stable wall portions
    Stream     //!< Data changes every frame (GL_STREAM_DRAW) - begin segments
};

//! Abstract interface for wall geometry buffers
//! Enables Vulkan migration by providing API-agnostic buffer management
class rWallGeometryBuffer
{
public:
    virtual ~rWallGeometryBuffer() = default;

    //! Reserve capacity for vertices (avoids reallocations)
    //! @param quadVertexCount Expected number of quad vertices (triangles = quads * 1.5)
    //! @param lineVertexCount Expected number of line vertices
    virtual void Reserve(size_t quadVertexCount, size_t lineVertexCount) = 0;

    //! Clear all accumulated geometry (keeps capacity)
    virtual void Clear() = 0;

    //! Add a quad for wall surface (4 vertices -> 2 triangles)
    //! @param v0,v1,v2,v3 Vertices in CCW order (bottom-left, top-left, top-right, bottom-right)
    virtual void AddQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                         const rPackedWallVertex& v2, const rPackedWallVertex& v3) = 0;

    //! Add a line segment for wall top edge
    //! @param v0,v1 Line endpoints
    virtual void AddLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1) = 0;

    //! Add multiple quads from a quad strip (for begin segment curves)
    //! @param vertices Array of quad strip vertices (alternating bottom/top)
    //! @param count Number of vertices (must be even, >= 4)
    virtual void AddQuadStrip(const rPackedWallVertex* vertices, size_t count) = 0;

    //! Add multiple line segments from a line strip
    //! @param vertices Array of line strip vertices
    //! @param count Number of vertices (>= 2)
    virtual void AddLineStrip(const rPackedLineVertex* vertices, size_t count) = 0;

    //! Upload accumulated geometry to GPU
    //! @return true if upload successful
    virtual bool Upload() = 0;

    //! Check if buffer has valid GPU resources and data
    virtual bool IsReady() const = 0;

    //! Render all quads (triangles)
    virtual void RenderQuads() = 0;

    //! Render all lines
    virtual void RenderLines() = 0;

    //! Release GPU resources
    virtual void Release() = 0;

    //! Get number of quads currently stored
    virtual size_t GetQuadCount() const = 0;

    //! Get number of lines currently stored
    virtual size_t GetLineCount() const = 0;

    //! Get GPU memory usage in bytes
    virtual size_t GetGPUMemoryUsage() const = 0;

    //! Check if buffer is empty
    bool IsEmpty() const { return GetQuadCount() == 0 && GetLineCount() == 0; }
};

//! Factory function to create appropriate buffer implementation
//! Returns GL3 implementation for now, Vulkan implementation later
rWallGeometryBuffer* CreateWallGeometryBuffer(rBufferUsage usage);

#endif // RWALLGEOMETRYBUFFER_H
