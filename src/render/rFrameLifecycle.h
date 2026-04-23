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

#ifndef RFRAMELIFECYCLE_H
#define RFRAMELIFECYCLE_H

#include <functional>

//! @file rFrameLifecycle.h
//! Centralized frame lifecycle management for the rendering system.
//!
//! This module provides a single entry point to initialize all render
//! subsystems at the beginning of each frame. It calls BeginFrame on:
//! - rRenderStats (performance statistics)
//! - rRenderQueue (batched geometry)
//! - rEffectsRenderer (explosions, sparks)
//! - rCycleRenderer (cycles with lighting)
//! - rHUDRenderer (HUD elements)
//! - rZoneRenderer (zones)
//!
//! Usage (legacy):
//!   Call rBeginFrame() at the start of each render frame, after clearing
//!   the frame buffer but before any rendering.
//!   Call rEndFrame() at the end of each render frame, after all rendering
//!   but before buffer swap.
//!
//! Usage (recommended):
//!   Use rRenderFrame() to wrap your rendering code. This handles the
//!   complete frame lifecycle: Clear -> BeginFrame -> render -> EndFrame -> Swap.

//! Begin a new render frame.
//! Resets statistics and initializes all render subsystems.
//! Call this after rSysDep::ClearGL() but before any rendering.
//! Note: Prefer using rRenderFrame() wrapper for new code.
void rBeginFrame();

//! End the current render frame.
//! Finalizes statistics and flushes any pending operations.
//! Call this after all rendering but before rSysDep::SwapGL().
//! Note: Prefer using rRenderFrame() wrapper for new code.
void rEndFrame();

//! Render a complete frame with full lifecycle management.
//! Handles Clear -> BeginFrame -> renderCallback() -> EndFrame -> Swap.
//! This is the recommended way to render frames.
//! @param renderCallback Function to call for rendering the frame content
void rRenderFrame(std::function<void()> renderCallback);

//! Render a frame without clearing the buffer first.
//! Useful for accumulation effects or multi-pass rendering.
//! Handles BeginFrame -> renderCallback() -> EndFrame -> Swap (no Clear).
//! @param renderCallback Function to call for rendering the frame content
void rRenderFrameNoClear(std::function<void()> renderCallback);

//! Render a frame without swapping buffers at the end.
//! Useful for multi-pass rendering or off-screen rendering.
//! Handles Clear -> BeginFrame -> renderCallback() -> EndFrame (no Swap).
//! @param renderCallback Function to call for rendering the frame content
void rRenderFrameNoSwap(std::function<void()> renderCallback);

#endif // RFRAMELIFECYCLE_H
