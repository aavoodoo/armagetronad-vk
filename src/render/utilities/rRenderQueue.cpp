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

#include "rRenderQueue.h"

#ifndef DEDICATED
#include "rRender.h"              // For rRenderer
#include "rScreen.h"              // For sr_screenWidth/Height
#endif

#include <algorithm>
#include <cstring>
#include <cmath>

// Global HUD rotation for tablet multi-player cockpit alignment.
// Set per-viewport before ExecutePhase(HUD), reset to 0 after.
int sr_hudRotationDeg = 0;

//=============================================================================
// Utility: viewport remapping
//=============================================================================

// When rendering into a per-viewport FBO, the viewport IS the full render
// target — the HUD phase must NOT reset viewport to fullscreen.
bool sr_inViewportFBO = false;

//=============================================================================
// Phase configuration
//=============================================================================

static const rPhaseConfig s_phaseConfigs[] = {
    // Sky: no depth, blend enabled (floor needs alpha blend for mirror compositing)
    {false, false, true, false, "Sky"},
    // OpaqueStatic: depth on, write on
    {true, true, false, true, "OpaqueStatic"},
    // OpaqueDynamic: depth on, write on
    {true, true, false, false, "OpaqueDynamic"},
    // Transparent: depth test on, write off, blend on
    {true, false, true, false, "Transparent"},
    // Effects: depth test on, write off, blend on (additive)
    {true, false, true, false, "Effects"},
    // HUD: no depth, blend on
    {false, false, true, false, "HUD"},
};

static_assert(sizeof(s_phaseConfigs) / sizeof(s_phaseConfigs[0]) ==
                  static_cast<size_t>(rRenderPhase::COUNT),
              "Phase config count mismatch");

const char* rRenderPhaseName(rRenderPhase phase)
{
    return s_phaseConfigs[static_cast<size_t>(phase)].name;
}

const rPhaseConfig& rGetPhaseConfig(rRenderPhase phase)
{
    return s_phaseConfigs[static_cast<size_t>(phase)];
}

//=============================================================================
// rRenderQueue implementation
//=============================================================================

rRenderQueue& rRenderQueue::Instance()
{
    static rRenderQueue instance;
    return instance;
}

rRenderQueue::rRenderQueue()
    : stateInitialized_(false)
    , currentPhase_(rRenderPhase::Sky)
{
    std::memset(&frameStats_, 0, sizeof(frameStats_));
}

rRenderQueue::~rRenderQueue()
{
    ReleaseGPU();
}

void rRenderQueue::BeginFrame()
{
    // Periodically evict empty buckets to bound Clear() overhead over long sessions.
    // Evict before Clear so we don't iterate stale entries.
    static int evictCounter = 0;
    if (++evictCounter >= 600)
    {
        evictCounter = 0;
        EvictEmptyBuckets();
    }

    // Clear all remaining buckets
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        for (auto& pair : phases_[i].buckets)
        {
            pair.second->Clear();
        }
        phases_[i].sortedBuckets.clear();
    }

    // NOTE: shadowVertices_ is NOT cleared here — the shadow pass in BeginFrame
    // reads last frame's collected vertices. Cleared after the shadow pass consumes them.

    stateInitialized_ = false;
    std::memset(&frameStats_, 0, sizeof(frameStats_));
}

void rRenderQueue::EvictEmptyBuckets()
{
#ifndef DEDICATED
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        auto& buckets = phases_[i].buckets;
        for (auto it = buckets.begin(); it != buckets.end(); )
        {
            if (it->second->IsEmpty())
                it = buckets.erase(it);
            else
                ++it;
        }
        phases_[i].sortedBuckets.clear();
        phases_[i].sortDirty = true;
    }
#endif
}

rRenderBucket* rRenderQueue::GetBucket(rRenderPhase phase, const rRenderStateKey& state)
{
    auto& phaseData = phases_[static_cast<size_t>(phase)];
    phaseData.sortDirty = true;
    auto it = phaseData.buckets.find(state);

    if (it != phaseData.buckets.end())
    {
        return it->second.get();
    }

    // Create new bucket (C++11 compatible - no make_unique)
    std::unique_ptr<rRenderBucket> bucket(new rRenderBucket(state));
    rRenderBucket* ptr = bucket.get();
    phaseData.buckets[state] = std::move(bucket);
    return ptr;
}

