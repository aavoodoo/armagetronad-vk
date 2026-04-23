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

#include "rSkyFloorRenderer.h"

#ifndef DEDICATED

#include "rRenderQueue.h"
#include "rVertex.h"
#include "tConfiguration.h"

//=============================================================================
// Configuration
//=============================================================================

//! Enable batched sky/floor rendering (default off until stable)
bool sr_useBatchedSkyFloor = false;
static tSettingItem<bool> conf_useBatchedSkyFloor("USE_BATCHED_SKY_FLOOR", sr_useBatchedSkyFloor);

//=============================================================================
// Internal helpers
//=============================================================================

namespace
{

//! Generate vertices for a finite sky/floor rectangle as a triangle fan.
//! The fan is centered at camera position for proper z-buffering.
//! @param vertices Output array (must have space for 6 vertices = center + 5 corners)
//! @param posX, posY Camera position (center of fan)
//! @param dirX, dirY Camera direction (offset from center)
//! @param height Z height of the plane
//! @param lowX, lowY, highX, highY Rectangle bounds
//! @param r, g, b, a Color
//! @param texScale Texture coordinate scale (1/gridSize for floor)
//! @return Number of triangles generated
size_t GenerateRectangleFan(rVertex20* vertices, float posX, float posY, float dirX, float dirY,
                            float height, float lowX, float lowY, float highX, float highY,
                            float r, float g, float b, float a, float texScale)
{
    // Use a 2x2 grid of quads instead of a triangle fan. A fan creates a
    // singularity at the center vertex where all triangles meet, causing
    // discontinuous texture gradients and mipmap artifacts (dark spot).
    float cx = posX - dirX;
    float cy = posY - dirY;

    uint8_t rb = static_cast<uint8_t>(r * 255.0f);
    uint8_t gb = static_cast<uint8_t>(g * 255.0f);
    uint8_t bb = static_cast<uint8_t>(b * 255.0f);
    uint8_t ab = static_cast<uint8_t>(a * 255.0f);

    auto set = [&](size_t i, float x, float y) {
        vertices[i].SetPosition(x, y, height);
        vertices[i].SetColor(rb, gb, bb, ab);
        vertices[i].SetTexCoord(x * texScale, y * texScale);
    };

    // 3x3 grid vertices → 4 quads → 8 triangles → 24 vertices
    // But caller allocated 12 vertices. We need to check if the caller
    // can handle 24. For now, keep 12 vertices but use 2 quads (4 triangles)
    // split along the center, which still eliminates the fan singularity.
    // Split rectangle into 2 quads along the center Y coordinate:
    // Bottom quad: (lx,ly)-(hx,cy), Top quad: (lx,cy)-(hx,hy)
    size_t idx = 0;
    // Bottom quad: 2 triangles
    set(idx++, lowX, lowY);  set(idx++, highX, lowY); set(idx++, highX, cy);
    set(idx++, lowX, lowY);  set(idx++, highX, cy);   set(idx++, lowX, cy);
    // Top quad: 2 triangles
    set(idx++, lowX, cy);    set(idx++, highX, cy);    set(idx++, highX, highY);
    set(idx++, lowX, cy);    set(idx++, highX, highY); set(idx++, lowX, highY);

    return 4; // 4 triangles
}

//! Generate infinite plane vertices using projective coordinates (w=0 trick).
//! This creates 4 triangles from camera position to points at infinity.
//! @param vertices Output array (must have space for 12 vertices = 4 triangles)
//! @param posX, posY Camera position
//! @param dirX, dirY Camera direction
//! @param height Z height
//! @param r, g, b, a Color
//! @return Number of triangles generated
size_t GenerateInfinitePlane(rVertex20* vertices, float posX, float posY, float dirX, float dirY,
                             float height, float r, float g, float b, float a)
{
    // For infinite plane, we use a simple quad that's big enough.
    // True infinity would require w=0 in clip space, but we can approximate
    // with a large quad. The original code uses w=0, but our vertex format
    // doesn't support projective coordinates, so we use a large finite plane.

    // Center vertex
    float cx = posX - dirX;
    float cy = posY - dirY;

    // Large distance for "infinity"
    const float INF = 10000.0f;

    // Four corners at "infinity"
    float corners[5][2] = {
        {INF, 0.1f * INF},
        {0.1f * INF, INF},
        {-INF, 0.1f * INF},
        {0.1f * INF, -INF},
        {INF, 0.1f * INF} // Close
    };

    // Convert color
    uint8_t rb = static_cast<uint8_t>(r * 255.0f);
    uint8_t gb = static_cast<uint8_t>(g * 255.0f);
    uint8_t bb = static_cast<uint8_t>(b * 255.0f);
    uint8_t ab = static_cast<uint8_t>(a * 255.0f);

    // Generate triangles
    size_t idx = 0;
    for (int i = 0; i < 4; ++i)
    {
        // Center
        vertices[idx].SetPosition(cx, cy, height);
        vertices[idx].SetColor(rb, gb, bb, ab);
        vertices[idx].SetTexCoord(0.5f, 0.5f);
        idx++;

        // Corner 1
        vertices[idx].SetPosition(corners[i][0], corners[i][1], height);
        vertices[idx].SetColor(rb, gb, bb, ab);
        vertices[idx].SetTexCoord(0.0f, 0.0f);
        idx++;

        // Corner 2
        vertices[idx].SetPosition(corners[i + 1][0], corners[i + 1][1], height);
        vertices[idx].SetColor(rb, gb, bb, ab);
        vertices[idx].SetTexCoord(1.0f, 0.0f);
        idx++;
    }

    return 4;
}

} // anonymous namespace

