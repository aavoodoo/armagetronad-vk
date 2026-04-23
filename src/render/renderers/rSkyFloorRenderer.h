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

#ifndef RSKYFLOORRENDERER_H
#define RSKYFLOORRENDERER_H

//! @file rSkyFloorRenderer.h
//! Batched sky and floor rendering using the modern render queue.
//!
//! This provides an optional path for sky/floor rendering that uses
//! the rRenderQueue instead of immediate mode. Enable with
//! USE_BATCHED_SKY_FLOOR config option.

//! Configuration: enable batched sky/floor rendering
extern bool sr_useBatchedSkyFloor;

//=============================================================================
// Sky rendering
//=============================================================================

//! Submit sky plane geometry to render queue.
//! @param posX, posY Camera position
//! @param dirX, dirY Camera direction
//! @param height Sky plane height
//! @param r, g, b Sky color (0.0-1.0)
//! @param alpha Sky alpha (0.0-1.0)
//! @param useTexture Whether to use sky texture
//! @param textureId OpenGL texture ID (if useTexture)
void rSubmitSkyPlane(float posX, float posY, float dirX, float dirY, float height,
                     float r, float g, float b, float alpha = 1.0f,
                     bool useTexture = false, unsigned int textureId = 0);

//! Submit finite sky rectangle to render queue.
//! @param posX, posY Camera position
//! @param dirX, dirY Camera direction
//! @param height Sky plane height
//! @param lowX, lowY, highX, highY Rectangle bounds
//! @param r, g, b Sky color (0.0-1.0)
//! @param alpha Sky alpha (0.0-1.0)
//! @param useTexture Whether to use sky texture
//! @param textureId OpenGL texture ID (if useTexture)
void rSubmitSkyRectangle(float posX, float posY, float dirX, float dirY, float height,
                         float lowX, float lowY, float highX, float highY,
                         float r, float g, float b, float alpha = 1.0f,
                         bool useTexture = false, unsigned int textureId = 0);

//! Submit black sky (solid color, no texture)
void rSubmitBlackSky(float posX, float posY, float dirX, float dirY, float height);

//=============================================================================
// Floor rendering
//=============================================================================

//! Submit floor plane geometry to render queue.
//! @param posX, posY Camera position
//! @param dirX, dirY Camera direction
//! @param alpha Floor transparency (0.0-1.0)
//! @param textureId OpenGL texture ID
//! @param gridSize Size of grid for texture scaling
void rSubmitFloorPlane(float posX, float posY, float dirX, float dirY,
                       float alpha, unsigned int textureId, float gridSize);

//! Submit floor rectangle (bounded by arena)
void rSubmitFloorRectangle(float posX, float posY, float dirX, float dirY,
                           float lowX, float lowY, float highX, float highY,
                           float alpha, unsigned int textureId, float gridSize);

//! Submit floor grid lines (for grid mode)
void rSubmitFloorGrid(float posX, float posY, float dirX, float dirY,
                      float gridSize, float r, float g, float b, float alpha);

//=============================================================================
// Frame lifecycle
//=============================================================================

//! Begin frame for sky/floor rendering (call before any submissions)
void rSkyFloorBeginFrame();

//! Execute sky/floor rendering (call after all submissions, before other phases)
void rSkyFloorExecute();

#endif // RSKYFLOORRENDERER_H
