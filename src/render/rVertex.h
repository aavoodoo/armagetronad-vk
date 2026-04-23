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

#ifndef RVERTEX_H
#define RVERTEX_H

#include <cstdint>
#include <cstring>
#include <cstddef>

//! @file rVertex.h
//! Unified vertex formats for the modern GL3 renderer.

//! Clamp float [0..1] to uint8_t [0..255]
inline uint8_t rFloatToU8(float f) {
    return static_cast<uint8_t>(f < 0 ? 0 : (f > 1 ? 255 : f * 255.0f));
}
//!
//! Two formats cover all rendering needs:
//! - rVertex20: Standard format for 90% of geometry (walls, zones, 2D, effects)
//! - rVertexLit32: Lit format for cycles and other lit models

//=============================================================================
// rVertex20 - Standard vertex format (20 bytes)
//=============================================================================
//!
//! Compact vertex format for most game geometry:
//! - Walls (quads and lines)
//! - Zones (triangle fans)
//! - Sky and floor
//! - Effects (sparks, explosions)
//! - HUD elements
//! - Text rendering
//!
//! Attribute layout (for VAO setup):
//! - location 0: vec3 position  (3 floats, offset 0)
//! - location 1: vec4 color     (4 ubytes normalized, offset 12)
//! - location 2: vec2 texcoord  (2 shorts normalized, offset 16)
//!
struct rVertex20
{
    float position[3];    //!< World position (12 bytes)
    uint8_t color[4];     //!< RGBA color, normalized 0-255 -> 0.0-1.0 (4 bytes)
    int16_t texcoord[2];  //!< UV coordinates, normalized -32767..32767 -> -1.0..1.0 (4 bytes)

    //! Default constructor - zero initialized
    rVertex20()
    {
        std::memset(this, 0, sizeof(rVertex20));
    }

    //! Full constructor
    rVertex20(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float u,
              float v)
    {
        SetPosition(x, y, z);
        SetColor(r, g, b, a);
        SetTexCoord(u, v);
    }

    //! Position-only constructor (white, no texture)
    rVertex20(float x, float y, float z)
    {
        SetPosition(x, y, z);
        SetColor(255, 255, 255, 255);
        SetTexCoord(0.0f, 0.0f);
    }

    //! Set position
    void SetPosition(float x, float y, float z)
    {
        position[0] = x;
        position[1] = y;
        position[2] = z;
    }

    //! Set color from bytes (0-255)
    void SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }

    //! Set color from floats (0.0-1.0)
    void SetColorF(float r, float g, float b, float a = 1.0f)
    {
        color[0] = static_cast<uint8_t>(r < 0 ? 0 : (r > 1 ? 255 : r * 255.0f));
        color[1] = static_cast<uint8_t>(g < 0 ? 0 : (g > 1 ? 255 : g * 255.0f));
        color[2] = static_cast<uint8_t>(b < 0 ? 0 : (b > 1 ? 255 : b * 255.0f));
        color[3] = static_cast<uint8_t>(a < 0 ? 0 : (a > 1 ? 255 : a * 255.0f));
    }

    //! Set texture coordinates from floats
    //! Input range: typically 0.0-1.0 but can exceed for tiling
    //! Stored as normalized int16: multiply by 32767
    void SetTexCoord(float u, float v)
    {
        // Clamp to valid int16 range after scaling
        float su = u * 32767.0f;
        float sv = v * 32767.0f;
        texcoord[0] =
            static_cast<int16_t>(su > 32767.0f ? 32767 : (su < -32767.0f ? -32767 : su));
        texcoord[1] =
            static_cast<int16_t>(sv > 32767.0f ? 32767 : (sv < -32767.0f ? -32767 : sv));
    }

    //! Set texture coordinates from raw int16 values
    void SetTexCoordRaw(int16_t u, int16_t v)
    {
        texcoord[0] = u;
        texcoord[1] = v;
    }

    // Attribute offsets for VAO setup
    static constexpr size_t OffsetPosition() { return 0; }
    static constexpr size_t OffsetColor() { return 12; }
    static constexpr size_t OffsetTexCoord() { return 16; }
    static constexpr size_t Stride() { return sizeof(rVertex20); }
};

static_assert(sizeof(rVertex20) == 20, "rVertex20 must be exactly 20 bytes");
static_assert(offsetof(rVertex20, color)    == 12, "rVertex20 color offset mismatch — pipeline vertex descriptor needs updating");
static_assert(offsetof(rVertex20, texcoord) == 16, "rVertex20 texcoord offset mismatch — pipeline vertex descriptor needs updating");

