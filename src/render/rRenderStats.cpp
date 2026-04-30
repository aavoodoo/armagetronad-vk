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

#include "rRenderStats.h"

#include <chrono>
#include <cstdio>
#include <cstring>

//=============================================================================
// rRenderStats implementation
//=============================================================================

rRenderStats& rRenderStats::Instance()
{
    static rRenderStats instance;
    return instance;
}

rRenderStats::rRenderStats()
    : enabled_(true)
    , historyIndex_(0)
    , historyCount_(0)
    , frameNumber_(0)
    , frameStartTime_(0.0)
    , phaseStartTime_(0.0)
    , currentPhase_(rRenderPhase::Sky)
    , overlayVisible_(false)
    , overdrawMode_(false)
    , wireframeMode_(false)
{
    currentFrame_.Reset();
    lastFrame_.Reset();

    // Initialize phase names
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        currentFrame_.phases[i].name = rRenderPhaseName(static_cast<rRenderPhase>(i));
        lastFrame_.phases[i].name = currentFrame_.phases[i].name;
    }
}

double rRenderStats::GetTimeMs() const
{
    using namespace std::chrono;
    auto now = high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return duration_cast<microseconds>(duration).count() / 1000.0;
}

void rRenderStats::BeginFrame()
{
    if (!enabled_)
        return;

    frameStartTime_ = GetTimeMs();
    currentFrame_.Reset();
    currentFrame_.frameNumber = frameNumber_;

    // Re-initialize phase names
    for (size_t i = 0; i < static_cast<size_t>(rRenderPhase::COUNT); ++i)
    {
        currentFrame_.phases[i].name = rRenderPhaseName(static_cast<rRenderPhase>(i));
    }
}

void rRenderStats::EndFrame()
{
    if (!enabled_)
        return;

    double endTime = GetTimeMs();
    currentFrame_.frameTimeMs = endTime - frameStartTime_;

    // Store in last frame
    lastFrame_ = currentFrame_;

    // Add to history
    history_[historyIndex_] = currentFrame_;
    historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;
    if (historyCount_ < HISTORY_SIZE)
    {
        historyCount_++;
    }

    frameNumber_++;
}

void rRenderStats::BeginPhase(rRenderPhase phase)
{
    if (!enabled_)
        return;

    phaseStartTime_ = GetTimeMs();
    currentPhase_ = phase;
}

void rRenderStats::EndPhase()
{
    if (!enabled_)
        return;

    double endTime = GetTimeMs();
    size_t phaseIndex = static_cast<size_t>(currentPhase_);
    currentFrame_.phases[phaseIndex].timeMs += endTime - phaseStartTime_;
}

void rRenderStats::AddDrawCall(size_t vertices, size_t triangles, size_t lines)
{
    if (!enabled_)
        return;

    currentFrame_.drawCalls++;
    currentFrame_.vertices += vertices;
    currentFrame_.triangles += triangles;
    currentFrame_.lines += lines;

    size_t phaseIndex = static_cast<size_t>(currentPhase_);
    currentFrame_.phases[phaseIndex].drawCalls++;
    currentFrame_.phases[phaseIndex].vertices += vertices;
    currentFrame_.phases[phaseIndex].triangles += triangles;
    currentFrame_.phases[phaseIndex].lines += lines;
}

void rRenderStats::AddStateChange()
{
    if (!enabled_)
        return;

    currentFrame_.stateChanges++;

    size_t phaseIndex = static_cast<size_t>(currentPhase_);
    currentFrame_.phases[phaseIndex].stateChanges++;
}

void rRenderStats::AddShaderSwitch()
{
    if (!enabled_)
        return;

    currentFrame_.shaderSwitches++;
}

void rRenderStats::AddTextureSwitch()
{
    if (!enabled_)
        return;

    currentFrame_.textureSwitches++;
}

void rRenderStats::AddBytesUploaded(size_t bytes)
{
    if (!enabled_)
        return;

    currentFrame_.bytesUploaded += bytes;

    size_t phaseIndex = static_cast<size_t>(currentPhase_);
    currentFrame_.phases[phaseIndex].bytesUploaded += bytes;
}

