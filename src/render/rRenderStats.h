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

#ifndef RRENDERSTATS_H
#define RRENDERSTATS_H

#include "rRenderQueue.h"
#include <cstddef>
#include <cstdint>

//! @file rRenderStats.h
//! Rendering statistics and profiling for the modern GL3 renderer.
//!
//! Collects per-frame statistics including:
//! - Draw call counts
//! - Vertex/triangle counts
//! - State change counts
//! - Timing information (optional GPU queries)
//! - Per-phase breakdown
//!
//! Enable with RENDER_DEBUG_STATS build flag or runtime config.

//=============================================================================
// Build-time configuration
//=============================================================================

//! Set to 1 to enable statistics collection (slight overhead)
#ifndef RENDER_DEBUG_STATS
#define RENDER_DEBUG_STATS 1
#endif

//! Set to 1 to enable GPU timing queries (moderate overhead)
#ifndef RENDER_PROFILE_GPU
#define RENDER_PROFILE_GPU 0
#endif

//! Set to 1 to enable overdraw visualization mode
#ifndef RENDER_DEBUG_OVERDRAW
#define RENDER_DEBUG_OVERDRAW 0
#endif

//=============================================================================
// Per-phase statistics
//=============================================================================

struct rPhaseStats
{
    const char* name;       //!< Phase name
    double timeMs;          //!< Time spent in phase (ms)
    size_t drawCalls;       //!< Number of draw calls
    size_t triangles;       //!< Number of triangles rendered
    size_t lines;           //!< Number of lines rendered
    size_t vertices;        //!< Total vertices
    size_t stateChanges;    //!< State changes within phase
    size_t bytesUploaded;   //!< Bytes uploaded to GPU

    rPhaseStats()
        : name(nullptr)
        , timeMs(0.0)
        , drawCalls(0)
        , triangles(0)
        , lines(0)
        , vertices(0)
        , stateChanges(0)
        , bytesUploaded(0)
    {
    }

    void Reset()
    {
        timeMs = 0.0;
        drawCalls = 0;
        triangles = 0;
        lines = 0;
        vertices = 0;
        stateChanges = 0;
        bytesUploaded = 0;
    }
};

//=============================================================================
// Frame statistics
//=============================================================================

struct rFrameStats
{
    // Timing
    double frameTimeMs;     //!< Total frame time (ms)
    double uploadTimeMs;    //!< Time spent uploading to GPU
    double drawTimeMs;      //!< Time spent in draw calls
    double cpuTimeMs;       //!< CPU-side time (collection, sorting)

    // Counts
    size_t drawCalls;       //!< Total draw calls
    size_t triangles;       //!< Total triangles
    size_t lines;           //!< Total lines
    size_t vertices;        //!< Total vertices
    size_t stateChanges;    //!< Total state changes
    size_t shaderSwitches;  //!< Shader program switches
    size_t textureSwitches; //!< Texture binding changes
    size_t bytesUploaded;        //!< Total bytes uploaded to GPU
    size_t descriptorPoolCount;  //!< Number of live descriptor pools (grows on exhaustion, shrinks when empty)

    // Per-phase breakdown
    rPhaseStats phases[static_cast<size_t>(rRenderPhase::COUNT)];

    // Frame number
    uint64_t frameNumber;

    rFrameStats()
        : frameTimeMs(0.0)
        , uploadTimeMs(0.0)
        , drawTimeMs(0.0)
        , cpuTimeMs(0.0)
        , drawCalls(0)
        , triangles(0)
        , lines(0)
        , vertices(0)
        , stateChanges(0)
        , shaderSwitches(0)
        , textureSwitches(0)
        , bytesUploaded(0)
        , descriptorPoolCount(0)
        , frameNumber(0)
    {
    }

    void Reset()
    {
        frameTimeMs = 0.0;
        uploadTimeMs = 0.0;
        drawTimeMs = 0.0;
        cpuTimeMs = 0.0;
        drawCalls = 0;
        triangles = 0;
        lines = 0;
        vertices = 0;
        stateChanges = 0;
        shaderSwitches = 0;
        textureSwitches = 0;
        bytesUploaded = 0;
        descriptorPoolCount = 0;

        for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
        {
            phases[i].Reset();
        }
    }
};

//=============================================================================
// Render statistics collector
//=============================================================================

//! Collects and aggregates rendering statistics.
//! Use BeginFrame/EndFrame to bracket a frame, and the various Add* methods
//! to record events during rendering.
class rRenderStats
{
public:
    //! Get singleton instance
    static rRenderStats& Instance();

    //! Check if statistics collection is enabled
    [[nodiscard]] bool IsEnabled() const { return enabled_; }

    //! Enable/disable statistics collection
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    //-------------------------------------------------------------------------
    // Frame lifecycle
    //-------------------------------------------------------------------------

    //! Begin a new frame
    void BeginFrame();

    //! End the current frame
    void EndFrame();

    //! Begin a phase
    void BeginPhase(rRenderPhase phase);

    //! End the current phase
    void EndPhase();

    //-------------------------------------------------------------------------
    // Event recording
    //-------------------------------------------------------------------------

    //! Record a draw call
    void AddDrawCall(size_t vertices, size_t triangles, size_t lines = 0);

    //! Record a state change
    void AddStateChange();

    //! Record a shader switch
    void AddShaderSwitch();

