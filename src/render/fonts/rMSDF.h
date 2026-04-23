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

Multi-channel Signed Distance Field (MSDF) font rendering

*/

#ifndef RMSDF_H
#define RMSDF_H

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstdint>

// Include stb_truetype.h for stbtt_vertex type (no implementation, just declarations)
#include "stb_truetype.h"

namespace msdf
{

//! Compile-time constants for MSDF generation
constexpr float MSDF_MAX_RANGE = 8.0f;
constexpr float MSDF_MIN_CORNER_ANGLE = 3.0f;
constexpr int MSDF_DEFAULT_GLYPH_SIZE = 48;
constexpr int MSDF_MAX_CUBIC_ITERATIONS = 10;

//! Edge color channels for MSDF
//! Each edge is assigned a color channel to enable multi-channel distance computation
enum class EdgeColor : uint8_t
{
    BLACK = 0,
    RED = 1,
    GREEN = 2,
    BLUE = 4,
    YELLOW = RED | GREEN,   // 3
    CYAN = GREEN | BLUE,    // 6
    MAGENTA = RED | BLUE,   // 5
    WHITE = RED | GREEN | BLUE
};

inline EdgeColor operator|(EdgeColor a, EdgeColor b)
{
    return static_cast<EdgeColor>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline EdgeColor operator&(EdgeColor a, EdgeColor b)
{
    return static_cast<EdgeColor>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool hasChannel(EdgeColor color, EdgeColor channel)
{
    return (static_cast<uint8_t>(color) & static_cast<uint8_t>(channel)) != 0;
}

//! Result of signed distance computation
struct SignedDistance
{
    float distance;
    float dot;  // dot product for tie-breaking

    SignedDistance() : distance(-1e30f), dot(1.0f) {}
    SignedDistance(float d, float dt) : distance(d), dot(dt) {}

    bool operator<(const SignedDistance& other) const
    {
        return fabsf(distance) < fabsf(other.distance) ||
               (fabsf(distance) == fabsf(other.distance) && dot < other.dot);
    }

    bool operator>(const SignedDistance& other) const
    {
        return fabsf(distance) > fabsf(other.distance) ||
               (fabsf(distance) == fabsf(other.distance) && dot > other.dot);
    }
};

//! Abstract edge base class
struct Edge
{
    EdgeColor color = EdgeColor::WHITE;

    virtual ~Edge() = default;

    //! Compute signed distance from point p to this edge
    //! @param p Point to compute distance from
    //! @param param Output parameter t along the edge [0,1]
    //! @return Signed distance (negative inside, positive outside)
    virtual SignedDistance signedDistance(glm::vec2 p, float& param) const = 0;

    //! Get point on edge at parameter t
    virtual glm::vec2 point(float t) const = 0;

    //! Get direction (tangent) at parameter t
    virtual glm::vec2 direction(float t) const = 0;

    //! Clone this edge
    virtual std::unique_ptr<Edge> clone() const = 0;
};

//! Linear edge (line segment)
struct LinearEdge : Edge
{
    glm::vec2 p0, p1;

    LinearEdge(glm::vec2 a, glm::vec2 b) : p0(a), p1(b) {}

    SignedDistance signedDistance(glm::vec2 p, float& param) const override;
    glm::vec2 point(float t) const override;
    glm::vec2 direction(float t) const override;
    std::unique_ptr<Edge> clone() const override;
};

//! Quadratic Bezier edge
struct QuadraticEdge : Edge
{
    glm::vec2 p0, p1, p2;  // start, control, end

    QuadraticEdge(glm::vec2 a, glm::vec2 ctrl, glm::vec2 b) : p0(a), p1(ctrl), p2(b) {}

    SignedDistance signedDistance(glm::vec2 p, float& param) const override;
    glm::vec2 point(float t) const override;
    glm::vec2 direction(float t) const override;
    std::unique_ptr<Edge> clone() const override;
};

//! Cubic Bezier edge
struct CubicEdge : Edge
{
    glm::vec2 p0, p1, p2, p3;  // start, ctrl1, ctrl2, end

    CubicEdge(glm::vec2 a, glm::vec2 c1, glm::vec2 c2, glm::vec2 b)
        : p0(a), p1(c1), p2(c2), p3(b)
    {
    }

    SignedDistance signedDistance(glm::vec2 p, float& param) const override;
    glm::vec2 point(float t) const override;
    glm::vec2 direction(float t) const override;
    std::unique_ptr<Edge> clone() const override;
};

//! Contour: a closed loop of edges
struct Contour
{
    std::vector<std::unique_ptr<Edge>> edges;

    //! Calculate winding number (1 for CCW, -1 for CW)
    int winding() const;

    //! Add edge to contour
    void addEdge(std::unique_ptr<Edge> edge);
};

//! Shape: collection of contours that define a glyph
struct Shape
{
    std::vector<Contour> contours;

    //! Normalize the shape (reverse winding if needed)
    void normalize();

    //! Check if shape is empty
    bool isEmpty() const { return contours.empty(); }

    //! Get bounds of the shape
    void getBounds(float& left, float& bottom, float& right, float& top) const;
};

//! Generation bitmap template
template <typename Pixel>
struct Bitmap
{
    int width, height;
    std::vector<Pixel> data;

    Bitmap() : width(0), height(0) {}
    Bitmap(int w, int h) : width(w), height(h), data(w * h) {}

    Pixel& at(int x, int y)
    {
        return data[y * width + x];
    }

    const Pixel& at(int x, int y) const
    {
        return data[y * width + x];
    }

    void resize(int w, int h)
    {
        width = w;
        height = h;
        data.resize(w * h);
    }

    void clear(const Pixel& value = Pixel())
    {
        std::fill(data.begin(), data.end(), value);
    }
};

// Bitmap type aliases
using BitmapSDF = Bitmap<float>;         // Single channel
using BitmapMSDF = Bitmap<glm::vec3>;    // RGB float
using BitmapMTSDF = Bitmap<glm::vec4>;   // RGBA float

//! Convert stb_truetype vertices to Shape
//! @param vertices Array of stbtt_vertex from stbtt_GetGlyphShape
//! @param numVertices Number of vertices
//! @return Shape containing all contours
Shape shapeFromSTBVertices(const stbtt_vertex* vertices, int numVertices);

//! Apply edge coloring using simple corner-based strategy
//! @param shape Shape to color
//! @param angleThreshold Angle threshold in radians for corner detection
void colorEdges(Shape& shape, float angleThreshold = 3.0f);

//! Generate single-channel SDF bitmap
//! @param output Output bitmap (pre-allocated)
//! @param shape Input shape
//! @param scale Scale factor (font units to pixels)
//! @param translate Translation offset
//! @param range SDF range in pixels
void generateSDF(BitmapSDF& output, const Shape& shape,
                 float scale, glm::vec2 translate, float range);

//! Generate multi-channel SDF bitmap (RGB)
//! @param output Output bitmap (pre-allocated)
//! @param shape Input shape
//! @param scale Scale factor (font units to pixels)
//! @param translate Translation offset
//! @param range SDF range in pixels
void generateMSDF(BitmapMSDF& output, const Shape& shape,
                  float scale, glm::vec2 translate, float range);

//! Generate multi-channel SDF with true SDF in alpha (RGBA)
//! @param output Output bitmap (pre-allocated)
//! @param shape Input shape
//! @param scale Scale factor (font units to pixels)
//! @param translate Translation offset
//! @param range SDF range in pixels
void generateMTSDF(BitmapMTSDF& output, const Shape& shape,
                   float scale, glm::vec2 translate, float range);

//! Apply error correction to MSDF bitmap
//! Detects and fixes artifacts at edges where channels diverge
//! @param bitmap MSDF bitmap to correct (modified in place)
//! @param range SDF range in pixels (used for threshold calculation)
void errorCorrectionMSDF(BitmapMSDF& bitmap, float range);

//! Apply error correction to MTSDF bitmap
//! Detects and fixes artifacts at edges where channels diverge
//! @param bitmap MTSDF bitmap to correct (modified in place)
//! @param range SDF range in pixels (used for threshold calculation)
void errorCorrectionMTSDF(BitmapMTSDF& bitmap, float range);

}  // namespace msdf

#endif  // RMSDF_H