void rRenderQueue::Submit(rRenderPhase phase, const rRenderStateKey& state,
                          const rVertex20* vertices, size_t count)
{
    if (count == 0)
        return;

    rRenderBucket* bucket = GetBucket(phase, state);
    bucket->AddTriangles(vertices, count);

    // Shadow geometry collection: static phase → persistent, dynamic → per-frame
    if (shadowCollectionEnabled_)
    {
        if (phase == rRenderPhase::OpaqueStatic && shadowStaticDirty_)
            shadowStaticVertices_.insert(shadowStaticVertices_.end(), vertices, vertices + count);
        else if (phase == rRenderPhase::OpaqueDynamic)
            shadowDynamicVertices_.insert(shadowDynamicVertices_.end(), vertices, vertices + count);
    }
}

void rRenderQueue::SubmitQuad(rRenderPhase phase, const rRenderStateKey& state,
                              const rVertex20& v0, const rVertex20& v1, const rVertex20& v2,
                              const rVertex20& v3)
{
    rRenderBucket* bucket = GetBucket(phase, state);
    bucket->AddQuad(v0, v1, v2, v3);

    // Shadow geometry collection: quads → 2 triangles
    if (shadowCollectionEnabled_)
    {
        std::vector<rVertex20>* buf = nullptr;
        if (phase == rRenderPhase::OpaqueStatic && shadowStaticDirty_)
            buf = &shadowStaticVertices_;
        else if (phase == rRenderPhase::OpaqueDynamic)
            buf = &shadowDynamicVertices_;
        if (buf)
        {
            buf->push_back(v0);
            buf->push_back(v1);
            buf->push_back(v2);
            buf->push_back(v0);
            buf->push_back(v2);
            buf->push_back(v3);
        }
    }
}

void rRenderQueue::SubmitTriangleFan(rRenderPhase phase, const rRenderStateKey& state,
                                     const rVertex20* vertices, size_t count)
{
    if (count < 3)
        return;

    rRenderBucket* bucket = GetBucket(phase, state);
    bucket->AddTriangleFan(vertices, count);

    // Shadow geometry collection: fan → triangles
    if (shadowCollectionEnabled_)
    {
        std::vector<rVertex20>* buf = nullptr;
        if (phase == rRenderPhase::OpaqueStatic && shadowStaticDirty_)
            buf = &shadowStaticVertices_;
        else if (phase == rRenderPhase::OpaqueDynamic)
            buf = &shadowDynamicVertices_;
        if (buf)
        {
            for (size_t i = 1; i + 1 < count; i++)
            {
                buf->push_back(vertices[0]);
                buf->push_back(vertices[i]);
                buf->push_back(vertices[i + 1]);
            }
        }
    }
}

void rRenderQueue::SubmitLines(rRenderPhase phase, const rRenderStateKey& state,
                               const rVertex20* vertices, size_t count)
{
    if (count == 0)
        return;

    rRenderBucket* bucket = GetBucket(phase, state);
    bucket->AddLines(vertices, count);
}

void rRenderQueue::SubmitLineStrip(rRenderPhase phase, const rRenderStateKey& state,
                                   const rVertex20* vertices, size_t count)
{
    if (count < 2)
        return;

    rRenderBucket* bucket = GetBucket(phase, state);
    bucket->AddLineStrip(vertices, count);
}

void rRenderQueue::ApplyPhaseState(rRenderPhase phase)
{
#ifndef DEDICATED
    const rPhaseConfig& config = rGetPhaseConfig(phase);

    // Apply phase state through renderer abstraction
    if (config.depthTest)
        RenderEnableState(rGLConst::DepthTest);
    else
        RenderDisableState(rGLConst::DepthTest);

    RenderDepthMask(config.depthWrite);

    if (config.blend)
        RenderEnableState(rGLConst::Blend);
    else
        RenderDisableState(rGLConst::Blend);

    if (phase == rRenderPhase::OpaqueStatic || phase == rRenderPhase::Sky)
        RenderDisableState(rGLConst::CullFace);

    if (config.clearDepth)
        RenderClear(false, true);

    // HUD phase: identity matrices. The viewport is NOT reset here —
    // callers are responsible for setting the correct viewport before
    // calling ExecutePhase(HUD). Cockpit renders at per-player sub-viewports,
    // while global overlays (console, menu, logo) set fullscreen first.
    if (phase == rRenderPhase::HUD)
    {
        ModelMatrix();
        IdentityMatrix();
        ProjMatrix();
        IdentityMatrix();
        // Apply HUD rotation (for multi-viewport cockpit on tablets)
        // Note: sr_hudRotationDeg is set but NOT applied here.
        // The proper fix for BUG 17 is to render cockpit INTO the viewport FBO
        // (before composite), so the UV rotation in the composite pass applies
        // uniformly to both 3D and cockpit. See known_issues.txt BUG 17.
    }

    currentPhase_ = phase;
#endif
}