rFrameStats rRenderStats::GetAverageStats(int frameCount) const
{
    rFrameStats avg;

    if (historyCount_ == 0)
    {
        return avg;
    }

    int count = (frameCount < historyCount_) ? frameCount : historyCount_;

    for (int i = 0; i < count; ++i)
    {
        int idx = (historyIndex_ - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
        const rFrameStats& frame = history_[idx];

        avg.frameTimeMs += frame.frameTimeMs;
        avg.uploadTimeMs += frame.uploadTimeMs;
        avg.drawTimeMs += frame.drawTimeMs;
        avg.cpuTimeMs += frame.cpuTimeMs;
        avg.drawCalls += frame.drawCalls;
        avg.triangles += frame.triangles;
        avg.lines += frame.lines;
        avg.vertices += frame.vertices;
        avg.stateChanges += frame.stateChanges;
        avg.shaderSwitches += frame.shaderSwitches;
        avg.textureSwitches += frame.textureSwitches;
        avg.bytesUploaded += frame.bytesUploaded;
        // Pool count: take the most recent value (not a mean) — it's a current state metric.
        // We just carry it forward from the last history entry; the loop overwrites it each time.
        avg.descriptorPoolCount = frame.descriptorPoolCount;

        for (size_t j = 0; j < static_cast<size_t>(rRenderPhase::COUNT); ++j)
        {
            avg.phases[j].timeMs += frame.phases[j].timeMs;
            avg.phases[j].drawCalls += frame.phases[j].drawCalls;
            avg.phases[j].triangles += frame.phases[j].triangles;
            avg.phases[j].lines += frame.phases[j].lines;
            avg.phases[j].vertices += frame.phases[j].vertices;
            avg.phases[j].stateChanges += frame.phases[j].stateChanges;
            avg.phases[j].bytesUploaded += frame.phases[j].bytesUploaded;
        }
    }

    // Average
    double invCount = 1.0 / count;
    avg.frameTimeMs *= invCount;
    avg.uploadTimeMs *= invCount;
    avg.drawTimeMs *= invCount;
    avg.cpuTimeMs *= invCount;
    avg.drawCalls = static_cast<size_t>(avg.drawCalls * invCount);
    avg.triangles = static_cast<size_t>(avg.triangles * invCount);
    avg.lines = static_cast<size_t>(avg.lines * invCount);
    avg.vertices = static_cast<size_t>(avg.vertices * invCount);
    avg.stateChanges = static_cast<size_t>(avg.stateChanges * invCount);
    avg.shaderSwitches = static_cast<size_t>(avg.shaderSwitches * invCount);
    avg.textureSwitches = static_cast<size_t>(avg.textureSwitches * invCount);
    avg.bytesUploaded = static_cast<size_t>(avg.bytesUploaded * invCount);

    for (size_t j = 0; j < static_cast<size_t>(rRenderPhase::COUNT); ++j)
    {
        avg.phases[j].name = rRenderPhaseName(static_cast<rRenderPhase>(j));
        avg.phases[j].timeMs *= invCount;
        avg.phases[j].drawCalls = static_cast<size_t>(avg.phases[j].drawCalls * invCount);
        avg.phases[j].triangles = static_cast<size_t>(avg.phases[j].triangles * invCount);
        avg.phases[j].lines = static_cast<size_t>(avg.phases[j].lines * invCount);
        avg.phases[j].vertices = static_cast<size_t>(avg.phases[j].vertices * invCount);
        avg.phases[j].stateChanges = static_cast<size_t>(avg.phases[j].stateChanges * invCount);
        avg.phases[j].bytesUploaded = static_cast<size_t>(avg.phases[j].bytesUploaded * invCount);
    }

    return avg;
}

float rRenderStats::GetFPS() const
{
    rFrameStats avg = GetAverageStats(30);
    if (avg.frameTimeMs > 0.0)
    {
        return static_cast<float>(1000.0 / avg.frameTimeMs);
    }
    return 0.0f;
}

void rRenderStats::RenderOverlay()
{
    if (!overlayVisible_ || !enabled_)
        return;

    // This would render text overlay using the font system
    // For now, just log to console periodically

#ifndef DEDICATED
    // Every 60 frames, print stats
    if (frameNumber_ % 60 == 0)
    {
        rFrameStats avg = GetAverageStats(60);

        // Format: FPS: XXX | Draws: XX | Tris: XXXXX | State: XX
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      "[RenderStats] FPS: %.1f | Frame: %.2fms | Draws: %zu | Tris: %zu | "
                      "Verts: %zu | States: %zu | DescPools: %zu",
                      GetFPS(), avg.frameTimeMs, avg.drawCalls, avg.triangles, avg.vertices,
                      avg.stateChanges, avg.descriptorPoolCount);

        // Would use tOutput or con << here
        // For now just rely on external code to query and display
    }
#endif
}
