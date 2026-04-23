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

#include "rZoneRenderer.h"

#ifndef DEDICATED

#include "rVertex.h"
#include "rRenderQueue.h"      // For batch rendering
#include "tConfiguration.h"

#include <cmath>
#include <vector>

//=============================================================================
// Configuration
//=============================================================================

// Zone batch rendering (legacy fallback removed, always enabled)

//=============================================================================
// Statistics tracking
//=============================================================================

namespace
{

//! Statistics tracking
struct ZoneStats
{
    size_t zonesSubmitted;
    size_t verticesGenerated;
    size_t trianglesGenerated;

    ZoneStats() : zonesSubmitted(0), verticesGenerated(0), trianglesGenerated(0) {}

    void Reset()
    {
        zonesSubmitted = 0;
        verticesGenerated = 0;
        trianglesGenerated = 0;
    }
};

ZoneStats sg_zoneStats;

} // anonymous namespace

//=============================================================================
// Helper functions
//=============================================================================

namespace
{

//! Generate circular zone geometry matching original immediate mode logic.
//! Handles both vertical walls and floor quads in one pass.
//! @return Number of vertices generated
size_t GenerateCircularZoneGeometry(std::vector<rVertex20>& vertices, float posX, float posY,
                                     float radius, float bottom, float top, float rotationAngle,
                                     float r, float g, float b, float a, int segments,
                                     float segmentLength, int segmentSteps, bool filled,
                                     float floorRadiusPct)
{
    // Convert color to bytes
    uint8_t rb = static_cast<uint8_t>(r * 255.0f);
    uint8_t gb = static_cast<uint8_t>(g * 255.0f);
    uint8_t bb = static_cast<uint8_t>(b * 255.0f);
    uint8_t ab = static_cast<uint8_t>(a * 255.0f);

    const float pi = 3.14159265358979f;
    const float segmentArc = 2.0f * pi / segments;
    const float seglen = segmentArc * segmentLength;

    size_t startIdx = vertices.size();

    // Match original logic exactly: loop through segments and steps
    for (int i = segments - 1; i >= 0; --i)
    {
        float a = rotationAngle + i * 2.0f * pi / static_cast<float>(segments);
        float sa = radius * std::sin(a);
        float ca = radius * std::cos(a);

        for (int s = 0; s < segmentSteps; ++s)
        {
            float b = a + seglen / segmentSteps;
            float sb = radius * std::sin(b);
            float cb = radius * std::cos(b);

            // Vertical quad
            rVertex20 v0, v1, v2, v3;
            v0.SetPosition(posX + sa, posY + ca, bottom);
            v0.SetColor(rb, gb, bb, ab);

            if (top != bottom)
            {
                v1.SetPosition(posX + sa, posY + ca, top);
                v1.SetColor(rb, gb, bb, ab);
                v2.SetPosition(posX + sb, posY + cb, top);
                v2.SetColor(rb, gb, bb, ab);
            }

            v3.SetPosition(posX + sb, posY + cb, bottom);
            v3.SetColor(rb, gb, bb, ab);

            if (filled)
            {
                // Filled mode: convert triangle fan to triangles
                if (top != bottom)
                {
                    vertices.push_back(v0);
                    vertices.push_back(v1);
                    vertices.push_back(v2);

                    vertices.push_back(v0);
                    vertices.push_back(v2);
                    vertices.push_back(v3);
                }

                // Floor quad (only if floor radius is > 0)
                if (floorRadiusPct > 0.0f)
                {
                    rVertex20 f0, f1, f2, f3;

                    if (bottom != 0.0f)
                    {
                        // Separate floor quad at z=0
                        f0.SetPosition(posX + sa, posY + ca, 0.0f);
                        f0.SetColor(rb, gb, bb, ab);
                        f1.SetPosition(posX + sb, posY + cb, 0.0f);
                        f1.SetColor(rb, gb, bb, ab);
                    }
                    else
                    {
                        // Floor connects to vertical quad base
                        f0 = v0;
                        f1 = v3;
                    }

                    // Inner floor radius (scaled)
                    f2.SetPosition(posX + sb * floorRadiusPct, posY + cb * floorRadiusPct, 0.0f);
                    f2.SetColor(rb, gb, bb, ab);
                    f3.SetPosition(posX + sa * floorRadiusPct, posY + ca * floorRadiusPct, 0.0f);
                    f3.SetColor(rb, gb, bb, ab);

                    // Convert triangle fan to triangles
                    vertices.push_back(f0);
                    vertices.push_back(f1);
                    vertices.push_back(f2);

                    vertices.push_back(f0);
                    vertices.push_back(f2);
                    vertices.push_back(f3);
                }
            }
            else
            {
                // Wireframe mode: generate line strip outline of vertical quad
                // LineStrip vertices: bot-left, top-left, top-right, bot-right, bot-left (closing)
                if (top != bottom)
                {
                    // Create lines for the quad edges
                    rVertex20 lines[5] = {v0, v1, v2, v3, v0};
                    for (int l = 0; l < 4; ++l)
                    {
                        vertices.push_back(lines[l]);
                        vertices.push_back(lines[l + 1]);
                    }
                }
            }

            // Update for next step
            a = b;
            sa = sb;
            ca = cb;
        }
    }

    return vertices.size() - startIdx;
}


//! Generate polygon zone geometry as triangles.
//! @return Number of vertices generated
size_t GeneratePolygonZoneQuads(std::vector<rVertex20>& vertices, float posX, float posY,
                                 float scaleX, float scaleY, float bottom, float top,
                                 float rotationAngle, const float* points, size_t numPoints,
                                 float r, float g, float b, float a)
{
    if (numPoints < 2)
        return 0;

    // Convert color to bytes
    uint8_t rb = static_cast<uint8_t>(r * 255.0f);
    uint8_t gb = static_cast<uint8_t>(g * 255.0f);
    uint8_t bb = static_cast<uint8_t>(b * 255.0f);
    uint8_t ab = static_cast<uint8_t>(a * 255.0f);

    float cosR = std::cos(rotationAngle);
    float sinR = std::sin(rotationAngle);

    size_t startIdx = vertices.size();

    for (size_t i = 0; i < numPoints; ++i)
    {
        size_t j = (i + 1) % numPoints;

        // Get local coordinates
        float lx1 = points[i * 2] * scaleX;
        float ly1 = points[i * 2 + 1] * scaleY;
        float lx2 = points[j * 2] * scaleX;
        float ly2 = points[j * 2 + 1] * scaleY;

        // Apply rotation and translation
        float x1 = posX + lx1 * cosR - ly1 * sinR;
        float y1 = posY + lx1 * sinR + ly1 * cosR;
        float x2 = posX + lx2 * cosR - ly2 * sinR;
        float y2 = posY + lx2 * sinR + ly2 * cosR;

        // Create quad as two triangles
        rVertex20 v0, v1, v2, v3, v4, v5;

        v0.SetPosition(x1, y1, bottom);
        v0.SetColor(rb, gb, bb, ab);
        v1.SetPosition(x1, y1, top);
        v1.SetColor(rb, gb, bb, ab);
        v2.SetPosition(x2, y2, top);
        v2.SetColor(rb, gb, bb, ab);

        v3.SetPosition(x1, y1, bottom);
        v3.SetColor(rb, gb, bb, ab);
        v4.SetPosition(x2, y2, top);
        v4.SetColor(rb, gb, bb, ab);
        v5.SetPosition(x2, y2, bottom);
        v5.SetColor(rb, gb, bb, ab);

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);
        vertices.push_back(v4);
        vertices.push_back(v5);
    }

