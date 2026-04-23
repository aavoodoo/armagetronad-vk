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

#include "rEffectsRenderer.h"

#ifndef DEDICATED

#include "rRenderQueue.h"
#include "rVertex.h"
#include "tConfiguration.h"

#include <algorithm>
#include <cmath>
#include <vector>

//=============================================================================
// Configuration
//=============================================================================

bool sr_useBatchedEffects = true;  // Enabled by default for Vulkan-ready batch rendering
static tSettingItem<bool> conf_useBatchedEffects("USE_BATCHED_EFFECTS", sr_useBatchedEffects);

//=============================================================================
// Statistics tracking
//=============================================================================

namespace
{

struct EffectsStats
{
    size_t explosionsSubmitted;
    size_t sparksSubmitted;
    size_t linesGenerated;

    EffectsStats() : explosionsSubmitted(0), sparksSubmitted(0), linesGenerated(0) {}

    void Reset()
    {
        explosionsSubmitted = 0;
        sparksSubmitted = 0;
        linesGenerated = 0;
    }
};

EffectsStats sg_effectsStats;

// Spark batching state
bool sg_sparkBatchActive = false;
std::vector<rVertex20> sg_sparkBatchVertices;

} // anonymous namespace

//=============================================================================
// Helper functions
//=============================================================================

namespace
{

//! Convert effect blend mode to render state key
rRenderStateKey GetEffectStateKey(rEffectBlendMode mode)
{
    switch (mode)
    {
    case rEffectBlendMode::Additive:
        return rRenderStateKey::Colored(rBlendMode::Additive);
    case rEffectBlendMode::Alpha:
    default:
        return rRenderStateKey::Colored(rBlendMode::Alpha);
    }
}

//! Add a line to vertex array
void AddLine(std::vector<rVertex20>& vertices, float x1, float y1, float z1, float x2, float y2,
             float z2, float r, float g, float b, float a)
{
    uint8_t rb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, r)) * 255.0f);
    uint8_t gb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, g)) * 255.0f);
    uint8_t bb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, b)) * 255.0f);
    uint8_t ab = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, a)) * 255.0f);

    rVertex20 v1, v2;
    v1.SetPosition(x1, y1, z1);
    v1.SetColor(rb, gb, bb, ab);
    v2.SetPosition(x2, y2, z2);
    v2.SetColor(rb, gb, bb, ab);

    vertices.push_back(v1);
    vertices.push_back(v2);
}

} // anonymous namespace

//=============================================================================
// Explosion rendering
//=============================================================================

void rSubmitExplosion(float posX, float posY, float posZ, float outerRadius, float innerRadius,
                      float r, float g, float b, float a, const float* directions,
                      size_t numDirections, bool useHUDPhase)
{
    if (!sr_useBatchedEffects || numDirections == 0)
        return;

    // Generate lines from outer to inner radius along each direction
    std::vector<rVertex20> vertices;
    vertices.reserve(numDirections * 2);

    uint8_t rb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, r)) * 255.0f);
    uint8_t gb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, g)) * 255.0f);
    uint8_t bb = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, b)) * 255.0f);
    uint8_t ab = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, a)) * 255.0f);

    for (size_t i = 0; i < numDirections; ++i)
    {
        float dx = directions[i * 3];
        float dy = directions[i * 3 + 1];
        float dz = directions[i * 3 + 2];

        // Outer point
        rVertex20 v1;
        v1.SetPosition(posX + dx * outerRadius, posY + dy * outerRadius, posZ + dz * outerRadius);
        v1.SetColor(rb, gb, bb, ab);

        // Inner point
        rVertex20 v2;
        v2.SetPosition(posX + dx * innerRadius, posY + dy * innerRadius, posZ + dz * innerRadius);
        v2.SetColor(rb, gb, bb, ab);

        vertices.push_back(v1);
        vertices.push_back(v2);
    }

    // 2D map rendering uses Sky phase (like cycles) so the map's matrix transform
    // is applied correctly and ExecutePhase is called immediately by the caller.
    // 3D game rendering uses Effects phase (depth test, additive blend).
    rRenderStateKey state = useHUDPhase ? rRenderStateKey::HUD(0, rBlendMode::Alpha)
                                        : rRenderStateKey::Colored(rBlendMode::Alpha);
    rRenderPhase phase = useHUDPhase ? rRenderPhase::Sky : rRenderPhase::Effects;
    rRenderQueue::Instance().SubmitLines(phase, state, vertices.data(),
                                         vertices.size());

    sg_effectsStats.explosionsSubmitted++;
    sg_effectsStats.linesGenerated += numDirections;
}

