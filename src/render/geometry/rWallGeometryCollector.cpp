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
#include "rWallGeometryCollector.h"

#ifndef DEDICATED

#include "rWallGeometryBufferPacked.h"

rWallGeometryCollector::rWallGeometryCollector()
    : staticNeedsRebuild_(true)
    , collecting_(false)
{
    // Create buffers with appropriate usage hints
    staticBuffer_.reset(CreateWallGeometryBuffer(rBufferUsage::Static));
    streamingBuffer_.reset(CreateWallGeometryBuffer(rBufferUsage::Stream));
}

rWallGeometryCollector::~rWallGeometryCollector()
{
    Release();
}

void rWallGeometryCollector::BeginFrame()
{
    // Clear streaming buffer every frame (it changes constantly)
    if (streamingBuffer_)
    {
        streamingBuffer_->Clear();
    }

    // Always clear and rebuild static buffer each frame.
    // This is necessary because RenderList() calls AddNormalQuad for ALL stable
    // segments every frame, not just new ones. Without clearing, we'd accumulate
    // duplicate geometry causing visual artifacts (black band at static/streaming boundary).
    // The duplicate geometry comes from the same segment being re-added with slightly
    // different boundary positions as the cycle moves forward.
    if (staticBuffer_)
    {
        staticBuffer_->Clear();
        pendingStaticQuads_.clear();
        pendingStaticLines_.clear();
    }
    staticNeedsRebuild_ = true;

    collecting_ = true;
}

REAL rWallGeometryCollector::CalculateStableThreshold(REAL cycleDistance, REAL segmentLength, REAL beginLength)
{
    // Segments at distances below this threshold are stable (won't animate)
    // cycleDistance / segmentLength gives the "time" coordinate of the cycle's current position
    // Subtracting beginLength gives the threshold where animation ends
    return (cycleDistance / segmentLength) - beginLength;
}

void rWallGeometryCollector::AddNormalQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                                            const rPackedWallVertex& v2, const rPackedWallVertex& v3)
{
    if (!collecting_ || !staticBuffer_)
    {
        return;
    }

    if (staticNeedsRebuild_)
    {
        // Full rebuild mode - add directly to buffer
        staticBuffer_->AddQuad(v0, v1, v2, v3);
    }
    else
    {
        // Incremental mode - accumulate for append
        // Convert quad to triangles
        pendingStaticQuads_.push_back(v0);
        pendingStaticQuads_.push_back(v1);
        pendingStaticQuads_.push_back(v2);
        pendingStaticQuads_.push_back(v0);
        pendingStaticQuads_.push_back(v2);
        pendingStaticQuads_.push_back(v3);
    }
}

void rWallGeometryCollector::AddNormalLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1)
{
    if (!collecting_ || !staticBuffer_)
    {
        return;
    }

    if (staticNeedsRebuild_)
    {
        staticBuffer_->AddLine(v0, v1);
    }
    else
    {
        pendingStaticLines_.push_back(v0);
        pendingStaticLines_.push_back(v1);
    }
}

void rWallGeometryCollector::AddBeginQuadStrip(const std::vector<rPackedWallVertex>& vertices)
{
    if (!collecting_ || !streamingBuffer_ || vertices.size() < 4)
    {
        return;
    }

    streamingBuffer_->AddQuadStrip(vertices.data(), vertices.size());
}

void rWallGeometryCollector::AddBeginLineStrip(const std::vector<rPackedLineVertex>& vertices)
{
    if (!collecting_ || !streamingBuffer_ || vertices.size() < 2)
    {
        return;
    }

    streamingBuffer_->AddLineStrip(vertices.data(), vertices.size());
}

void rWallGeometryCollector::AddDeathQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                                           const rPackedWallVertex& v2, const rPackedWallVertex& v3)
{
    if (!collecting_ || !streamingBuffer_)
    {
        return;
    }

    // Death effects go to streaming buffer (they animate)
    streamingBuffer_->AddQuad(v0, v1, v2, v3);
}

void rWallGeometryCollector::AddDeathLine(const rPackedLineVertex& v0, const rPackedLineVertex& v1)
{
    if (!collecting_ || !streamingBuffer_)
    {
        return;
    }

    streamingBuffer_->AddLine(v0, v1);
}

bool rWallGeometryCollector::TryAppendStatic()
{
    if (staticNeedsRebuild_ || !staticBuffer_)
    {
        return false;
    }

    if (pendingStaticQuads_.empty() && pendingStaticLines_.empty())
    {
        return true;  // Nothing to append
    }

    // Try incremental append via GL3 buffer
    auto* gl3Buffer = dynamic_cast<rWallGeometryBufferPacked*>(staticBuffer_.get());
    if (gl3Buffer)
    {
        bool success = gl3Buffer->AppendVertices(pendingStaticQuads_, pendingStaticLines_);
        if (success)
        {
            pendingStaticQuads_.clear();
            pendingStaticLines_.clear();
            return true;
        }
    }

    // Append failed - need full rebuild
    staticNeedsRebuild_ = true;
    return false;
}

bool rWallGeometryCollector::EndFrame()
{
    collecting_ = false;

    bool success = true;

    // Handle static buffer
    if (staticBuffer_)
    {
        if (staticNeedsRebuild_)
        {
            // Full upload after rebuild
            success = staticBuffer_->Upload();
            if (success)
            {
                staticNeedsRebuild_ = false;
            }
        }
        else
        {
            // Try incremental append
            if (!TryAppendStatic())
            {
                // Append failed, do full rebuild next frame
            }
        }
    }

    // Streaming buffer always gets full upload each frame
    if (streamingBuffer_ && !streamingBuffer_->IsEmpty())
    {
        success = streamingBuffer_->Upload() && success;
    }

    return success;
}

void rWallGeometryCollector::Render(bool renderLines, bool renderQuads)
{
    // Render static geometry first
    if (staticBuffer_ && staticBuffer_->IsReady())
    {
        if (renderLines)
        {
            staticBuffer_->RenderLines();
        }
        if (renderQuads)
        {
            staticBuffer_->RenderQuads();
        }
    }

    // Then render streaming geometry (begin segments, death effects)
    if (streamingBuffer_ && streamingBuffer_->IsReady())
    {
        if (renderLines)
        {
            streamingBuffer_->RenderLines();
        }
        if (renderQuads)
        {
            streamingBuffer_->RenderQuads();
        }
    }
}

size_t rWallGeometryCollector::GetStaticQuadCount() const
{
    return staticBuffer_ ? staticBuffer_->GetQuadCount() : 0;
}

size_t rWallGeometryCollector::GetStreamingQuadCount() const
{
    return streamingBuffer_ ? streamingBuffer_->GetQuadCount() : 0;
}

size_t rWallGeometryCollector::GetGPUMemoryUsage() const
{
    size_t usage = 0;
    if (staticBuffer_)
    {
        usage += staticBuffer_->GetGPUMemoryUsage();
    }
    if (streamingBuffer_)
    {
        usage += streamingBuffer_->GetGPUMemoryUsage();
    }
    return usage;
}

void rWallGeometryCollector::Release()
{
    if (staticBuffer_)
    {
        staticBuffer_->Release();
    }
    if (streamingBuffer_)
    {
        streamingBuffer_->Release();
    }
    pendingStaticQuads_.clear();
    pendingStaticLines_.clear();
    staticNeedsRebuild_ = true;
}

#endif // DEDICATED
