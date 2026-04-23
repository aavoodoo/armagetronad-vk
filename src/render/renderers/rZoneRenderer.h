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

#ifndef RZONERENDERER_H
#define RZONERENDERER_H

#include <cstddef>

//! @file rZoneRenderer.h
//! Batched zone rendering using the modern render queue.
//!
//! This provides an optional path for zone rendering that uses
//! the rRenderQueue instead of immediate mode. Enable with
//=============================================================================
// Zone shape types
//=============================================================================

//! Zone render mode
enum class rZoneRenderMode
{
    Filled,    //!< Solid filled zone (additive blending)
    Wireframe  //!< Wireframe outline (no alpha)
};

//=============================================================================
// Circular zone rendering
//=============================================================================

//! Submit a circular zone to the render queue.
//! @param posX, posY Zone center position
//! @param radius Zone radius
//! @param bottom Z position of zone bottom
//! @param height Z height of zone
//! @param rotationAngle Rotation angle in radians
//! @param r, g, b, a Zone color and alpha
//! @param segments Number of segments around the circle
//! @param mode Filled or wireframe
void rSubmitCircularZone(float posX, float posY, float radius,
                         float bottom, float height, float rotationAngle,
                         float r, float g, float b, float a,
                         int segments = 11,
                         rZoneRenderMode mode = rZoneRenderMode::Filled);

//! Submit a circular zone with proximity-based height scaling.
//! @param posX, posY Zone center position
//! @param radius Zone radius
//! @param bottom Z position of zone bottom
//! @param height Z height of zone (before proximity scaling)
//! @param heightMult Height multiplier (0-1, from proximity calculation)
//! @param rotationAngle Rotation angle in radians
//! @param r, g, b, a Zone color and alpha
//! @param segments Number of segments around the circle
//! @param segmentLength Fraction of segment arc to fill (0-1, creates gaps when < 1)
//! @param segmentSteps Number of quads per segment
//! @param floorRadiusPct Floor radius as percentage of zone radius (0-1)
//! @param mode Filled or wireframe
void rSubmitCircularZoneProximity(float posX, float posY, float radius,
                                  float bottom, float height, float heightMult,
                                  float rotationAngle,
                                  float r, float g, float b, float a,
                                  int segments = 11,
                                  float segmentLength = 1.0f,
                                  int segmentSteps = 1,
                                  float floorRadiusPct = 0.0f,
                                  rZoneRenderMode mode = rZoneRenderMode::Filled);

//=============================================================================
// Polygon zone rendering
//=============================================================================

//! Submit a polygon zone to the render queue.
//! @param posX, posY Zone center position
//! @param scaleX, scaleY Zone scale
//! @param bottom Z position of zone bottom
//! @param height Z height of zone
//! @param rotationAngle Rotation angle in radians
//! @param points Array of polygon points (x, y pairs relative to center)
//! @param numPoints Number of points in the polygon
//! @param r, g, b, a Zone color and alpha
//! @param mode Filled or wireframe
void rSubmitPolygonZone(float posX, float posY, float scaleX, float scaleY,
                        float bottom, float height, float rotationAngle,
                        const float* points, size_t numPoints,
                        float r, float g, float b, float a,
                        rZoneRenderMode mode = rZoneRenderMode::Filled);

//=============================================================================
// Frame lifecycle
//=============================================================================

//! Begin frame for zone rendering (clears accumulated geometry)
void rZoneRendererBeginFrame();

//! Render all accumulated zone geometry immediately (call during frame, like walls)
void rZoneRendererRender();

//! Get statistics
struct rZoneRenderStats
{
    size_t zonesSubmitted;
    size_t verticesGenerated;
    size_t trianglesGenerated;
};
rZoneRenderStats rZoneRendererGetStats();

#endif // RZONERENDERER_H
