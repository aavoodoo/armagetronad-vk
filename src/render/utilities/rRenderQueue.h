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

#ifndef RRENDERQUEUE_H
#define RRENDERQUEUE_H

#include "rRenderBucket.h"
#include <memory>
#include <unordered_map>
#include <vector>

//! @file rRenderQueue.h
//! Phase-based render queue for the modern GL3 renderer.
//!
//! Utility: RemapVerticesToFullscreen() converts vertex positions from the
//! current sub-viewport's NDC to fullscreen NDC for HUD phase rendering.

//! Phase-based render queue for the modern GL3 renderer.
//!
//! The render queue organizes geometry into phases (sky, opaque, transparent, etc.)
//! and buckets (by render state). This enables efficient batching and proper
//! rendering order.

//=============================================================================
// Render phases
//=============================================================================

//! Rendering phases executed in order
enum class rRenderPhase
{
    Sky,           //!< Background sky (no depth test/write)
    OpaqueStatic,  //!< Static opaque geometry (floor, rim walls)
    OpaqueDynamic, //!< Dynamic opaque geometry (player walls)
    Transparent,   //!< Transparent geometry (zones, alpha walls)
    Effects,       //!< Additive effects (sparks, explosions)
    HUD,           //!< 2D HUD overlay (no depth)
    COUNT
};

//! Get phase name for debugging
const char* rRenderPhaseName(rRenderPhase phase);

//=============================================================================
// Phase configuration
//=============================================================================

//! Configuration for a render phase
struct rPhaseConfig
{
    bool depthTest;    //!< Enable depth testing
    bool depthWrite;   //!< Enable depth buffer writes
    bool blend;        //!< Enable alpha blending
    bool clearDepth;   //!< Clear depth buffer before phase
    const char* name;  //!< Debug name
};

//! Get configuration for a phase
const rPhaseConfig& rGetPhaseConfig(rRenderPhase phase);

//=============================================================================
// Render queue
//=============================================================================

//! Phase-based render queue that organizes geometry into buckets by render state.
//!
//! Usage:
//! 1. Call BeginFrame() at start of frame
//! 2. Submit geometry via Submit() or SubmitQuad() etc.
//! 3. Call Execute() to render all phases in order
//!
class rRenderQueue
{
public:
    //! Get the singleton instance
    static rRenderQueue& Instance();

    //! Begin a new frame (clears all buckets)
    void BeginFrame();

    //! Submit triangle geometry to a phase
    void Submit(rRenderPhase phase, const rRenderStateKey& state, const rVertex20* vertices,
                size_t count);

    //! Submit a single quad (converted to 2 triangles)
    void SubmitQuad(rRenderPhase phase, const rRenderStateKey& state, const rVertex20& v0,
                    const rVertex20& v1, const rVertex20& v2, const rVertex20& v3);

    //! Submit a triangle fan
    void SubmitTriangleFan(rRenderPhase phase, const rRenderStateKey& state,
                           const rVertex20* vertices, size_t count);

    //! Submit line geometry to a phase
    void SubmitLines(rRenderPhase phase, const rRenderStateKey& state, const rVertex20* vertices,
                     size_t count);

    //! Submit a line strip
    void SubmitLineStrip(rRenderPhase phase, const rRenderStateKey& state,
                         const rVertex20* vertices, size_t count);

    //! Execute all phases in order
    //! Uploads geometry, applies states, issues draw calls
    void Execute();

    //! Execute a single phase (for debugging/custom rendering)
    void ExecutePhase(rRenderPhase phase);

    //! Get statistics for a phase
    struct PhaseStats
    {
        size_t bucketCount;
        size_t drawCalls;
        size_t triangleCount;
        size_t lineCount;
        size_t vertexCount;
    };
    PhaseStats GetPhaseStats(rRenderPhase phase) const;

    //! Get total statistics for last frame
    struct FrameStats
    {
        size_t totalBuckets;
        size_t totalDrawCalls;
        size_t totalTriangles;
        size_t totalLines;
        size_t totalVertices;
        size_t stateChanges;
    };
    FrameStats GetFrameStats() const;

    //! Check if queue is empty
    bool IsEmpty() const;

    //! Evict empty buckets to reduce per-frame Clear overhead.
    //! Call between rounds/matches when the render state set changes.
    void EvictEmptyBuckets();

