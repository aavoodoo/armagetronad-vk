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

#ifndef REFFECTSRENDERER_H
#define REFFECTSRENDERER_H

#include <cstddef>

//! @file rEffectsRenderer.h
//! Batched effects rendering (explosions, sparks) using the modern render queue.
//!
//! This provides an optional path for effects rendering that uses
//! the rRenderQueue instead of immediate mode. Enable with
//! USE_BATCHED_EFFECTS config option.

//! Configuration: enable batched effects rendering
extern bool sr_useBatchedEffects;

//=============================================================================
// Effect blending modes
//=============================================================================

//! Effect blend mode
enum class rEffectBlendMode
{
    Alpha,    //!< Standard alpha blending (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    Additive  //!< Additive blending (GL_SRC_ALPHA, GL_ONE) - for bright glowing effects
};

//=============================================================================
// Explosion rendering
//=============================================================================

//! Submit an explosion effect (radial lines from center).
//! @param posX, posY, posZ Explosion center position
//! @param outerRadius Outer radius of explosion lines
//! @param innerRadius Inner radius of explosion lines (creates expanding ring effect)
//! @param r, g, b, a Explosion color and alpha
//! @param directions Array of direction vectors (x, y, z triplets, normalized)
//! @param numDirections Number of direction vectors
//! @param useHUDPhase If true, submit to HUD phase (for 2D/cockpit rendering)
void rSubmitExplosion(float posX, float posY, float posZ,
                      float outerRadius, float innerRadius,
                      float r, float g, float b, float a,
                      const float* directions, size_t numDirections,
                      bool useHUDPhase = false);

//! Submit a simple explosion effect using default radial directions.
//! @param posX, posY, posZ Explosion center position
//! @param outerRadius Outer radius of explosion lines
//! @param innerRadius Inner radius of explosion lines
//! @param r, g, b, a Explosion color and alpha
//! @param numRays Number of radial rays (default 16)
void rSubmitExplosionSimple(float posX, float posY, float posZ,
                            float outerRadius, float innerRadius,
                            float r, float g, float b, float a,
                            int numRays = 16);

//=============================================================================
// Spark rendering
//=============================================================================

//! Submit spark trail lines (motion blur effect).
//! @param x1, y1, z1 Current spark position
//! @param x2, y2, z2 Previous spark position (trail end)
//! @param r, g, b, a Spark color and alpha
//! @param blendMode Blend mode (usually Additive for sparks)
void rSubmitSparkTrail(float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float r, float g, float b, float a,
                       rEffectBlendMode blendMode = rEffectBlendMode::Additive);

//! Begin batching multiple spark trails.
//! Call this before submitting multiple sparks for efficiency.
void rBeginSparkBatch();

//! End spark batch and submit to render queue.
void rEndSparkBatch();

//! Submit a spark trail within a batch (more efficient for many sparks).
//! Must be called between rBeginSparkBatch() and rEndSparkBatch().
void rBatchSparkTrail(float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float r, float g, float b, float a);

//=============================================================================
// Generic line effects
//=============================================================================

//! Submit a colored line effect.
//! @param x1, y1, z1 Line start position
//! @param x2, y2, z2 Line end position
//! @param r, g, b, a Line color and alpha
//! @param blendMode Blend mode
void rSubmitEffectLine(float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float r, float g, float b, float a,
                       rEffectBlendMode blendMode = rEffectBlendMode::Alpha);

//=============================================================================
// Frame lifecycle
//=============================================================================

//! Begin frame for effects rendering
void rEffectsRendererBeginFrame();

//! Get statistics
struct rEffectsRenderStats
{
    size_t explosionsSubmitted;
    size_t sparksSubmitted;
    size_t linesGenerated;
};
rEffectsRenderStats rEffectsRendererGetStats();

#endif // REFFECTSRENDERER_H
