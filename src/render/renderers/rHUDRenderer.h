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

#ifndef RHUDRENDERER_H
#define RHUDRENDERER_H

#include "defs.h"
#include <cstddef>

//! @file rHUDRenderer.h
//! HUD (Heads-Up Display) rendering helpers for the modern GL3 renderer.
//!
//! This module provides:
//! - Standard HUD rendering state setup (2D projection, no depth, alpha blend)
//! - Frame lifecycle management
//! - Statistics tracking
//!
//! The cockpit widgets and text rendering use immediate mode emulation
//! which is handled by the GL3 renderer. This helper ensures consistent
//! state setup and provides hooks for future batching optimizations.

//! Configuration: enable batched HUD rendering (future optimization)
extern bool sr_useBatchedHUD;

//=============================================================================
// HUD rendering state
//=============================================================================

//! Begin HUD rendering phase.
//! Sets up 2D orthographic projection and appropriate render state:
//! - Disables depth testing and writing
//! - Enables alpha blending
//! - Disables lighting
//! Call this before rendering any HUD elements.
void rBeginHUDRendering();

//! End HUD rendering phase.
//! Restores previous render state.
void rEndHUDRendering();

//! RAII guard — calls rBeginHUDRendering() on construction and rEndHUDRendering() on destruction.
struct rHUDScope
{
    rHUDScope()  { rBeginHUDRendering(); }
    ~rHUDScope() { rEndHUDRendering(); }
    rHUDScope(const rHUDScope&)            = delete;
    rHUDScope& operator=(const rHUDScope&) = delete;
};

//! Set up 2D orthographic projection for HUD elements.
//! @param left Left edge of view (typically -1 or 0)
//! @param right Right edge of view (typically 1 or screen width)
//! @param bottom Bottom edge of view (typically -1 or 0)
//! @param top Top edge of view (typically 1 or screen height)
void rSetHUDProjection(float left, float right, float bottom, float top);

//! Reset HUD projection to default normalized coordinates (-1 to 1).
void rResetHUDProjection();

//=============================================================================
// HUD element helpers
//=============================================================================

//! Submit a colored 2D quad.
//! @param x0, y0 Bottom-left corner
//! @param x1, y1 Top-right corner
//! @param r, g, b, a Color (0.0-1.0)
void rHUDQuad(float x0, float y0, float x1, float y1,
              float r, float g, float b, float a = 1.0f);

//! Submit a textured 2D quad.
//! @param x0, y0 Bottom-left corner
//! @param x1, y1 Top-right corner
//! @param u0, v0 Texture coords for bottom-left
//! @param u1, v1 Texture coords for top-right
//! @param r, g, b, a Color tint (0.0-1.0)
void rHUDTexturedQuad(float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a = 1.0f);

//! Submit a 2D line.
//! @param x0, y0 Start point
//! @param x1, y1 End point
//! @param r, g, b, a Color (0.0-1.0)
void rHUDLine(float x0, float y0, float x1, float y1,
              float r, float g, float b, float a = 1.0f);

//=============================================================================
// Statistics
//=============================================================================

//! Statistics for HUD rendering
struct rHUDRenderStats
{
    size_t quadsSubmitted;   //!< Number of quads submitted
    size_t linesSubmitted;   //!< Number of lines submitted
    size_t textChars;        //!< Number of text characters rendered
    size_t drawCalls;        //!< Draw calls used
};

//! Get HUD rendering statistics for current/last frame
rHUDRenderStats rGetHUDRenderStats();

//=============================================================================
// Frame lifecycle
//=============================================================================

//! Begin frame for HUD rendering (reset statistics)
void rHUDRendererBeginFrame();

#endif // RHUDRENDERER_H