    //! Release all GPU resources (call on context loss)
    void ReleaseGPU();

private:
    rRenderQueue();
    ~rRenderQueue();

    // Non-copyable
    rRenderQueue(const rRenderQueue&) = delete;
    rRenderQueue& operator=(const rRenderQueue&) = delete;

    //! Get or create bucket for state in phase
    rRenderBucket* GetBucket(rRenderPhase phase, const rRenderStateKey& state);

    //! Apply phase state (depth, blend, etc.)
    void ApplyPhaseState(rRenderPhase phase);

    //! Apply render state key (texture, blend mode, flags)
    void ApplyRenderState(const rRenderStateKey& state);

    //! Per-phase data
    struct PhaseData
    {
        std::unordered_map<rRenderStateKey, std::unique_ptr<rRenderBucket>, rRenderStateKeyHash>
            buckets;
        std::vector<rRenderBucket*> sortedBuckets; // Sorted for optimal rendering
        bool sortDirty = true; // Rebuild sorted list only when geometry was submitted
    };

    PhaseData phases_[static_cast<size_t>(rRenderPhase::COUNT)];

public:
    //! Read-only access to phase data (for shadow pass geometry iteration)
    const PhaseData& GetPhaseData(rRenderPhase phase) const
    {
        return phases_[static_cast<size_t>(phase)];
    }
private:

    // Current state tracking (for minimizing state changes)
    rRenderStateKey currentState_;
    bool stateInitialized_;
    rRenderPhase currentPhase_;

    // Shadow geometry collection (FR13)
    // Split into static (rim walls — persisted across frames) and dynamic
    // (player walls, cycles — collected each frame). The shadow pass draws both.
    std::vector<rVertex20> shadowStaticVertices_;   // rim walls, persisted
    std::vector<rVertex20> shadowDynamicVertices_;  // player walls, per-frame
    bool shadowCollectionEnabled_ = false;
    bool shadowStaticDirty_ = true;  // true = rebuild static next frame

public:
    //! Enable/disable shadow geometry collection for current frame
    void SetShadowCollection(bool enabled) { shadowCollectionEnabled_ = enabled; }

    //! Check if shadow collection is enabled
    bool IsShadowCollectionEnabled() const { return shadowCollectionEnabled_; }

    //! Get combined shadow vertices (static + dynamic, for shadow pass)
    const std::vector<rVertex20>& GetShadowStaticVertices() const { return shadowStaticVertices_; }
    const std::vector<rVertex20>& GetShadowDynamicVertices() const { return shadowDynamicVertices_; }

    //! Get mutable dynamic shadow vertices (for adding lit geometry from DrawBatch)
    std::vector<rVertex20>& GetShadowDynamicVerticesMut() { return shadowDynamicVertices_; }

    //! Clear dynamic shadow vertices (called after shadow pass consumes them)
    void ClearShadowDynamic() { shadowDynamicVertices_.clear(); }

    //! Mark static shadow geometry as needing rebuild (e.g., after round start)
    void InvalidateShadowStatic() { shadowStaticDirty_ = true; }

    //! Check if static shadow geometry needs rebuild
    bool IsShadowStaticDirty() const { return shadowStaticDirty_; }

    //! Mark static shadow geometry as built
    void SetShadowStaticClean() { shadowStaticDirty_ = false; }

    //! Clear all shadow vertices
    void ClearShadowVertices() { shadowDynamicVertices_.clear(); }
private:

    // Frame statistics
    mutable FrameStats frameStats_;
};

//=============================================================================
// Convenience macros for common submissions
//=============================================================================

//! Submit colored quad to a phase
inline void rSubmitColoredQuad(rRenderPhase phase, float x0, float y0, float z0, float x1,
                               float y1, float z1, float x2, float y2, float z2, float x3,
                               float y3, float z3, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    rVertex20 v0, v1, v2, v3;
    v0.SetPosition(x0, y0, z0);
    v0.SetColor(r, g, b, a);
    v1.SetPosition(x1, y1, z1);
    v1.SetColor(r, g, b, a);
    v2.SetPosition(x2, y2, z2);
    v2.SetColor(r, g, b, a);
    v3.SetPosition(x3, y3, z3);
    v3.SetColor(r, g, b, a);

    rRenderQueue::Instance().SubmitQuad(phase, rRenderStateKey::Colored(), v0, v1, v2, v3);
}

#endif // RRENDERQUEUE_H
