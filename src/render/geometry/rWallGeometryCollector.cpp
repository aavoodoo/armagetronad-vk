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
#include "rRender.h"

rWallGeometryCollector::rWallGeometryCollector()
    : staticNeedsRebuild_(true)
    , collecting_(false)
{
    // Create buffers with appropriate usage hints. Begin-gradient and death
    // effects both stream every frame but render through different phases
    // (OpaqueDynamic vs Transparent) — keep them in separate buffers so each
    // can be submitted with its own pipeline state.
    staticBuffer_.reset(CreateWallGeometryBuffer(rBufferUsage::Static));
    beginBuffer_.reset(CreateWallGeometryBuffer(rBufferUsage::Stream));
    streamingBuffer_.reset(CreateWallGeometryBuffer(rBufferUsage::Stream));
}

rWallGeometryCollector::~rWallGeometryCollector()
{
    Release();
}

void rWallGeometryCollector::BeginFrame()
{
    // Save previous frame's per-collector compute slice, record new start
    computeStartPrev_    = computeStartCurrent_;
    computeCountPrev_    = computeCountCurrent_;
    computeStartCurrent_ = sr_GetWallComputeCurrentCount();
    computeCountCurrent_ = 0;

    // Clear per-frame buffers (begin gradient + death effects)
    if (beginBuffer_)
    {
        beginBuffer_->Clear();
    }
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

bool rWallGeometryCollector::AddNormalQuad(const rPackedWallVertex& v0, const rPackedWallVertex& v1,
                                            const rPackedWallVertex& v2, const rPackedWallVertex& v3)
{
    if (!collecting_) return false;

    if (!staticBuffer_) return false;

    if (staticNeedsRebuild_)
    {
        staticBuffer_->AddQuad(v0, v1, v2, v3);
    }
    else
    {
        pendingStaticQuads_.push_back(v0);
        pendingStaticQuads_.push_back(v1);
        pendingStaticQuads_.push_back(v2);
        pendingStaticQuads_.push_back(v0);
        pendingStaticQuads_.push_back(v2);
        pendingStaticQuads_.push_back(v3);
    }
    return true;
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
    if (!collecting_ || !beginBuffer_ || vertices.size() < 4)
    {
        return;
    }

    beginBuffer_->AddQuadStrip(vertices.data(), vertices.size());
}

void rWallGeometryCollector::AddBeginLineStrip(const std::vector<rPackedLineVertex>& vertices)
{
    if (!collecting_ || !beginBuffer_ || vertices.size() < 2)
    {
        return;
    }

    beginBuffer_->AddLineStrip(vertices.data(), vertices.size());
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

    // Record how many segments this collector contributed this frame
    computeCountCurrent_ = sr_GetWallComputeCurrentCount() - computeStartCurrent_;

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

    // Per-frame buffers always get full upload
    if (beginBuffer_ && !beginBuffer_->IsEmpty())
    {
        success = beginBuffer_->Upload() && success;
    }
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
            if (sr_IsWallComputeActive() && computeCountPrev_ > 0)
            {
                // GPU compute covers this collector's previous-frame slice, but we
                // never delegate the newest SAFE_MARGIN segments to compute.
                // Those newest segments share their start boundary with the streaming
                // (begin/gradient) zone.  Because compute always uses last-frame data,
                // delegating them would introduce a 1-frame gap in network mode
                // (server position corrections shift the boundary by multiple segments).
                // Rendering them CPU-side keeps the boundary seamless at zero cost.
                //
                // NOTE: RenderList iterates segments newest→oldest, so they land in the
                // SSBO with index 0 = newest and index N-1 = oldest. kComputeSafeMargin
                // newest segments are skipped by compute (handled by CPU); compute starts
                // at offset kComputeSafeMargin to cover the remaining (older) segments.
                constexpr uint32_t kComputeSafeMargin = 4;
                uint32_t safeComputeCount = (computeCountPrev_ > kComputeSafeMargin)
                    ? computeCountPrev_ - kComputeSafeMargin : 0;

                uint32_t totalSegs = static_cast<uint32_t>(staticBuffer_->GetQuadCount());

                if (safeComputeCount > 0)
                {
                    unsigned int texId = RenderGetBoundTexture2D();
                    // Start at +kComputeSafeMargin to skip newest segments (CPU handles those).
                    sr_DrawComputedWallsRange(texId, computeStartPrev_ + kComputeSafeMargin, safeComputeCount);
                    // CPU renders the newest segments (safe margin + any beyond the compute slice).
                    // safeComputeCount is from last frame; if expiration shrank totalSegs, clamp to all.
                    uint32_t cpuSegs = (totalSegs > safeComputeCount) ? totalSegs - safeComputeCount : totalSegs;
                    staticBuffer_->RenderQuadsHead(cpuSegs);
                }
                else
                {
                    staticBuffer_->RenderQuads();
                }
            }
            else
            {
                staticBuffer_->RenderQuads();
            }
        }
    }

    // Begin-gradient segments: route through OpaqueDynamic + Alpha so the
    // wall surface writes depth (matches the static portion). Cel-shading's
    // depth-Sobel and bloom both depend on continuous depth across the
    // static/streaming junction; with depth-write OFF the streaming region
    // would carry floor depth and produce a sharp boundary artifact.
    if (beginBuffer_ && beginBuffer_->IsReady())
    {
        if (renderLines)
        {
            beginBuffer_->RenderLines();
        }
        if (renderQuads)
        {
            beginBuffer_->RenderQuadsBegin();
        }
    }

    // Death-fade effects: keep on the Transparent phase (depth-write OFF) so
    // nearly-invisible fragments at the tail of a dying wall don't depth-cull
    // cycles or zones driving past.
    if (streamingBuffer_ && streamingBuffer_->IsReady())
    {
        if (renderLines)
        {
            streamingBuffer_->RenderLines();
        }
        if (renderQuads)
        {
            streamingBuffer_->RenderQuadsTransparent();
        }
    }
}

size_t rWallGeometryCollector::GetStaticQuadCount() const
{
    return staticBuffer_ ? staticBuffer_->GetQuadCount() : 0;
}

size_t rWallGeometryCollector::GetStreamingQuadCount() const
{
    // Combined begin + death streaming quads (both per-frame buffers).
    size_t count = 0;
    if (beginBuffer_)     count += beginBuffer_->GetQuadCount();
    if (streamingBuffer_) count += streamingBuffer_->GetQuadCount();
    return count;
}

size_t rWallGeometryCollector::GetGPUMemoryUsage() const
{
    size_t usage = 0;
    if (staticBuffer_)    usage += staticBuffer_->GetGPUMemoryUsage();
    if (beginBuffer_)     usage += beginBuffer_->GetGPUMemoryUsage();
    if (streamingBuffer_) usage += streamingBuffer_->GetGPUMemoryUsage();
    return usage;
}

void rWallGeometryCollector::Release()
{
    if (staticBuffer_)    staticBuffer_->Release();
    if (beginBuffer_)     beginBuffer_->Release();
    if (streamingBuffer_) streamingBuffer_->Release();
    pendingStaticQuads_.clear();
    pendingStaticLines_.clear();
    staticNeedsRebuild_ = true;
}

#endif // DEDICATED
