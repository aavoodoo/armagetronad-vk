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

Multi-channel Signed Distance Field (MSDF) font rendering implementation

*/

#include "aa_config.h"

#ifndef DEDICATED

#include "rMSDF.h"
#include <algorithm>

// stb_truetype.h is already included via rMSDF.h, giving us stbtt_vertex and STBTT_* constants
#include <cmath>
#include <limits>

namespace msdf
{

//=============================================================================
// Helper functions
//=============================================================================

static inline float cross(glm::vec2 a, glm::vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

static inline float sign(float x)
{
    return x >= 0.0f ? 1.0f : -1.0f;
}

static inline int solveQuadratic(float a, float b, float c, float* roots)
{
    if (fabsf(a) < 1e-14f)
    {
        if (fabsf(b) < 1e-14f)
        {
            return 0;
        }
        roots[0] = -c / b;
        return 1;
    }

    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
    {
        return 0;
    }
    else if (disc == 0)
    {
        roots[0] = -b / (2.0f * a);
        return 1;
    }
    else
    {
        float sqrtDisc = sqrtf(disc);
        roots[0] = (-b + sqrtDisc) / (2.0f * a);
        roots[1] = (-b - sqrtDisc) / (2.0f * a);
        return 2;
    }
}

static inline int solveCubicNormed(float a, float b, float c, float* roots)
{
    float a2 = a * a;
    float q = (a2 - 3.0f * b) / 9.0f;
    float r = (a * (2.0f * a2 - 9.0f * b) + 27.0f * c) / 54.0f;
    float r2 = r * r;
    float q3 = q * q * q;

    if (r2 < q3)
    {
        float t = r / sqrtf(q3);
        if (t < -1.0f)
            t = -1.0f;
        if (t > 1.0f)
            t = 1.0f;
        t = acosf(t);
        a /= 3.0f;
        q = -2.0f * sqrtf(q);
        roots[0] = q * cosf(t / 3.0f) - a;
        roots[1] = q * cosf((t + 2.0f * 3.14159265f) / 3.0f) - a;
        roots[2] = q * cosf((t - 2.0f * 3.14159265f) / 3.0f) - a;
        return 3;
    }
    else
    {
        float aa = -powf(fabsf(r) + sqrtf(r2 - q3), 1.0f / 3.0f);
        if (r < 0.0f)
            aa = -aa;
        float bb = (aa == 0.0f) ? 0.0f : q / aa;
        a /= 3.0f;
        roots[0] = (aa + bb) - a;
        roots[1] = -0.5f * (aa + bb) - a;
        roots[2] = 0.5f * sqrtf(3.0f) * (aa - bb);
        if (fabsf(roots[2]) < 1e-14f)
        {
            return 2;
        }
        return 1;
    }
}

static inline int solveCubic(float a, float b, float c, float d, float* roots)
{
    if (fabsf(a) < 1e-14f)
    {
        return solveQuadratic(b, c, d, roots);
    }
    return solveCubicNormed(b / a, c / a, d / a, roots);
}

//=============================================================================
// LinearEdge implementation
//=============================================================================

SignedDistance LinearEdge::signedDistance(glm::vec2 p, float& param) const
{
    glm::vec2 ab = p1 - p0;
    glm::vec2 ap = p - p0;

    float abLen2 = glm::dot(ab, ab);
    if (abLen2 < 1e-14f)
    {
        param = 0.0f;
        return SignedDistance(glm::length(ap), 0.0f);
    }

    param = glm::dot(ab, ap) / abLen2;
    param = glm::clamp(param, 0.0f, 1.0f);

    glm::vec2 closest = p0 + ab * param;
    glm::vec2 toP = p - closest;

    float dist = glm::length(toP);
    float crossVal = cross(ab, ap);
    float signVal = sign(crossVal);

    glm::vec2 dir = glm::normalize(ab);
    float dotVal = (param >= 0.0f && param <= 1.0f) ? fabsf(glm::dot(glm::normalize(toP), dir)) : 1.0f;

    return SignedDistance(signVal * dist, dotVal);
}

glm::vec2 LinearEdge::point(float t) const
{
    return glm::mix(p0, p1, t);
}

glm::vec2 LinearEdge::direction(float t) const
{
    return p1 - p0;
}

std::unique_ptr<Edge> LinearEdge::clone() const
{
    auto e = std::make_unique<LinearEdge>(p0, p1);
    e->color = color;
    return e;
}

//=============================================================================
// QuadraticEdge implementation
//=============================================================================

SignedDistance QuadraticEdge::signedDistance(glm::vec2 p, float& param) const
{
    glm::vec2 qa = p0 - p;
    glm::vec2 ab = p1 - p0;
    glm::vec2 br = p0 + p2 - p1 - p1;

    float a = glm::dot(br, br);
    float b = 3.0f * glm::dot(ab, br);
    float c = 2.0f * glm::dot(ab, ab) + glm::dot(qa, br);
    float d = glm::dot(qa, ab);

    float roots[3];
    int numRoots = solveCubic(a, b, c, d, roots);

    SignedDistance minDist;
    param = 0.0f;

    // Check endpoints
    float dist0 = glm::length(qa);
    glm::vec2 toP0 = -qa;
    float dot0 = fabsf(glm::dot(glm::normalize(toP0.x != 0 || toP0.y != 0 ? toP0 : glm::vec2(1, 0)),
                                glm::normalize(direction(0))));
    SignedDistance sd0(sign(cross(direction(0), toP0)) * dist0, dot0);

    glm::vec2 ep1 = p2 - p;
    float dist1 = glm::length(ep1);
    glm::vec2 toP1 = -ep1;
    float dot1 = fabsf(glm::dot(glm::normalize(toP1.x != 0 || toP1.y != 0 ? toP1 : glm::vec2(1, 0)),
                                glm::normalize(direction(1))));
    SignedDistance sd1(sign(cross(direction(1), toP1)) * dist1, dot1);

    if (sd0 < sd1)
    {
        minDist = sd0;
        param = 0.0f;
    }
    else
    {
        minDist = sd1;
        param = 1.0f;
    }

    // Check roots
    for (int i = 0; i < numRoots; ++i)
    {
        float t = roots[i];
        if (t > 0.0f && t < 1.0f)
        {
            glm::vec2 qe = point(t) - p;
            float distT = glm::length(qe);
            glm::vec2 toP = -qe;
            glm::vec2 dir = direction(t);
            float dotT = fabsf(glm::dot(glm::normalize(toP.x != 0 || toP.y != 0 ? toP : glm::vec2(1, 0)),
                                        glm::normalize(dir)));
            SignedDistance sdT(sign(cross(dir, toP)) * distT, dotT);
            if (sdT < minDist)
            {
                minDist = sdT;
                param = t;
            }
        }
    }

    return minDist;
}

glm::vec2 QuadraticEdge::point(float t) const
{
    float t2 = 1.0f - t;
    return t2 * t2 * p0 + 2.0f * t2 * t * p1 + t * t * p2;
}

glm::vec2 QuadraticEdge::direction(float t) const
{
    glm::vec2 tangent = glm::mix(p1 - p0, p2 - p1, t);
    if (glm::length(tangent) < 1e-14f)
    {
        return p2 - p0;
    }
    return tangent;
}

std::unique_ptr<Edge> QuadraticEdge::clone() const
{
    auto e = std::make_unique<QuadraticEdge>(p0, p1, p2);
    e->color = color;
    return e;
}

//=============================================================================
// CubicEdge implementation
//=============================================================================

SignedDistance CubicEdge::signedDistance(glm::vec2 p, float& param) const
{
    // Use iterative refinement with multiple starting points
    SignedDistance minDist;
    param = 0.0f;

    // Check endpoints
    glm::vec2 ep0 = p0 - p;
    float dist0 = glm::length(ep0);
    glm::vec2 dir0 = direction(0);
    float dot0 = glm::length(dir0) > 1e-14f
                     ? fabsf(glm::dot(glm::normalize(-ep0.x != 0 || -ep0.y != 0 ? -ep0 : glm::vec2(1, 0)),
                                      glm::normalize(dir0)))
                     : 1.0f;
    SignedDistance sd0(sign(cross(dir0, -ep0)) * dist0, dot0);

    glm::vec2 ep1 = p3 - p;
    float dist1 = glm::length(ep1);
    glm::vec2 dir1 = direction(1);
    float dot1 = glm::length(dir1) > 1e-14f
                     ? fabsf(glm::dot(glm::normalize(-ep1.x != 0 || -ep1.y != 0 ? -ep1 : glm::vec2(1, 0)),
                                      glm::normalize(dir1)))
                     : 1.0f;
    SignedDistance sd1(sign(cross(dir1, -ep1)) * dist1, dot1);

    if (sd0 < sd1)
    {
        minDist = sd0;
        param = 0.0f;
    }
    else
    {
        minDist = sd1;
        param = 1.0f;
    }

    // Sample and refine
    const int numSteps = 8;
    for (int i = 1; i < numSteps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(numSteps);

        // Newton-Raphson refinement
        for (int iter = 0; iter < MSDF_MAX_CUBIC_ITERATIONS; ++iter)
        {
            glm::vec2 qe = point(t) - p;
            glm::vec2 d1 = direction(t);

            float d1Len = glm::dot(d1, d1);
            if (d1Len < 1e-14f)
                break;

            float step = glm::dot(qe, d1) / d1Len;
            t -= step;

            if (fabsf(step) < 1e-6f)
                break;
        }

        t = glm::clamp(t, 0.0f, 1.0f);

        glm::vec2 qe = point(t) - p;
        float distT = glm::length(qe);
        glm::vec2 dirT = direction(t);
        float dotT = glm::length(dirT) > 1e-14f
                         ? fabsf(glm::dot(glm::normalize(-qe.x != 0 || -qe.y != 0 ? -qe : glm::vec2(1, 0)),
                                          glm::normalize(dirT)))
                         : 1.0f;
        SignedDistance sdT(sign(cross(dirT, -qe)) * distT, dotT);

        if (sdT < minDist)
        {
            minDist = sdT;
            param = t;
        }
    }

    return minDist;
}

glm::vec2 CubicEdge::point(float t) const
{
    float t2 = 1.0f - t;
    return t2 * t2 * t2 * p0 + 3.0f * t2 * t2 * t * p1 + 3.0f * t2 * t * t * p2 + t * t * t * p3;
}

glm::vec2 CubicEdge::direction(float t) const
{
    glm::vec2 tangent = glm::mix(glm::mix(p1 - p0, p2 - p1, t), glm::mix(p2 - p1, p3 - p2, t), t);
    if (glm::length(tangent) < 1e-14f)
    {
        tangent = p2 - p0;
    }
    if (glm::length(tangent) < 1e-14f)
    {
        tangent = p3 - p1;
    }
    if (glm::length(tangent) < 1e-14f)
    {
        tangent = p3 - p0;
    }
    return tangent;
}

std::unique_ptr<Edge> CubicEdge::clone() const
{
    auto e = std::make_unique<CubicEdge>(p0, p1, p2, p3);
    e->color = color;
    return e;
}

//=============================================================================
// Contour implementation
//=============================================================================

int Contour::winding() const
{
    if (edges.empty())
        return 0;

    float total = 0.0f;
    glm::vec2 prev = edges.back()->point(1.0f);

    for (const auto& edge : edges)
    {
        glm::vec2 curr = edge->point(0.0f);
        total += (curr.x - prev.x) * (curr.y + prev.y);

        // Sample along edge
        for (int i = 1; i <= 4; ++i)
        {
            float t = static_cast<float>(i) / 4.0f;
            glm::vec2 next = edge->point(t);
            total += (next.x - curr.x) * (next.y + curr.y);
            curr = next;
        }
        prev = curr;
    }

    return total > 0.0f ? 1 : -1;
}

void Contour::addEdge(std::unique_ptr<Edge> edge)
{
    edges.push_back(std::move(edge));
}

//=============================================================================
// Shape implementation
//=============================================================================

void Shape::normalize()
{
    for (auto& contour : contours)
    {
        if (contour.edges.size() == 1)
        {
            // Split single-edge contours into thirds
            auto& edge = contour.edges[0];
            glm::vec2 start = edge->point(0.0f);
            glm::vec2 p1 = edge->point(1.0f / 3.0f);
            glm::vec2 p2 = edge->point(2.0f / 3.0f);
            glm::vec2 end = edge->point(1.0f);

            contour.edges.clear();
            contour.addEdge(std::make_unique<LinearEdge>(start, p1));
            contour.addEdge(std::make_unique<LinearEdge>(p1, p2));
            contour.addEdge(std::make_unique<LinearEdge>(p2, end));
        }
    }
}

void Shape::getBounds(float& left, float& bottom, float& right, float& top) const
{
    left = bottom = std::numeric_limits<float>::max();
    right = top = std::numeric_limits<float>::lowest();

    for (const auto& contour : contours)
    {
        for (const auto& edge : contour.edges)
        {
            for (int i = 0; i <= 4; ++i)
            {
                float t = static_cast<float>(i) / 4.0f;
                glm::vec2 p = edge->point(t);
                left = std::min(left, p.x);
                bottom = std::min(bottom, p.y);
                right = std::max(right, p.x);
                top = std::max(top, p.y);
            }
        }
    }
}

//=============================================================================
// stbtt_vertex to Shape conversion
//=============================================================================

Shape shapeFromSTBVertices(const stbtt_vertex* vertices, int numVertices)
{
    Shape shape;
    Contour* currentContour = nullptr;
    glm::vec2 startPoint(0.0f);
    glm::vec2 currentPoint(0.0f);

    for (int i = 0; i < numVertices; ++i)
    {
        const stbtt_vertex& v = vertices[i];
        glm::vec2 p(static_cast<float>(v.x), static_cast<float>(v.y));

        switch (v.type)
        {
        case STBTT_vmove:
            // Start new contour
            shape.contours.push_back(Contour());
            currentContour = &shape.contours.back();
            startPoint = p;
            currentPoint = p;
            break;

        case STBTT_vline:
            if (currentContour && glm::length(p - currentPoint) > 1e-6f)
            {
                currentContour->addEdge(std::make_unique<LinearEdge>(currentPoint, p));
            }
            currentPoint = p;
            break;

        case STBTT_vcurve:
        {
            glm::vec2 ctrl(static_cast<float>(v.cx), static_cast<float>(v.cy));
            if (currentContour)
            {
                currentContour->addEdge(std::make_unique<QuadraticEdge>(currentPoint, ctrl, p));
            }
            currentPoint = p;
            break;
        }

        case STBTT_vcubic:
        {
            glm::vec2 ctrl1(static_cast<float>(v.cx), static_cast<float>(v.cy));
            glm::vec2 ctrl2(static_cast<float>(v.cx1), static_cast<float>(v.cy1));
            if (currentContour)
            {
                currentContour->addEdge(std::make_unique<CubicEdge>(currentPoint, ctrl1, ctrl2, p));
            }
            currentPoint = p;
            break;
        }
        }
    }

    // Close contours (connect last point to start if needed)
    for (auto& contour : shape.contours)
    {
        if (!contour.edges.empty())
        {
            glm::vec2 first = contour.edges.front()->point(0.0f);
            glm::vec2 last = contour.edges.back()->point(1.0f);
            if (glm::length(last - first) > 1e-6f)
            {
                contour.addEdge(std::make_unique<LinearEdge>(last, first));
            }
        }
    }

    // Remove empty contours
    shape.contours.erase(
        std::remove_if(shape.contours.begin(), shape.contours.end(),
                       [](const Contour& c) { return c.edges.empty(); }),
        shape.contours.end());

    return shape;
}

//=============================================================================
// Edge coloring
//=============================================================================

void colorEdges(Shape& shape, float angleThreshold)
{
    const EdgeColor colors[] = {EdgeColor::CYAN, EdgeColor::MAGENTA, EdgeColor::YELLOW};

    for (auto& contour : shape.contours)
    {
        if (contour.edges.empty())
            continue;

        // Find corners
        std::vector<bool> isCorner(contour.edges.size(), false);
        std::vector<int> cornerIndices;

        for (size_t i = 0; i < contour.edges.size(); ++i)
        {
            size_t prevIdx = (i + contour.edges.size() - 1) % contour.edges.size();
            glm::vec2 prevDir = contour.edges[prevIdx]->direction(1.0f);
            glm::vec2 currDir = contour.edges[i]->direction(0.0f);

            float prevLen = glm::length(prevDir);
            float currLen = glm::length(currDir);

            if (prevLen < 1e-14f || currLen < 1e-14f)
                continue;

            prevDir /= prevLen;
            currDir /= currLen;

            float crossVal = cross(prevDir, currDir);
            float dotVal = glm::dot(prevDir, currDir);

            // Check if this is a corner (sharp angle change)
            if (fabsf(crossVal) > sinf(angleThreshold) ||
                (dotVal < 0.0f && fabsf(crossVal) < sinf(angleThreshold)))
            {
                isCorner[i] = true;
                cornerIndices.push_back(static_cast<int>(i));
            }
        }

        // Assign colors
        if (cornerIndices.empty())
        {
            // No corners - use single color for all edges
            for (auto& edge : contour.edges)
            {
                edge->color = EdgeColor::WHITE;
            }
        }
        else if (cornerIndices.size() == 1)
        {
            // Single corner - need at least 3 colors
            int colorIdx = 0;
            for (auto& edge : contour.edges)
            {
                edge->color = colors[colorIdx % 3];
                colorIdx++;
            }
        }
        else
        {
            // Multiple corners - cycle colors at corners
            int colorIdx = 0;
            int currentCorner = 0;

            for (size_t i = 0; i < contour.edges.size(); ++i)
            {
                if (isCorner[i])
                {
                    colorIdx = (colorIdx + 1) % 3;
                }
                contour.edges[i]->color = colors[colorIdx];
            }
        }
    }
}

//=============================================================================
// SDF Generation
//=============================================================================

static float computeSignedDistance(const Shape& shape, glm::vec2 p)
{
    // Use proper SignedDistance comparison (same approach as MSDF)
    SignedDistance minDist;

    for (const auto& contour : shape.contours)
    {
        for (const auto& edge : contour.edges)
        {
            float param;
            SignedDistance sd = edge->signedDistance(p, param);
            if (sd < minDist)
            {
                minDist = sd;
            }
        }
    }

    return minDist.distance;
}

void generateSDF(BitmapSDF& output, const Shape& shape,
                 float scale, glm::vec2 translate, float range)
{
    for (int y = 0; y < output.height; ++y)
    {
        for (int x = 0; x < output.width; ++x)
        {
            // Flip Y coordinate: stb_truetype uses Y-up, bitmap uses Y-down
            float flippedY = static_cast<float>(output.height - 1 - y);
            glm::vec2 p((static_cast<float>(x) + 0.5f - translate.x) / scale,
                        (flippedY + 0.5f - translate.y) / scale);

            float sd = computeSignedDistance(shape, p);
            // Convert distance from font units to pixels, then normalize by range
            // Negate: stb_truetype winding is opposite to MSDF convention
            output.at(x, y) = -sd * scale / range + 0.5f;
        }
    }
}

//=============================================================================
// MSDF Generation
//=============================================================================

static float median(float a, float b, float c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

void generateMSDF(BitmapMSDF& output, const Shape& shape,
                  float scale, glm::vec2 translate, float range)
{
    for (int y = 0; y < output.height; ++y)
    {
        for (int x = 0; x < output.width; ++x)
        {
            // Flip Y coordinate: stb_truetype uses Y-up, bitmap uses Y-down
            float flippedY = static_cast<float>(output.height - 1 - y);
            glm::vec2 p((static_cast<float>(x) + 0.5f - translate.x) / scale,
                        (flippedY + 0.5f - translate.y) / scale);

            // Track closest edge for each channel using proper SignedDistance comparison
            SignedDistance minDistR, minDistG, minDistB;

            for (const auto& contour : shape.contours)
            {
                for (const auto& edge : contour.edges)
                {
                    float param;
                    SignedDistance sd = edge->signedDistance(p, param);

                    // For each channel, keep the closest edge (smallest SignedDistance)
                    // SignedDistance comparison considers both distance and pseudo-distance
                    if (hasChannel(edge->color, EdgeColor::RED))
                    {
                        if (sd < minDistR)
                            minDistR = sd;
                    }
                    if (hasChannel(edge->color, EdgeColor::GREEN))
                    {
                        if (sd < minDistG)
                            minDistG = sd;
                    }
                    if (hasChannel(edge->color, EdgeColor::BLUE))
                    {
                        if (sd < minDistB)
                            minDistB = sd;
                    }
                }
            }

            // Convert signed distances to output values
            // Scale from font units to pixels, then normalize by range
            // Negate: stb_truetype winding is opposite to MSDF convention
            output.at(x, y) = glm::vec3(
                -minDistR.distance * scale / range + 0.5f,
                -minDistG.distance * scale / range + 0.5f,
                -minDistB.distance * scale / range + 0.5f);
        }
    }
}

void generateMTSDF(BitmapMTSDF& output, const Shape& shape,
                   float scale, glm::vec2 translate, float range)
{
    for (int y = 0; y < output.height; ++y)
    {
        for (int x = 0; x < output.width; ++x)
        {
            // Flip Y coordinate: stb_truetype uses Y-up, bitmap uses Y-down
            float flippedY = static_cast<float>(output.height - 1 - y);
            glm::vec2 p((static_cast<float>(x) + 0.5f - translate.x) / scale,
                        (flippedY + 0.5f - translate.y) / scale);

            // Track closest edge for each channel using proper SignedDistance comparison
            SignedDistance minDistR, minDistG, minDistB;
            SignedDistance minDistTrue;  // True SDF (all edges, no color filtering)

            for (const auto& contour : shape.contours)
            {
                for (const auto& edge : contour.edges)
                {
                    float param;
                    SignedDistance sd = edge->signedDistance(p, param);

                    // True SDF: closest edge regardless of color
                    if (sd < minDistTrue)
                        minDistTrue = sd;

                    // For each channel, keep the closest edge
                    if (hasChannel(edge->color, EdgeColor::RED))
                    {
                        if (sd < minDistR)
                            minDistR = sd;
                    }
                    if (hasChannel(edge->color, EdgeColor::GREEN))
                    {
                        if (sd < minDistG)
                            minDistG = sd;
                    }
                    if (hasChannel(edge->color, EdgeColor::BLUE))
                    {
                        if (sd < minDistB)
                            minDistB = sd;
                    }
                }
            }

            // Convert signed distances to output values
            // Scale from font units to pixels, then normalize by range
            // Negate: stb_truetype winding is opposite to MSDF convention
            output.at(x, y) = glm::vec4(
                -minDistR.distance * scale / range + 0.5f,
                -minDistG.distance * scale / range + 0.5f,
                -minDistB.distance * scale / range + 0.5f,
                -minDistTrue.distance * scale / range + 0.5f);
        }
    }
}

//=============================================================================
// Error Correction
//=============================================================================

// Detect if a pixel has an artifact based on channel differences
// Returns true if the pixel should be corrected
static bool detectArtifact(const glm::vec3& pixel, float threshold)
{
    float med = median(pixel.r, pixel.g, pixel.b);

    // Check if any channel deviates significantly from median
    float maxDev = std::max({
        fabsf(pixel.r - med),
        fabsf(pixel.g - med),
        fabsf(pixel.b - med)
    });

    return maxDev > threshold;
}

// Check if pixel is near an edge (median close to 0.5)
static bool isNearEdge(const glm::vec3& pixel, float edgeThreshold)
{
    float med = median(pixel.r, pixel.g, pixel.b);
    return fabsf(med - 0.5f) < edgeThreshold;
}

void errorCorrectionMSDF(BitmapMSDF& bitmap, float range)
{
    // Threshold for detecting artifacts: scaled by range
    // Typical value: 1.001 / range (from msdfgen)
    float threshold = 1.001f / range;
    float edgeThreshold = 0.5f / range;

    // First pass: identify pixels that need correction
    std::vector<bool> needsCorrection(bitmap.width * bitmap.height, false);

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            const glm::vec3& pixel = bitmap.at(x, y);

            // Only correct pixels near edges that show artifacts
            if (isNearEdge(pixel, edgeThreshold) && detectArtifact(pixel, threshold))
            {
                needsCorrection[y * bitmap.width + x] = true;
            }
        }
    }

    // Second pass: apply corrections
    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            if (needsCorrection[y * bitmap.width + x])
            {
                glm::vec3& pixel = bitmap.at(x, y);
                float med = median(pixel.r, pixel.g, pixel.b);
                pixel = glm::vec3(med, med, med);
            }
        }
    }
}

void errorCorrectionMTSDF(BitmapMTSDF& bitmap, float range)
{
    // Same threshold as MSDF
    float threshold = 1.001f / range;
    float edgeThreshold = 0.5f / range;

    // First pass: identify pixels that need correction
    std::vector<bool> needsCorrection(bitmap.width * bitmap.height, false);

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            const glm::vec4& pixel = bitmap.at(x, y);
            glm::vec3 rgb(pixel.r, pixel.g, pixel.b);

            if (isNearEdge(rgb, edgeThreshold) && detectArtifact(rgb, threshold))
            {
                needsCorrection[y * bitmap.width + x] = true;
            }
        }
    }

    // Second pass: apply corrections (preserve alpha channel)
    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            if (needsCorrection[y * bitmap.width + x])
            {
                glm::vec4& pixel = bitmap.at(x, y);
                float med = median(pixel.r, pixel.g, pixel.b);
                pixel.r = med;
                pixel.g = med;
                pixel.b = med;
                // Keep alpha (true SDF) unchanged
            }
        }
    }
}

}  // namespace msdf

#endif  // DEDICATED