void rSubmitExplosionSimple(float posX, float posY, float posZ, float outerRadius,
                            float innerRadius, float r, float g, float b, float a, int numRays)
{
    if (!sr_useBatchedEffects || numRays <= 0)
        return;

    // Generate default radial directions
    std::vector<float> directions;
    directions.reserve(numRays * 3);

    float angleStep = 2.0f * static_cast<float>(M_PI) / numRays;

    for (int i = 0; i < numRays; ++i)
    {
        float angle = i * angleStep;
        directions.push_back(std::cos(angle)); // x
        directions.push_back(std::sin(angle)); // y
        directions.push_back(0.0f);            // z (2D radial)
    }

    rSubmitExplosion(posX, posY, posZ, outerRadius, innerRadius, r, g, b, a, directions.data(),
                     numRays);
}

//=============================================================================
// Spark rendering
//=============================================================================

void rSubmitSparkTrail(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g,
                       float b, float a, rEffectBlendMode blendMode)
{
    if (!sr_useBatchedEffects)
        return;

    std::vector<rVertex20> vertices;
    AddLine(vertices, x1, y1, z1, x2, y2, z2, r, g, b, a);

    rRenderStateKey state = GetEffectStateKey(blendMode);
    rRenderQueue::Instance().SubmitLines(rRenderPhase::Effects, state, vertices.data(),
                                         vertices.size());

    sg_effectsStats.sparksSubmitted++;
    sg_effectsStats.linesGenerated++;
}

void rBeginSparkBatch()
{
    if (!sr_useBatchedEffects)
        return;

    sg_sparkBatchActive = true;
    sg_sparkBatchVertices.clear();
    sg_sparkBatchVertices.reserve(128); // Pre-allocate for typical spark count
}

void rEndSparkBatch()
{
    if (!sr_useBatchedEffects || !sg_sparkBatchActive)
        return;

    sg_sparkBatchActive = false;

    if (sg_sparkBatchVertices.empty())
        return;

    // Sparks use additive blending for bright glow effect
    rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Additive);
    rRenderQueue::Instance().SubmitLines(rRenderPhase::Effects, state, sg_sparkBatchVertices.data(),
                                         sg_sparkBatchVertices.size());

    sg_effectsStats.linesGenerated += sg_sparkBatchVertices.size() / 2;
}

void rBatchSparkTrail(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g,
                      float b, float a)
{
    if (!sr_useBatchedEffects || !sg_sparkBatchActive)
        return;

    AddLine(sg_sparkBatchVertices, x1, y1, z1, x2, y2, z2, r, g, b, a);
    sg_effectsStats.sparksSubmitted++;
}

//=============================================================================
// Generic line effects
//=============================================================================

void rSubmitEffectLine(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g,
                       float b, float a, rEffectBlendMode blendMode)
{
    if (!sr_useBatchedEffects)
        return;

    std::vector<rVertex20> vertices;
    AddLine(vertices, x1, y1, z1, x2, y2, z2, r, g, b, a);

    rRenderStateKey state = GetEffectStateKey(blendMode);
    rRenderQueue::Instance().SubmitLines(rRenderPhase::Effects, state, vertices.data(),
                                         vertices.size());

    sg_effectsStats.linesGenerated++;
}

//=============================================================================
// Frame lifecycle
//=============================================================================

void rEffectsRendererBeginFrame()
{
    sg_effectsStats.Reset();
    sg_sparkBatchActive = false;
    sg_sparkBatchVertices.clear();
}

rEffectsRenderStats rEffectsRendererGetStats()
{
    rEffectsRenderStats stats;
    stats.explosionsSubmitted = sg_effectsStats.explosionsSubmitted;
    stats.sparksSubmitted = sg_effectsStats.sparksSubmitted;
    stats.linesGenerated = sg_effectsStats.linesGenerated;
    return stats;
}

#else // DEDICATED

// Stub implementations for dedicated server

bool sr_useBatchedEffects = false;

void rSubmitExplosion(float, float, float, float, float, float, float, float, float, const float*,
                      size_t)
{
}
void rSubmitExplosionSimple(float, float, float, float, float, float, float, float, float, int) {}
void rSubmitSparkTrail(float, float, float, float, float, float, float, float, float, float,
                       rEffectBlendMode)
{
}
void rBeginSparkBatch() {}
void rEndSparkBatch() {}
void rBatchSparkTrail(float, float, float, float, float, float, float, float, float, float) {}
void rSubmitEffectLine(float, float, float, float, float, float, float, float, float, float,
                       rEffectBlendMode)
{
}
void rEffectsRendererBeginFrame() {}
rEffectsRenderStats rEffectsRendererGetStats()
{
    rEffectsRenderStats stats = {0, 0, 0};
    return stats;
}

#endif // DEDICATED
