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

#include "rHUDRenderer.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rVertex.h"
#include "rRenderQueue.h"
#include "rRenderBucket.h"
#include "tConfiguration.h"

//=============================================================================
// Configuration
//=============================================================================

bool sr_useBatchedHUD = false;
static tSettingItem<bool> conf_useBatchedHUD("USE_BATCHED_HUD", sr_useBatchedHUD);

//=============================================================================
// Statistics
//=============================================================================

namespace
{

struct HUDStats
{
    size_t quadsSubmitted;
    size_t linesSubmitted;
    size_t textChars;
    size_t drawCalls;

    HUDStats() : quadsSubmitted(0), linesSubmitted(0), textChars(0), drawCalls(0) {}

    void Reset()
    {
        quadsSubmitted = 0;
        linesSubmitted = 0;
        textChars = 0;
        drawCalls = 0;
    }
};

HUDStats sg_hudStats;

// State tracking
bool sg_hudRenderingActive = false;
bool sg_savedDepthTest = false;
bool sg_savedBlend = false;
bool sg_savedLighting = false;
bool sg_savedTexture2D = false;

} // anonymous namespace

//=============================================================================
// HUD rendering state
//=============================================================================

void rBeginHUDRendering()
{
    if (sg_hudRenderingActive)
        return;

    sg_hudRenderingActive = true;

    // Save current state for restoration
    sg_savedDepthTest = RenderIsEnabled(rGLConst::DepthTest);
    sg_savedBlend = RenderIsEnabled(rGLConst::Blend);
    sg_savedLighting = RenderIsEnabled(rGLConst::Lighting);
    sg_savedTexture2D = RenderIsEnabled(rGLConst::Texture2D);

    // Set up HUD rendering state
    RenderDisableState(rGLConst::DepthTest);
    RenderDepthMask(false);
    RenderDisableState(rGLConst::Lighting);
    RenderEnableState(rGLConst::Blend);
    RenderBlendFunc(rGLConst::SrcAlpha, rGLConst::OneMinusSrcAlpha);

    // Set default 2D projection (normalized coordinates)
    rResetHUDProjection();
}

void rEndHUDRendering()
{
    if (!sg_hudRenderingActive)
        return;

    sg_hudRenderingActive = false;

    // Restore depth state
    if (sg_savedDepthTest)
    {
        RenderEnableState(rGLConst::DepthTest);
    }
    else
    {
        RenderDisableState(rGLConst::DepthTest);
    }
    RenderDepthMask(true);

    // Restore blend state
    if (sg_savedBlend)
    {
        RenderEnableState(rGLConst::Blend);
    }
    else
    {
        RenderDisableState(rGLConst::Blend);
    }

    // Restore lighting state
    if (sg_savedLighting)
    {
        RenderEnableState(rGLConst::Lighting);
    }

    // Restore texture state
    if (sg_savedTexture2D)
    {
        RenderEnableState(rGLConst::Texture2D);
    }
}

void rSetHUDProjection(float left, float right, float bottom, float top)
{
    ProjMatrix();
    IdentityMatrix();

    // Orthographic projection
    float nearVal = -1.0f;
    float farVal = 1.0f;

    float tx = -(right + left) / (right - left);
    float ty = -(top + bottom) / (top - bottom);
    float tz = -(farVal + nearVal) / (farVal - nearVal);

    float ortho[16] = {
        2.0f / (right - left), 0, 0, 0,
        0, 2.0f / (top - bottom), 0, 0,
        0, 0, -2.0f / (farVal - nearVal), 0,
        tx, ty, tz, 1
    };

    MultMatrix(ortho);

    ModelMatrix();
    IdentityMatrix();
}

void rResetHUDProjection()
{
    rSetHUDProjection(-1.0f, 1.0f, -1.0f, 1.0f);
}

//=============================================================================
// HUD element helpers
//=============================================================================

void rHUDQuad(float x0, float y0, float x1, float y1,
              float r, float g, float b, float a)
{
    uint8_t cr = rFloatToU8(r);
    uint8_t cg = rFloatToU8(g);
    uint8_t cb = rFloatToU8(b);
    uint8_t ca = rFloatToU8(a);

    rVertex20 verts[4] = {
        rVertex20(x0, y0, 0, cr, cg, cb, ca, 0, 0),
        rVertex20(x1, y0, 0, cr, cg, cb, ca, 0, 0),
        rVertex20(x1, y1, 0, cr, cg, cb, ca, 0, 0),
        rVertex20(x0, y1, 0, cr, cg, cb, ca, 0, 0)
    };

    rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Alpha);
    rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, verts[0], verts[1], verts[2], verts[3]);

    sg_hudStats.quadsSubmitted++;
}

void rHUDTexturedQuad(float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a)
{
    uint8_t cr = rFloatToU8(r);
    uint8_t cg = rFloatToU8(g);
    uint8_t cb = rFloatToU8(b);
    uint8_t ca = rFloatToU8(a);

    unsigned int textureId = RenderGetBoundTexture2D();

    rVertex20 verts[4] = {
        rVertex20(x0, y0, 0, cr, cg, cb, ca, u0, v0),
        rVertex20(x1, y0, 0, cr, cg, cb, ca, u1, v0),
        rVertex20(x1, y1, 0, cr, cg, cb, ca, u1, v1),
        rVertex20(x0, y1, 0, cr, cg, cb, ca, u0, v1)
    };

    rRenderStateKey state = rRenderStateKey::HUD(textureId, rBlendMode::Alpha);
    rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, verts[0], verts[1], verts[2], verts[3]);

    sg_hudStats.quadsSubmitted++;
}

void rHUDLine(float x0, float y0, float x1, float y1,
              float r, float g, float b, float a)
{
    uint8_t cr = rFloatToU8(r);
    uint8_t cg = rFloatToU8(g);
    uint8_t cb = rFloatToU8(b);
    uint8_t ca = rFloatToU8(a);

    rVertex20 verts[2] = {
        rVertex20(x0, y0, 0, cr, cg, cb, ca, 0, 0),
        rVertex20(x1, y1, 0, cr, cg, cb, ca, 0, 0)
    };

    rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Alpha);
    rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, state, verts, 2);

    sg_hudStats.linesSubmitted++;
}

//=============================================================================
// Statistics
//=============================================================================

rHUDRenderStats rGetHUDRenderStats()
{
    rHUDRenderStats stats;
    stats.quadsSubmitted = sg_hudStats.quadsSubmitted;
    stats.linesSubmitted = sg_hudStats.linesSubmitted;
    stats.textChars = sg_hudStats.textChars;
    stats.drawCalls = sg_hudStats.drawCalls;
    return stats;
}

//=============================================================================
// Frame lifecycle
//=============================================================================

void rHUDRendererBeginFrame()
{
    sg_hudStats.Reset();
    sg_hudRenderingActive = false;
}

#else // DEDICATED

// Stub implementations for dedicated server

bool sr_useBatchedHUD = false;

void rBeginHUDRendering() {}
void rEndHUDRendering() {}
void rSetHUDProjection(float, float, float, float) {}
void rResetHUDProjection() {}
void rHUDQuad(float, float, float, float, float, float, float, float) {}
void rHUDTexturedQuad(float, float, float, float, float, float, float, float, float, float, float, float) {}
void rHUDLine(float, float, float, float, float, float, float, float) {}
rHUDRenderStats rGetHUDRenderStats()
{
    rHUDRenderStats stats = {0, 0, 0, 0};
    return stats;
}
void rHUDRendererBeginFrame() {}

#endif // DEDICATED
