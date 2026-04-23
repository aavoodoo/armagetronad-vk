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

#include "aa_config.h"
#include "rFrameLifecycle.h"

#ifndef DEDICATED

#include "rRenderStats.h"
#include "rRenderQueue.h"
#include "rSkyFloorRenderer.h"
#include "rEffectsRenderer.h"
#include "rCycleRenderer.h"
#include "rHUDRenderer.h"
#include "rZoneRenderer.h"
#include "rFont.h"
#include "rSysdep.h"
#include "rRender.h"           // For renderer access

//=============================================================================
// Frame lifecycle implementation
//=============================================================================

void rBeginFrame()
{
    // Reset performance statistics first
    rRenderStats::Instance().BeginFrame();

    // Initialize render queue for batched geometry
    rRenderQueue::Instance().BeginFrame();

    // Initialize subsystem renderers
    rSkyFloorBeginFrame();
    rEffectsRendererBeginFrame();
    rCycleRendererBeginFrame();
    rHUDRendererBeginFrame();
    rZoneRendererBeginFrame();
}

void rEndFrame()
{
    // Note: Transparent and Effects phases are executed in eDisplay.cpp while 3D camera is active

    // Debug: render font atlas if enabled
    sr_RenderFontAtlas();

    // Finalize performance statistics
    rRenderStats::Instance().EndFrame();
}

//=============================================================================
// Frame wrapper functions (recommended API)
//=============================================================================

void rRenderFrame(std::function<void()> renderCallback)
{
    rSysDep::ClearGL();
    rBeginFrame();
    renderCallback();
    rEndFrame();
    rSysDep::SwapGL();
}

void rRenderFrameNoClear(std::function<void()> renderCallback)
{
    rBeginFrame();
    renderCallback();
    rEndFrame();
    rSysDep::SwapGL();
}

void rRenderFrameNoSwap(std::function<void()> renderCallback)
{
    rSysDep::ClearGL();
    rBeginFrame();
    renderCallback();
    rEndFrame();
}

#else // DEDICATED

// Stub implementations for dedicated server

void rBeginFrame() {}
void rEndFrame() {}
void rRenderFrame(std::function<void()> renderCallback) { renderCallback(); }
void rRenderFrameNoClear(std::function<void()> renderCallback) { renderCallback(); }
void rRenderFrameNoSwap(std::function<void()> renderCallback) { renderCallback(); }

#endif // DEDICATED