//=============================================================================
// rVertexLit32 - Lit vertex format (32 bytes)
//=============================================================================
//!
//! Vertex format for lit geometry (cycles, lit models):
//! - Includes normal vector for lighting calculations
//! - 8 bytes reserved for future use (tangent, etc.)
//!
//! Attribute layout (for VAO setup):
//! - location 0: vec3 position  (3 floats, offset 0)
//! - location 1: vec3 normal    (3 bytes normalized + 1 padding, offset 12)
//! - location 2: vec4 color     (4 ubytes normalized, offset 16)
//! - location 3: vec2 texcoord  (2 shorts normalized, offset 20)
//!
struct rVertexLit32
{
    float position[3];    //!< World position (12 bytes)
    int8_t normal[4];     //!< Normal vector xyz + padding, normalized -127..127 -> -1.0..1.0 (4 bytes)
    uint8_t color[4];     //!< RGBA color, normalized 0-255 -> 0.0-1.0 (4 bytes)
    int16_t texcoord[2];  //!< UV coordinates, normalized -32767..32767 -> -1.0..1.0 (4 bytes)
    uint32_t reserved[2]; //!< Reserved for future use (tangent, etc.) (8 bytes)

    //! Default constructor - zero initialized
    rVertexLit32()
    {
        std::memset(this, 0, sizeof(rVertexLit32));
    }

    //! Full constructor
    rVertexLit32(float x, float y, float z, float nx, float ny, float nz, uint8_t r, uint8_t g,
                 uint8_t b, uint8_t a, float u, float v)
    {
        SetPosition(x, y, z);
        SetNormal(nx, ny, nz);
        SetColor(r, g, b, a);
        SetTexCoord(u, v);
        reserved[0] = 0;
        reserved[1] = 0;
    }

    //! Set position
    void SetPosition(float x, float y, float z)
    {
        position[0] = x;
        position[1] = y;
        position[2] = z;
    }

    //! Set normal from floats (-1.0 to 1.0)
    void SetNormal(float nx, float ny, float nz)
    {
        normal[0] = static_cast<int8_t>(nx * 127.0f);
        normal[1] = static_cast<int8_t>(ny * 127.0f);
        normal[2] = static_cast<int8_t>(nz * 127.0f);
        normal[3] = 0; // padding
    }

    //! Set color from bytes (0-255)
    void SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }

    //! Set color from floats (0.0-1.0)
    void SetColorF(float r, float g, float b, float a = 1.0f)
    {
        color[0] = static_cast<uint8_t>(r < 0 ? 0 : (r > 1 ? 255 : r * 255.0f));
        color[1] = static_cast<uint8_t>(g < 0 ? 0 : (g > 1 ? 255 : g * 255.0f));
        color[2] = static_cast<uint8_t>(b < 0 ? 0 : (b > 1 ? 255 : b * 255.0f));
        color[3] = static_cast<uint8_t>(a < 0 ? 0 : (a > 1 ? 255 : a * 255.0f));
    }

    //! Set texture coordinates from floats
    void SetTexCoord(float u, float v)
    {
        float su = u * 32767.0f;
        float sv = v * 32767.0f;
        texcoord[0] =
            static_cast<int16_t>(su > 32767.0f ? 32767 : (su < -32767.0f ? -32767 : su));
        texcoord[1] =
            static_cast<int16_t>(sv > 32767.0f ? 32767 : (sv < -32767.0f ? -32767 : sv));
    }

    // Attribute offsets for VAO setup
    static constexpr size_t OffsetPosition() { return 0; }
    static constexpr size_t OffsetNormal() { return 12; }
    static constexpr size_t OffsetColor() { return 16; }
    static constexpr size_t OffsetTexCoord() { return 20; }
    static constexpr size_t Stride() { return sizeof(rVertexLit32); }
};

static_assert(sizeof(rVertexLit32) == 32, "rVertexLit32 must be exactly 32 bytes");
static_assert(offsetof(rVertexLit32, normal)   == 12, "rVertexLit32 normal offset mismatch — pipeline vertex descriptor needs updating");
static_assert(offsetof(rVertexLit32, color)    == 16, "rVertexLit32 color offset mismatch — pipeline vertex descriptor needs updating");
static_assert(offsetof(rVertexLit32, texcoord) == 20, "rVertexLit32 texcoord offset mismatch — pipeline vertex descriptor needs updating");

//=============================================================================
// Vertex attribute locations (must match shader layout)
//=============================================================================
namespace rVertexAttrib
{
constexpr int Position = 0;
constexpr int ColorOrNormal = 1;  // Color for rVertex20, Normal for rVertexLit32
constexpr int TexCoordOrColor = 2; // TexCoord for rVertex20, Color for rVertexLit32
constexpr int TexCoord = 3;        // TexCoord for rVertexLit32 only

// Instancing attributes (per-instance data)
constexpr int ModelCol0 = 4;
constexpr int ModelCol1 = 5;
constexpr int ModelCol2 = 6;
constexpr int ModelCol3 = 7;
constexpr int InstanceColor = 8;
} // namespace rVertexAttrib

//=============================================================================
// Primitive types
//=============================================================================
enum class rPrimitiveType
{
    Triangles,
    TriangleFan,
    TriangleStrip,
    Lines,
    LineStrip,
    LineLoop,
    Points
};

#endif // RVERTEX_H
