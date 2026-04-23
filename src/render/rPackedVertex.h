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

#ifndef RPACKEDVERTEX_H
#define RPACKEDVERTEX_H

#include <cstdint>
#include <cmath>
#include <algorithm>

//! Packed vertex format for wall rendering (24 bytes vs 36 bytes for rStaticVertex)
//! Layout: position (3f=12), color RGBA8 (4b=4), texcoord (2f=8) = 24 bytes per vertex
//! This reduces vertex bandwidth by 33% compared to float4 colors.
struct rPackedWallVertex
{
    float position[3];    // 12 bytes - xyz position
    uint8_t color[4];     // 4 bytes - RGBA8 normalized color
    float texcoord[2];    // 8 bytes - uv texture coordinates
    // Total: 24 bytes

    rPackedWallVertex()
    {
        position[0] = position[1] = position[2] = 0.0f;
        color[0] = color[1] = color[2] = color[3] = 255;
        texcoord[0] = texcoord[1] = 0.0f;
    }

    rPackedWallVertex(float px, float py, float pz,
                      float tu, float tv,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
    {
        position[0] = px;
        position[1] = py;
        position[2] = pz;
        texcoord[0] = tu;
        texcoord[1] = tv;
        SetColorF(r, g, b, a);
    }

    //! Set color from floats [0,1] to packed RGBA8
    void SetColorF(float r, float g, float b, float a)
    {
        color[0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * 255.0f)));
        color[1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * 255.0f)));
        color[2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * 255.0f)));
        color[3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, a * 255.0f)));
    }

    //! Set color from uint8 values directly
    void SetColorU8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }
};

// Static assertion to ensure proper packing
static_assert(sizeof(rPackedWallVertex) == 24, "rPackedWallVertex must be exactly 24 bytes");

//! Line vertex format for wall top edge rendering (20 bytes)
//! Layout: position (3f=12), color RGBA8 (4b=4), unused padding (4b) = 20 bytes
//! (padding to 4-byte alignment for efficient GPU access)
struct rPackedLineVertex
{
    float position[3];    // 12 bytes - xyz position
    uint8_t color[4];     // 4 bytes - RGBA8 normalized color
    // Total: 16 bytes (no texcoord needed for lines)

    rPackedLineVertex()
    {
        position[0] = position[1] = position[2] = 0.0f;
        color[0] = color[1] = color[2] = color[3] = 255;
    }

    rPackedLineVertex(float px, float py, float pz,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
    {
        position[0] = px;
        position[1] = py;
        position[2] = pz;
        SetColorF(r, g, b, a);
    }

    //! Set color from floats [0,1] to packed RGBA8
    void SetColorF(float r, float g, float b, float a)
    {
        color[0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * 255.0f)));
        color[1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * 255.0f)));
        color[2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * 255.0f)));
        color[3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, a * 255.0f)));
    }
};

static_assert(sizeof(rPackedLineVertex) == 16, "rPackedLineVertex must be exactly 16 bytes");

#endif // RPACKEDVERTEX_H