//=============================================================================
// Sky rendering
//=============================================================================

void rSubmitSkyPlane(float posX, float posY, float dirX, float dirY, float height, float r, float g,
                     float b, float alpha, bool useTexture, unsigned int textureId)
{
    if (!sr_useBatchedSkyFloor)
        return;

    // Generate infinite plane vertices
    rVertex20 vertices[12];
    GenerateInfinitePlane(vertices, posX, posY, dirX, dirY, height, r, g, b, alpha);

    // Create render state
    rRenderStateKey state;
    if (useTexture && textureId != 0)
    {
        state = rRenderStateKey::Textured(textureId);
    }
    else
    {
        state = rRenderStateKey::Colored();
    }

    // Submit to sky phase
    rRenderQueue::Instance().Submit(rRenderPhase::Sky, state, vertices, 12);
}

void rSubmitSkyRectangle(float posX, float posY, float dirX, float dirY, float height, float lowX,
                         float lowY, float highX, float highY, float r, float g, float b,
                         float alpha, bool useTexture, unsigned int textureId)
{
    if (!sr_useBatchedSkyFloor)
        return;

    // Generate rectangle vertices (4 triangles = 12 vertices)
    rVertex20 vertices[12];
    GenerateRectangleFan(vertices, posX, posY, dirX, dirY, height, lowX, lowY, highX, highY, r, g,
                         b, alpha, 0.005f); // Sky texture scale

    // Create render state
    rRenderStateKey state;
    if (useTexture && textureId != 0)
    {
        state = rRenderStateKey::Textured(textureId);
    }
    else
    {
        state = rRenderStateKey::Colored();
    }

    // Submit to sky phase
    rRenderQueue::Instance().Submit(rRenderPhase::Sky, state, vertices, 12);
}

void rSubmitBlackSky(float posX, float posY, float dirX, float dirY, float height)
{
    // Black sky - no texture, solid black
    rSubmitSkyPlane(posX, posY, dirX, dirY, height, 0.0f, 0.0f, 0.0f, 1.0f, false, 0);
}

//=============================================================================
// Floor rendering
//=============================================================================

void rSubmitFloorPlane(float posX, float posY, float dirX, float dirY, float alpha,
                       unsigned int textureId, float gridSize)
{
    if (!sr_useBatchedSkyFloor)
        return;

    // Generate infinite plane vertices
    rVertex20 vertices[12];
    GenerateInfinitePlane(vertices, posX, posY, dirX, dirY, 0.0f, 1.0f, 1.0f, 1.0f, alpha);

    // Adjust texture coordinates for floor grid
    float texScale = 1.0f / gridSize;
    for (int i = 0; i < 12; ++i)
    {
        float x = vertices[i].position[0];
        float y = vertices[i].position[1];
        vertices[i].SetTexCoord(x * texScale, y * texScale);
    }

    // Create render state
    rRenderStateKey state = rRenderStateKey::Textured(textureId);

    // Submit to opaque static phase (floor is behind everything)
    rRenderQueue::Instance().Submit(rRenderPhase::OpaqueStatic, state, vertices, 12);
}

void rSubmitFloorRectangle(float posX, float posY, float dirX, float dirY, float lowX, float lowY,
                           float highX, float highY, float alpha, unsigned int textureId,
                           float gridSize)
{
    if (!sr_useBatchedSkyFloor)
        return;

    float texScale = 1.0f / gridSize;

    // Generate rectangle vertices
    rVertex20 vertices[12];
    GenerateRectangleFan(vertices, posX, posY, dirX, dirY, 0.0f, lowX, lowY, highX, highY, 1.0f,
                         1.0f, 1.0f, alpha, texScale);

    // Create render state
    rRenderStateKey state = rRenderStateKey::Textured(textureId);

    // Submit to opaque static phase
    rRenderQueue::Instance().Submit(rRenderPhase::OpaqueStatic, state, vertices, 12);
}