void rRenderQueue::ApplyRenderState(const rRenderStateKey& state)
{
#ifndef DEDICATED
    // State is now applied by the renderer's DrawBatch* methods.
    // Just track for statistics.
    if (!stateInitialized_ || !(state == currentState_))
    {
        currentState_ = state;
        stateInitialized_ = true;
        frameStats_.stateChanges++;
    }
#endif
}

void rRenderQueue::ExecutePhase(rRenderPhase phase)
{
#ifndef DEDICATED
    auto& phaseData = phases_[static_cast<size_t>(phase)];

    // Only rebuild sorted bucket list when new geometry was submitted since last execute
    if (phaseData.sortDirty)
    {
        phaseData.sortedBuckets.clear();
        for (auto& pair : phaseData.buckets)
        {
            if (!pair.second->IsEmpty())
            {
                phaseData.sortedBuckets.push_back(pair.second.get());
            }
        }

        // Sort by state key (opaque first, then by texture)
        std::sort(phaseData.sortedBuckets.begin(), phaseData.sortedBuckets.end(),
                  [](const rRenderBucket* a, const rRenderBucket* b) {
                      return a->GetState() < b->GetState();
                  });
        phaseData.sortDirty = false;
    }

    if (phaseData.sortedBuckets.empty())
    {
        return;
    }

    // Apply phase state (HUD phase sets identity matrices but keeps current viewport)
    ApplyPhaseState(phase);

    // Render all buckets
    for (rRenderBucket* bucket : phaseData.sortedBuckets)
    {
        // Apply render state (textures, blending)
        ApplyRenderState(bucket->GetState());

        // Upload geometry
        bucket->Upload();

        // Render triangles (sr_PrepareForVBODraw called inside)
        if (bucket->GetTriangleVertexCount() > 0)
        {
            bucket->RenderTriangles();
            frameStats_.totalDrawCalls++;
            frameStats_.totalTriangles += bucket->GetTriangleCount();
            frameStats_.totalVertices += bucket->GetTriangleVertexCount();
        }

        // Render lines (sr_PrepareForVBODraw called inside)
        if (bucket->GetLineVertexCount() > 0)
        {
            bucket->RenderLines();
            frameStats_.totalDrawCalls++;
            frameStats_.totalLines += bucket->GetLineCount();
            frameStats_.totalVertices += bucket->GetLineVertexCount();
        }

        // Clear bucket after rendering to prevent re-rendering on next ExecutePhase call
        bucket->Clear();
    }

    frameStats_.totalBuckets += phaseData.sortedBuckets.size();

    // Restore depth state to known defaults for the 3D pipeline.
    RenderDepthMask(true);
    RenderEnableState(rGLConst::DepthTest);
#endif
}

void rRenderQueue::Execute()
{
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        ExecutePhase(static_cast<rRenderPhase>(i));
    }
}

rRenderQueue::PhaseStats rRenderQueue::GetPhaseStats(rRenderPhase phase) const
{
    PhaseStats stats = {};
    const auto& phaseData = phases_[static_cast<size_t>(phase)];

    for (const auto& pair : phaseData.buckets)
    {
        if (!pair.second->IsEmpty())
        {
            stats.bucketCount++;
            stats.triangleCount += pair.second->GetTriangleCount();
            stats.lineCount += pair.second->GetLineCount();
            stats.vertexCount +=
                pair.second->GetTriangleVertexCount() + pair.second->GetLineVertexCount();

            // Each non-empty primitive type = 1 draw call
            if (pair.second->GetTriangleVertexCount() > 0)
                stats.drawCalls++;
            if (pair.second->GetLineVertexCount() > 0)
                stats.drawCalls++;
        }
    }

    return stats;
}

rRenderQueue::FrameStats rRenderQueue::GetFrameStats() const
{
    return frameStats_;
}

bool rRenderQueue::IsEmpty() const
{
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        for (const auto& pair : phases_[i].buckets)
        {
            if (!pair.second->IsEmpty())
            {
                return false;
            }
        }
    }
    return true;
}

void rRenderQueue::ReleaseGPU()
{
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        for (auto& pair : phases_[i].buckets)
        {
            pair.second->ReleaseGPU();
        }
    }
}