    //! Record a texture switch
    void AddTextureSwitch();

    //! Record bytes uploaded
    void AddBytesUploaded(size_t bytes);

    //! Set current descriptor pool count (call once per frame from the renderer)
    void SetDescriptorPoolCount(size_t count) { currentFrame_.descriptorPoolCount = count; }

    //-------------------------------------------------------------------------
    // Query results
    //-------------------------------------------------------------------------

    //! Get statistics for the last completed frame
    [[nodiscard]] const rFrameStats& GetLastFrameStats() const { return lastFrame_; }

    //! Get statistics for the current (incomplete) frame
    [[nodiscard]] const rFrameStats& GetCurrentFrameStats() const { return currentFrame_; }

    //! Get average statistics over recent frames
    [[nodiscard]] rFrameStats GetAverageStats(int frameCount = 60) const;

    //! Get current FPS (based on rolling average)
    [[nodiscard]] float GetFPS() const;

    //! Get frame number
    [[nodiscard]] uint64_t GetFrameNumber() const { return frameNumber_; }

    //-------------------------------------------------------------------------
    // Debug rendering
    //-------------------------------------------------------------------------

    //! Render statistics overlay (call after all rendering)
    void RenderOverlay();

    //! Set overlay visibility
    void SetOverlayVisible(bool visible) { overlayVisible_ = visible; }
    [[nodiscard]] bool IsOverlayVisible() const { return overlayVisible_; }

    //! Set overdraw visualization mode
    void SetOverdrawMode(bool enabled) { overdrawMode_ = enabled; }
    [[nodiscard]] bool IsOverdrawMode() const { return overdrawMode_; }

    //! Set wireframe mode
    void SetWireframeMode(bool enabled) { wireframeMode_ = enabled; }
    [[nodiscard]] bool IsWireframeMode() const { return wireframeMode_; }

private:
    rRenderStats();
    ~rRenderStats() = default;

    // Non-copyable
    rRenderStats(const rRenderStats&) = delete;
    rRenderStats& operator=(const rRenderStats&) = delete;

    bool enabled_;

    // Current frame accumulator
    rFrameStats currentFrame_;

    // Last complete frame
    rFrameStats lastFrame_;

    // History for averaging
    static const int HISTORY_SIZE = 120;
    rFrameStats history_[HISTORY_SIZE];
    int historyIndex_;
    int historyCount_;

    // Frame counter
    uint64_t frameNumber_;

    // Timing
    double frameStartTime_;
    double phaseStartTime_;
    rRenderPhase currentPhase_;

    // Debug modes
    bool overlayVisible_;
    bool overdrawMode_;
    bool wireframeMode_;

    // Get current time in milliseconds
    [[nodiscard]] double GetTimeMs() const;
};

//=============================================================================
// Scoped helpers
//=============================================================================

//! RAII helper for phase timing
class rScopedPhase
{
public:
    explicit rScopedPhase(rRenderPhase phase)
    {
#if RENDER_DEBUG_STATS
        rRenderStats::Instance().BeginPhase(phase);
#endif
    }

    ~rScopedPhase()
    {
#if RENDER_DEBUG_STATS
        rRenderStats::Instance().EndPhase();
#endif
    }

private:
    rScopedPhase(const rScopedPhase&) = delete;
    rScopedPhase& operator=(const rScopedPhase&) = delete;
};

//! RAII helper for frame timing
class rScopedFrame
{
public:
    rScopedFrame()
    {
#if RENDER_DEBUG_STATS
        rRenderStats::Instance().BeginFrame();
#endif
    }

    ~rScopedFrame()
    {
#if RENDER_DEBUG_STATS
        rRenderStats::Instance().EndFrame();
#endif
    }

private:
    rScopedFrame(const rScopedFrame&) = delete;
    rScopedFrame& operator=(const rScopedFrame&) = delete;
};

//=============================================================================
// Convenience macros
//=============================================================================

#if RENDER_DEBUG_STATS
#define RENDER_STATS_BEGIN_FRAME() rRenderStats::Instance().BeginFrame()
#define RENDER_STATS_END_FRAME() rRenderStats::Instance().EndFrame()
#define RENDER_STATS_BEGIN_PHASE(phase) rRenderStats::Instance().BeginPhase(phase)
#define RENDER_STATS_END_PHASE() rRenderStats::Instance().EndPhase()
#define RENDER_STATS_ADD_DRAW(verts, tris, lines) rRenderStats::Instance().AddDrawCall(verts, tris, lines)
#define RENDER_STATS_ADD_STATE_CHANGE() rRenderStats::Instance().AddStateChange()
#define RENDER_STATS_ADD_UPLOAD(bytes) rRenderStats::Instance().AddBytesUploaded(bytes)
#else
#define RENDER_STATS_BEGIN_FRAME() ((void)0)
#define RENDER_STATS_END_FRAME() ((void)0)
#define RENDER_STATS_BEGIN_PHASE(phase) ((void)0)
#define RENDER_STATS_END_PHASE() ((void)0)
#define RENDER_STATS_ADD_DRAW(verts, tris, lines) ((void)0)
#define RENDER_STATS_ADD_STATE_CHANGE() ((void)0)
#define RENDER_STATS_ADD_UPLOAD(bytes) ((void)0)
#endif

#endif // RRENDERSTATS_H
