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

#ifndef RCYCLERENDERER_H
#define RCYCLERENDERER_H

#include "defs.h"

//! @file rCycleRenderer.h
//! Cycle rendering helpers for the modern GL3 renderer.
//!
//! This module provides:
//! - Standard cycle lighting setup (two-light Blinn-Phong)
//! - Per-cycle instance data collection
//! - Future: instanced rendering for multiple cycles

//! Configuration: enable batched cycle rendering (future optimization)
extern bool sr_useBatchedCycles;

//=============================================================================
// Lighting setup
//=============================================================================

//! Set up standard cycle lighting.
//! Two lights: reddish from upper-right, bluish from lower-left.
//! Call this once per frame before rendering any cycles.
void rSetupCycleLighting();

//! Set up cycle material properties.
//! Uses white diffuse/specular for proper color blending with textures.
void rSetupCycleMaterial();

//=============================================================================
// Cycle instance data (for future instanced rendering)
//=============================================================================

//! Per-instance GPU data — matches uber_instanced.vert layout:
//!   locations 4-7: model matrix (mat4, 64 bytes)
//!   location 8:    instance color (vec4, 16 bytes)
//! Total: 80 bytes per instance, tightly packed.
struct rInstanceData
{
    float modelMatrix[16]; //!< Column-major 4×4 model matrix
    float color[4];        //!< RGBA team color
};

//! Cycle instance submitted from gCycle::Render().
//! Three of these per cycle (body, rear wheel, front wheel).
struct rCycleInstance
{
    rInstanceData instance;     //!< GPU-ready instance data
    const void* geometryKey;   //!< Pointer to rModelVertex data (cache key for mesh)
    unsigned int textureId;    //!< Bound texture for this part
};

//! Begin collecting cycle instances for batched rendering.
//! Call this at the start of cycle rendering phase.
void rBeginCycleRendering();

//! End cycle rendering phase.
//! If batched rendering is enabled, this will flush all collected cycles.
void rEndCycleRendering();

//! Submit a cycle instance for rendering.
//! @param instance Cycle instance data
//! @note If batched rendering is disabled, this does nothing (use legacy path)
void rSubmitCycleInstance(const rCycleInstance& instance);

//=============================================================================
// Statistics
//=============================================================================

//! Statistics for cycle rendering
struct rCycleRenderStats
{
    size_t cyclesSubmitted;  //!< Number of cycles submitted
    size_t cyclesRendered;   //!< Number of cycles actually rendered (not blinking)
    size_t drawCalls;        //!< Draw calls used
};

//! Get cycle rendering statistics for current/last frame
rCycleRenderStats rGetCycleRenderStats();

//=============================================================================
// Frame lifecycle
//=============================================================================

//! Begin frame for cycle rendering (reset statistics)
void rCycleRendererBeginFrame();

//=============================================================================
// Cache version
//=============================================================================

//! Returns a counter that increments every time the model mesh cache is cleared
//! (e.g. on shader reload / moviepack switch).  Game code compares against its
//! last-seen version and re-primes the cache when they differ.
uint32_t sr_GetModelMeshCacheVersion();

#endif // RCYCLERENDERER_H