void rSubmitFloorGrid(float posX, float posY, float dirX, float dirY, float gridSize, float r,
                      float g, float b, float alpha)
{
    if (!sr_useBatchedSkyFloor)
        return;

    // Grid rendering: draw lines in a grid pattern around camera
    const int EXTENSION = 10;
    float sideLen = gridSize;

    // Calculate center grid position
    float centerX = posX + dirX * (sideLen * EXTENSION * 0.8f);
    float centerY = posY + dirY * (sideLen * EXTENSION * 0.8f);
    int xn = static_cast<int>(centerX / sideLen);
    int yn = static_cast<int>(centerY / sideLen);

    // Count lines: (2*EXTENSION+1) horizontal + (2*EXTENSION+1) vertical = ~42 lines
    // Each line = 2 vertices
    const int maxLines = (2 * EXTENSION + 1) * 2;
    rVertex20 vertices[maxLines * 2];
    size_t idx = 0;

    auto intensity = [&](float linePos, float camPos) {
        float diff = linePos - camPos;
        float maxDist = EXTENSION * sideLen;
        float factor = 1.0f - (diff * diff) / (maxDist * maxDist);
        return factor > 0.0f ? factor : 0.0f;
    };

    // Vertical lines
    for (int i = xn - EXTENSION; i <= xn + EXTENSION; ++i)
    {
        float lineX = i * sideLen;
        float intens = intensity(lineX, centerX);
        if (intens > 0.0f)
        {
            uint8_t rb = static_cast<uint8_t>(r * intens * 255.0f);
            uint8_t gb = static_cast<uint8_t>(g * intens * 255.0f);
            uint8_t bb = static_cast<uint8_t>(b * intens * 255.0f);
            uint8_t ab = static_cast<uint8_t>(alpha * intens * 255.0f);

            vertices[idx].SetPosition(lineX, centerY - sideLen * (EXTENSION + 1), 0.0f);
            vertices[idx].SetColor(rb, gb, bb, ab);
            idx++;

            vertices[idx].SetPosition(lineX, centerY + sideLen * (EXTENSION + 1), 0.0f);
            vertices[idx].SetColor(rb, gb, bb, ab);
            idx++;
        }
    }

    // Horizontal lines
    for (int j = yn - EXTENSION; j <= yn + EXTENSION; ++j)
    {
        float lineY = j * sideLen;
        float intens = intensity(lineY, centerY);
        if (intens > 0.0f)
        {
            uint8_t rb = static_cast<uint8_t>(r * intens * 255.0f);
            uint8_t gb = static_cast<uint8_t>(g * intens * 255.0f);
            uint8_t bb = static_cast<uint8_t>(b * intens * 255.0f);
            uint8_t ab = static_cast<uint8_t>(alpha * intens * 255.0f);

            vertices[idx].SetPosition(centerX - (EXTENSION + 1) * sideLen, lineY, 0.0f);
            vertices[idx].SetColor(rb, gb, bb, ab);
            idx++;

            vertices[idx].SetPosition(centerX + (EXTENSION + 1) * sideLen, lineY, 0.0f);
            vertices[idx].SetColor(rb, gb, bb, ab);
            idx++;
        }
    }

    // Submit lines to opaque static phase
    if (idx > 0)
    {
        rRenderStateKey state = rRenderStateKey::Colored();
        rRenderQueue::Instance().SubmitLines(rRenderPhase::OpaqueStatic, state, vertices, idx);
    }
}

//=============================================================================
// Frame lifecycle
//=============================================================================

void rSkyFloorBeginFrame()
{
    // Currently no per-frame initialization needed for sky/floor
    // The rRenderQueue::BeginFrame() handles clearing buckets
}

void rSkyFloorExecute()
{
    // Currently no special execution needed
    // Sky and OpaqueStatic phases are executed by rRenderQueue::Execute()
}

#else // DEDICATED

// Stub implementations for dedicated server

bool sr_useBatchedSkyFloor = false;

void rSubmitSkyPlane(float, float, float, float, float, float, float, float, float, bool,
                     unsigned int)
{
}
void rSubmitSkyRectangle(float, float, float, float, float, float, float, float, float, float,
                         float, float, float, bool, unsigned int)
{
}
void rSubmitBlackSky(float, float, float, float, float) {}
void rSubmitFloorPlane(float, float, float, float, float, unsigned int, float) {}
void rSubmitFloorRectangle(float, float, float, float, float, float, float, float, float,
                           unsigned int, float)
{
}
void rSubmitFloorGrid(float, float, float, float, float, float, float, float, float) {}
void rSkyFloorBeginFrame() {}
void rSkyFloorExecute() {}

#endif // DEDICATED
