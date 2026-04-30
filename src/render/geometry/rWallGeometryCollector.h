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

#ifndef RWALLGEOMETRYCOLLECTOR_H
#define RWALLGEOMETRYCOLLECTOR_H

#include "defs.h"
#include "rPackedVertex.h"
#include "rWallGeometryBuffer.h"
#include <memory>
#include <vector>

class gCycle;

//! Segment type classification for buffer routing
enum class rWallSegmentType
{
    Normal,     //!< Stable wall segment (static buffer)
    Begin,      //!< Begin/fade-in segment near cycle (streaming buffer)
    Death       //!< Death fade-out segment (streaming buffer)
};

//! Collects wall geometry and routes to appropriate buffers
//! Single-pass collection: iterates walls once, separating static/streaming geometry
class rWallGeometryCollector
{
public:
    //! Create collector with separate static and streaming buffers
    rWallGeometryCollector();
    ~rWallGeometryCollector();

    //! Reset for new frame collection
    void BeginFrame();

    //! Calculate stable threshold for a cycle
    //! Segments below this threshold go to static buffer
    //! @param cycleDistance Current cycle distance
    //! @param segmentLength Segment length (SEGLEN)
    //! @param beginLength Begin fade length (gBEG_LEN)
    static REAL CalculateStableThreshold(REAL cycleDistance, REAL segmentLength, REAL beginLength);

    //! Add a normal (stable) wall segment quad.
    //! The very first call per BeginFrame is routed to the streaming buffer (bridges
    //! the gap between the begin/gradient zone and the static buffer).
    //! @return true  = added to static buffer (caller should also call sr_AddWallComputeSegment)
    //! @return false = added to streaming buffer (skip sr_AddWallComputeSegment)
    bool AddNormalQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                       const rPackedWallVertex& v2, const rPackedWallVertex& v3);

    //! Add a normal (stable) wall segment line
    //! Routes to static buffer
    void AddNormalLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1);

    //! Add a begin (streaming) wall segment quad strip
    //! Routes to streaming buffer
    void AddBeginQuadStrip(const std::vector<rPackedWallVertex>& vertices);

    //! Add a begin (streaming) wall segment line strip
    //! Routes to streaming buffer
    void AddBeginLineStrip(const std::vector<rPackedLineVertex>& vertices);

    //! Add death fade segment (streaming)
    void AddDeathQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                      const rPackedWallVertex& v2, const rPackedWallVertex& v3);

    //! Add death fade line (streaming)
    void AddDeathLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1);

    //! End frame collection, upload geometry to GPU
    //! @return true if upload successful
    bool EndFrame();

    //! Check if static buffer needs rebuild (capacity exceeded)
    bool StaticBufferNeedsRebuild() const { return staticNeedsRebuild_; }

    //! Force full rebuild of static buffer on next upload
    void InvalidateStaticBuffer() { staticNeedsRebuild_ = true; }

    //! Try to append new static geometry without full rebuild
    //! @return true if append succeeded, false if full rebuild needed
    bool TryAppendStatic();

    //! Render all collected geometry
    //! @param renderLines Whether to render line geometry
    //! @param renderQuads Whether to render quad geometry
    void Render(bool renderLines, bool renderQuads);

    //! Get static buffer for direct access
    rWallGeometryBuffer* GetStaticBuffer() { return staticBuffer_.get(); }

    //! Get streaming buffer for direct access
    rWallGeometryBuffer* GetStreamingBuffer() { return streamingBuffer_.get(); }

    //! Get number of static quads
    size_t GetStaticQuadCount() const;

    //! Get number of streaming quads
    size_t GetStreamingQuadCount() const;

    //! Get total GPU memory usage
    size_t GetGPUMemoryUsage() const;

    //! Release all GPU resources
    void Release();

private:
    std::unique_ptr<rWallGeometryBuffer> staticBuffer_;     //!< Static wall portions
    std::unique_ptr<rWallGeometryBuffer> streamingBuffer_;  //!< Begin segments + death effects

    // Pending static vertices (for incremental append)
    std::vector<rPackedWallVertex> pendingStaticQuads_;
    std::vector<rPackedLineVertex> pendingStaticLines_;

    bool staticNeedsRebuild_;
    bool collecting_;
    bool firstNormalAdded_;   //!< True once the bridge segment has been sent to streaming

    // Per-collector GPU compute wall tracking.
    // Each collector occupies a contiguous slice of the global sg_wallSegsCurrent_ array.
    // At BeginFrame the previous frame's slice is saved; at Render() it is used to
    // call sr_DrawComputedWallsRange with the correct per-collector offset and count.
    uint32_t computeStartPrev_    = 0;   //!< Start index in the previous frame's compute batch
    uint32_t computeCountPrev_    = 0;   //!< Number of segments this collector contributed last frame
    uint32_t computeStartCurrent_ = 0;   //!< Start index in the current frame's compute batch
    uint32_t computeCountCurrent_ = 0;   //!< Accumulated count this frame (set at EndFrame)
};

#endif // RWALLGEOMETRYCOLLECTOR_H
