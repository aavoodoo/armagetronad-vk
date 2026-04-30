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

#include "rCycleRenderer.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rRendererState.h"
#include "tConfiguration.h"

#include <vector>
#include <unordered_map>

//=============================================================================
// Configuration
//=============================================================================

bool sr_useBatchedCycles = true;
static tSettingItem<bool> conf_useBatchedCycles("USE_BATCHED_CYCLES", sr_useBatchedCycles);

//=============================================================================
// Statistics
//=============================================================================

namespace
{

struct CycleStats
{
    size_t cyclesSubmitted;
    size_t cyclesRendered;
    size_t drawCalls;

    CycleStats() : cyclesSubmitted(0), cyclesRendered(0), drawCalls(0) {}

    void Reset()
    {
        cyclesSubmitted = 0;
        cyclesRendered = 0;
        drawCalls = 0;
    }
};

CycleStats sg_cycleStats;

// Collected cycle instances for batched rendering
std::vector<rCycleInstance> sg_cycleInstances;
bool sg_cycleRenderingActive = false;

} // anonymous namespace

//=============================================================================
// Lighting setup
//=============================================================================

void rSetupCycleLighting()
{
    // Guard against null renderer (dedicated server or uninitialized state)
    if (!renderer)
        return;

    // Standard cycle lighting: two directional lights
    // Light A: Reddish, from upper-right-front
    static float lightAPos[4] = { 320.0f, 240.0f, 200.0f, 0.0f }; // w=0 for directional
    static float lightADiffuse[4] = { 1.0f, 0.7f, 0.7f, 1.0f };
    static float lightASpecular[4] = { 1.0f, 0.7f, 0.7f, 1.0f };

    // Light B: Bluish, from lower-left-back
    static float lightBPos[4] = { -240.0f, -100.0f, 200.0f, 0.0f };
    static float lightBDiffuse[4] = { 0.7f, 0.7f, 1.0f, 1.0f };
    static float lightBSpecular[4] = { 0.7f, 0.7f, 1.0f, 1.0f };

    // Set light A (Light0)
    RenderLight(rGLConst::Light0, rGLConst::Position, lightAPos);
    RenderLight(rGLConst::Light0, rGLConst::Diffuse, lightADiffuse);
    RenderLight(rGLConst::Light0, rGLConst::Specular, lightASpecular);

    // Set light B (Light1)
    RenderLight(rGLConst::Light0 + 1, rGLConst::Position, lightBPos);
    RenderLight(rGLConst::Light0 + 1, rGLConst::Diffuse, lightBDiffuse);
    RenderLight(rGLConst::Light0 + 1, rGLConst::Specular, lightBSpecular);
}

void rSetupCycleMaterial()
{
    // Guard against null renderer (dedicated server or uninitialized state)
    if (!renderer)
        return;

    // White material for proper texture color blending
    static float matDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static float matSpecular[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    RenderMaterial(rGLConst::FrontAndBack, rGLConst::Diffuse, matDiffuse);
    RenderMaterial(rGLConst::FrontAndBack, rGLConst::Specular, matSpecular);
}

//=============================================================================
// Cycle instance collection
//=============================================================================

void rBeginCycleRendering()
{
    sg_cycleRenderingActive = true;
    sg_cycleInstances.clear();
    sg_cycleInstances.reserve(16); // Typical max players

    // Set up lighting once for all cycles
    rSetupCycleLighting();
    rSetupCycleMaterial();
}

// Forward declarations from rVulkanRender.cpp
void sr_DrawInstancedModelMesh(uint64_t meshId,
                               const rInstanceData* instances, size_t instanceCount,
                               unsigned int textureId);
uint32_t sr_GetModelMeshCacheVersion_impl();

uint32_t sr_GetModelMeshCacheVersion()
{
    return sr_GetModelMeshCacheVersion_impl();
}

void rEndCycleRendering()
{
    sg_cycleRenderingActive = false;

    if (!sr_useBatchedCycles || sg_cycleInstances.empty())
        return;

    // Group instances by (geometryKey, textureId) so each group can be drawn
    // in one instanced draw call with shared geometry and texture.
    struct GroupKey {
        uint64_t geometry;
        unsigned int texture;
        bool operator==(const GroupKey& o) const { return geometry == o.geometry && texture == o.texture; }
    };
    struct GroupKeyHash {
        size_t operator()(const GroupKey& k) const {
            return std::hash<uint64_t>()(k.geometry) ^ (std::hash<unsigned int>()(k.texture) << 16);
        }
    };

    std::unordered_map<GroupKey, std::vector<rInstanceData>, GroupKeyHash> groups;
    for (const auto& ci : sg_cycleInstances)
    {
        GroupKey key{ci.geometryKey, ci.textureId};
        groups[key].push_back(ci.instance);
    }

    // Set render context to Cycles so the emissive shader hook fires
    // (instanced draws happen after individual Render() calls restored the context)
    rRenderContext prevCtx = sr_GetRenderContext();
    sr_SetRenderContext(rRenderContext::Game3D_Cycles);

    // Draw each group with one instanced draw call
    for (const auto& [key, instances] : groups)
    {
        sr_DrawInstancedModelMesh(key.geometry, instances.data(), instances.size(), key.texture);
        sg_cycleStats.drawCalls++;
    }

    sr_SetRenderContext(prevCtx);

    sg_cycleStats.cyclesRendered = sg_cycleInstances.size();
}

void rSubmitCycleInstance(const rCycleInstance& instance)
{
    if (!sr_useBatchedCycles || !sg_cycleRenderingActive)
    {
        return;
    }

    sg_cycleInstances.push_back(instance);
    sg_cycleStats.cyclesSubmitted++;
}

//=============================================================================
// Statistics
//=============================================================================

rCycleRenderStats rGetCycleRenderStats()
{
    rCycleRenderStats stats;
    stats.cyclesSubmitted = sg_cycleStats.cyclesSubmitted;
    stats.cyclesRendered = sg_cycleStats.cyclesRendered;
    stats.drawCalls = sg_cycleStats.drawCalls;
    return stats;
}

//=============================================================================
// Frame lifecycle
//=============================================================================

void rCycleRendererBeginFrame()
{
    sg_cycleStats.Reset();
    sg_cycleInstances.clear();
    sg_cycleRenderingActive = false;
}

#else // DEDICATED

// Stub implementations for dedicated server

bool sr_useBatchedCycles = false;

void rSetupCycleLighting() {}
void rSetupCycleMaterial() {}
void rBeginCycleRendering() {}
void rEndCycleRendering() {}
void rSubmitCycleInstance(const rCycleInstance&) {}
rCycleRenderStats rGetCycleRenderStats()
{
    rCycleRenderStats stats = {0, 0, 0};
    return stats;
}
void rCycleRendererBeginFrame() {}

#endif // DEDICATED