    return vertices.size() - startIdx;
}

} // anonymous namespace

//=============================================================================
// Public API
//=============================================================================

void rSubmitCircularZone(float posX, float posY, float radius, float bottom, float height,
                         float rotationAngle, float r, float g, float b, float a, int segments,
                         rZoneRenderMode mode)
{

    float top = bottom + height;
    bool filled = (mode == rZoneRenderMode::Filled);

    // Generate geometry into reusable buffer
    static std::vector<rVertex20> vertices;
    vertices.clear();
    if (vertices.capacity() < 256) vertices.reserve(256);
    GenerateCircularZoneGeometry(vertices, posX, posY, radius, bottom, top, rotationAngle, r, g, b, a,
                                  segments, 1.0f, 1, filled, 0.0f);

    if (vertices.empty())
        return;

    // Submit to render queue for deferred rendering
    rRenderStateKey state = filled ? rRenderStateKey::Colored(rBlendMode::Additive)
                                   : rRenderStateKey::Colored(rBlendMode::Alpha);

    if (filled)
    {
        rRenderQueue::Instance().Submit(rRenderPhase::Transparent, state, vertices.data(),
                                        vertices.size());
        sg_zoneStats.trianglesGenerated += vertices.size() / 3;
    }
    else
    {
        rRenderQueue::Instance().SubmitLines(rRenderPhase::Transparent, state, vertices.data(),
                                             vertices.size());
    }

    sg_zoneStats.zonesSubmitted++;
    sg_zoneStats.verticesGenerated += vertices.size();
}

void rSubmitCircularZoneProximity(float posX, float posY, float radius, float bottom, float height,
                                  float heightMult, float rotationAngle, float r, float g, float b,
                                  float a, int segments, float segmentLength, int segmentSteps,
                                  float floorRadiusPct, rZoneRenderMode mode)
{

    float top = bottom + height * heightMult;
    bool filled = (mode == rZoneRenderMode::Filled);

    // Generate geometry into reusable buffer
    static std::vector<rVertex20> vertices;
    vertices.clear();
    if (vertices.capacity() < 256) vertices.reserve(256);
    GenerateCircularZoneGeometry(vertices, posX, posY, radius, bottom, top, rotationAngle, r, g, b, a,
                                  segments, segmentLength, segmentSteps, filled, floorRadiusPct);

    if (vertices.empty())
        return;

    // Submit to render queue for deferred rendering
    rRenderStateKey state = filled ? rRenderStateKey::Colored(rBlendMode::Additive)
                                   : rRenderStateKey::Colored(rBlendMode::Alpha);

    if (filled)
    {
        rRenderQueue::Instance().Submit(rRenderPhase::Transparent, state, vertices.data(),
                                        vertices.size());
        sg_zoneStats.trianglesGenerated += vertices.size() / 3;
    }
    else
    {
        rRenderQueue::Instance().SubmitLines(rRenderPhase::Transparent, state, vertices.data(),
                                             vertices.size());
    }

    sg_zoneStats.zonesSubmitted++;
    sg_zoneStats.verticesGenerated += vertices.size();
}

void rSubmitPolygonZone(float posX, float posY, float scaleX, float scaleY, float bottom,
                        float height, float rotationAngle, const float* points, size_t numPoints,
                        float r, float g, float b, float a, rZoneRenderMode mode)
{
    if (numPoints < 2)
        return;

    float top = bottom + height;

    // Generate geometry into reusable buffer
    static std::vector<rVertex20> vertices;
    vertices.clear();
    if (vertices.capacity() < 256) vertices.reserve(256);
    size_t verts = GeneratePolygonZoneQuads(vertices, posX, posY, scaleX, scaleY, bottom, top,
                                             rotationAngle, points, numPoints, r, g, b, a);

    if (verts > 0)
    {
        // Submit to render queue for deferred rendering
        rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Additive);
        rRenderQueue::Instance().Submit(rRenderPhase::Transparent, state, vertices.data(), verts);

        sg_zoneStats.zonesSubmitted++;
        sg_zoneStats.verticesGenerated += verts;
        sg_zoneStats.trianglesGenerated += verts / 3;
    }
}


void rZoneRendererBeginFrame()
{
    // Reset statistics for new frame
    sg_zoneStats.Reset();
}

void rZoneRendererRender()
{
    // No-op: zones are rendered via rRenderQueue::Execute() now
}

rZoneRenderStats rZoneRendererGetStats()
{
    rZoneRenderStats stats;
    stats.zonesSubmitted = sg_zoneStats.zonesSubmitted;
    stats.verticesGenerated = sg_zoneStats.verticesGenerated;
    stats.trianglesGenerated = sg_zoneStats.trianglesGenerated;
    return stats;
}

#else // DEDICATED

// Stub implementations for dedicated server

void rSubmitCircularZone(float, float, float, float, float, float, float, float, float, float, int,
                         rZoneRenderMode)
{
}
void rSubmitCircularZoneProximity(float, float, float, float, float, float, float, float, float,
                                  float, float, int, float, int, float, rZoneRenderMode)
{
}
void rSubmitPolygonZone(float, float, float, float, float, float, float, const float*, size_t,
                        float, float, float, float, rZoneRenderMode)
{
}
void rZoneRendererBeginFrame() {}
void rZoneRendererRender() {}
rZoneRenderStats rZoneRendererGetStats()
{
    rZoneRenderStats stats = {0, 0, 0};
    return stats;
}

#endif // DEDICATED
