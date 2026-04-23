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

#ifndef RRENDERBUCKET_H
#define RRENDERBUCKET_H

#include "rVertex.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

//! @file rRenderBucket.h
//! Geometry batching system for the modern GL3 renderer.
//!
//! A render bucket collects geometry with identical render state (texture, blend mode,
//! shader flags) into a single draw call. This minimizes state changes and draw calls.

//=============================================================================
// Blend mode encoding
//=============================================================================

//! Encoded blend mode (src << 8 | dst)
enum class rBlendMode : uint16_t
{
    Opaque = 0x0000,                 //!< No blending (GL_ONE, GL_ZERO equivalent)
    Alpha = 0x0001,                  //!< Standard alpha (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    Additive = 0x0002,               //!< Additive (GL_SRC_ALPHA, GL_ONE)
    Multiply = 0x0003,               //!< Multiply (GL_DST_COLOR, GL_ZERO)
    PremultipliedAlpha = 0x0004,     //!< Premultiplied (GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
};

//=============================================================================
// Render state key
//=============================================================================

//! Render state key - determines which bucket geometry goes into.
//! Geometry with the same key can be batched into a single draw call.
struct rRenderStateKey
{
    uint64_t textureId;     //!< OpenGL texture ID (0 = no texture)
    rBlendMode blendMode;   //!< Blend mode
    uint8_t flags;          //!< Feature flags (see below)
    uint8_t padding;        //!< Padding for alignment
    float texMatrix[16];    //!< Texture matrix (column-major, used when UseTexMatrix is set)

    //! Feature flag bits
    enum Flags : uint8_t
    {
        UseTexture = 1 << 0,      //!< Sample texture in shader
        UseLighting = 1 << 1,     //!< Apply lighting calculations
        UseVertexColor = 1 << 2,  //!< Use vertex color attribute
        AlphaTest = 1 << 3,       //!< Enable alpha test/discard
        DepthWrite = 1 << 4,      //!< Write to depth buffer
        DepthTest = 1 << 5,       //!< Test against depth buffer
        UseTexMatrix = 1 << 6,    //!< Apply texture matrix transformation
        FontSDF = 1 << 7,         //!< SDF/MSDF/MTSDF font geometry; texMatrix[0]=screenPxRange, texMatrix[15]=-fontMode
    };

    //! Default constructor - opaque, no texture, vertex color enabled
    rRenderStateKey()
        : textureId(0)
        , blendMode(rBlendMode::Opaque)
        , flags(UseVertexColor | DepthWrite | DepthTest)
        , padding(0)
        , texMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}  // Identity matrix
    {
    }

    //! Create key for textured geometry
    static rRenderStateKey Textured(uint64_t texId, rBlendMode blend = rBlendMode::Alpha)
    {
        rRenderStateKey key;
        key.textureId = texId;
        key.blendMode = blend;
        key.flags = UseTexture | UseVertexColor | DepthWrite | DepthTest;
        return key;
    }

    //! Create key for colored (non-textured) geometry
    static rRenderStateKey Colored(rBlendMode blend = rBlendMode::Opaque)
    {
        rRenderStateKey key;
        key.textureId = 0;
        key.blendMode = blend;
        key.flags = UseVertexColor | DepthWrite | DepthTest;
        return key;
    }

    //! Create key for lit geometry
    static rRenderStateKey Lit(uint64_t texId = 0)
    {
        rRenderStateKey key;
        key.textureId = texId;
        key.blendMode = rBlendMode::Opaque;
        key.flags = UseLighting | DepthWrite | DepthTest;
        if (texId != 0)
        {
            key.flags |= UseTexture;
        }
        return key;
    }

    //! Create key for 2D HUD elements (no depth)
    static rRenderStateKey HUD(uint64_t texId = 0, rBlendMode blend = rBlendMode::Alpha)
    {
        rRenderStateKey key;
        key.textureId = texId;
        key.blendMode = blend;
        // UseTexMatrix forces identity texMatrix from the state key, preventing
        // stale floor/scene texture transforms from corrupting HUD UV mapping
        // and the lighting flag (texMatrix[2][3]) inside viewport FBO rendering.
        key.flags = UseVertexColor | UseTexMatrix;
        if (texId != 0)
        {
            key.flags |= UseTexture;
        }
        return key;
    }

    //! Create key for 2D HUD elements with custom texture matrix
    static rRenderStateKey HUDWithTexMatrix(uint64_t texId, rBlendMode blend, const float* matrix)
    {
        rRenderStateKey key;
        key.textureId = texId;
        key.blendMode = blend;
        key.flags = UseVertexColor | UseTexture | UseTexMatrix;
        for (int i = 0; i < 16; ++i)
        {
            key.texMatrix[i] = std::round(matrix[i] * 1000.0f) / 1000.0f;
        }
        return key;
    }

    //! Create key for SDF outline rendering on game surfaces.
    //! useTexture: false = inside color from parameters (single-channel SDF in R),
    //!             true  = inside color from texture RGB × vColor (SDF in alpha channel).
    //! outlineWidth: 0.0-0.5 in SDF units (fraction of distance field range).
    //! invert: true = invert SDF (for B&W images where black=inside shape).
    static rRenderStateKey SDFTextured(uint64_t sdfTexId, bool useTexture,
        float screenPxRange, float outlineWidth,
        float outR, float outG, float outB,
        float insR = 1.0f, float insG = 1.0f, float insB = 1.0f, float insA = 1.0f,
        bool invert = false, float uvScaleU = 0.0f, float uvScaleV = 0.0f)
    {
        rRenderStateKey key;
        key.textureId = sdfTexId;
        key.blendMode = rBlendMode::Alpha;
        key.flags = static_cast<uint8_t>(UseVertexColor | UseTexture | FontSDF | DepthWrite | DepthTest);
        for (int i = 0; i < 16; ++i) key.texMatrix[i] = 0.0f;
        key.texMatrix[0]  = screenPxRange;                  // [0][0]
        key.texMatrix[1]  = outlineWidth;                   // [0][1]
        key.texMatrix[2]  = insR;                           // [0][2]
        key.texMatrix[3]  = insG;                           // [0][3]
        key.texMatrix[4]  = insB;                           // [1][0]
        key.texMatrix[5]  = insA;                           // [1][1]
        key.texMatrix[6]  = outR;                           // [1][2]
        key.texMatrix[7]  = outG;                           // [1][3]
        key.texMatrix[8]  = outB;                           // [2][0]
        key.texMatrix[9]  = useTexture ? 1.0f : 0.0f;      // [2][1] useTexture flag
        key.texMatrix[10] = invert ? 1.0f : 0.0f;          // [2][2] invert flag
        key.texMatrix[12] = uvScaleU;                       // [3][0] UV scale U
        key.texMatrix[13] = uvScaleV;                       // [3][1] UV scale V
        key.texMatrix[15] = -4.0f;                          // sentinel: fontMode 4
        return key;
    }

    //! Create key for SDF/MSDF/MTSDF font geometry.
    //! fontMode: 1=SDF, 2=MSDF, 3=MTSDF. The Vulkan renderer interprets these for distance-field
    //! rendering; the GL3 renderer falls back to plain textured rendering (shows raw atlas).
    //! screenPxRange: range * (fontSize/glyphSize), used for anti-aliasing width.
    static rRenderStateKey HUDFont(uint64_t texId, uint8_t fontMode, float screenPxRange)
    {
        rRenderStateKey key;
        key.textureId = texId;
        key.blendMode = rBlendMode::Alpha;
        key.flags = static_cast<uint8_t>(UseVertexColor | UseTexture | FontSDF);
        // Encode SDF params into texMatrix (not used as a real matrix for this key type).
        // texMatrix[0] = screenPxRange; texMatrix[15] = -fontMode (negative = SDF sentinel).
        for (int i = 0; i < 16; ++i) key.texMatrix[i] = 0.0f;
        key.texMatrix[0] = screenPxRange;
        key.texMatrix[15] = -static_cast<float>(fontMode);
        return key;
    }

    //! Set texture matrix (4x4 column-major) and enable UseTexMatrix flag.
    //! Values are quantized to 3 decimal places so that minor floating-point
    //! differences don't split geometry into separate buckets.
    void SetTexMatrix(const float* matrix)
    {
        for (int i = 0; i < 16; ++i)
        {
            texMatrix[i] = std::round(matrix[i] * 1000.0f) / 1000.0f;
        }
        flags |= UseTexMatrix;
    }

    //! Embed render context ID into the state key's texMatrix[9].
    //! This allows batched draws to carry their render context without
    //! relying on a global variable at flush time.
    void SetRenderContext(int contextId)
    {
        texMatrix[9] = static_cast<float>(contextId);
    }

    //! Get the embedded render context (0 = not set, use global fallback)
    float GetRenderContext() const { return texMatrix[9]; }

    //! Clear texture matrix and disable UseTexMatrix flag
    void ClearTexMatrix()
    {
        // Reset to identity
        texMatrix[0] = texMatrix[5] = texMatrix[10] = texMatrix[15] = 1.0f;
        texMatrix[1] = texMatrix[2] = texMatrix[3] = texMatrix[4] = 0.0f;
        texMatrix[6] = texMatrix[7] = texMatrix[8] = texMatrix[9] = 0.0f;
        texMatrix[11] = texMatrix[12] = texMatrix[13] = texMatrix[14] = 0.0f;
        flags &= ~UseTexMatrix;
    }

    //! Equality comparison
    bool operator==(const rRenderStateKey& other) const
    {
        if (textureId != other.textureId || blendMode != other.blendMode ||
            flags != other.flags)
        {
            return false;
        }

        // Compare texture matrix if UseTexMatrix is set
        if (flags & UseTexMatrix)
        {
            for (int i = 0; i < 16; ++i)
            {
                if (texMatrix[i] != other.texMatrix[i])
                {
                    return false;
                }
            }
        }

        // Compare all SDF params when FontSDF is set (mode 4 uses slots [0]-[10], [15])
        if ((flags & FontSDF) && !(flags & UseTexMatrix))
        {
            for (int i = 0; i < 16; ++i)
            {
                if (texMatrix[i] != other.texMatrix[i])
                    return false;
            }
        }

        return true;
    }

    bool operator!=(const rRenderStateKey& other) const { return !(*this == other); }

    //! Hash function for use in unordered containers
    size_t Hash() const
    {
        // FNV-1a hash
        size_t hash = 14695981039346656037ULL;
        hash ^= textureId;
        hash *= 1099511628211ULL;
        hash ^= static_cast<uint16_t>(blendMode);
        hash *= 1099511628211ULL;
        hash ^= flags;
        hash *= 1099511628211ULL;

        // Hash texture matrix if UseTexMatrix is set
        if (flags & UseTexMatrix)
        {
            for (int i = 0; i < 16; ++i)
            {
                // Hash float bits as uint32_t
                uint32_t bits;
                memcpy(&bits, &texMatrix[i], sizeof(uint32_t));
                hash ^= bits;
                hash *= 1099511628211ULL;
            }
        }

        // Hash all SDF params when FontSDF is set (mode 4 uses slots [0]-[10], [15])
        if ((flags & FontSDF) && !(flags & UseTexMatrix))
        {
            for (int i = 0; i < 16; ++i)
            {
                uint32_t bits;
                memcpy(&bits, &texMatrix[i], sizeof(uint32_t));
                hash ^= bits;
                hash *= 1099511628211ULL;
            }
        }

        return hash;
    }

    //! Comparison for sorting (opaque before transparent, by texture)
    bool operator<(const rRenderStateKey& other) const
    {
        // Sort by blend mode first (opaque renders first)
        if (blendMode != other.blendMode)
        {
            return blendMode < other.blendMode;
        }
        // Then by texture (minimize texture switches)
        if (textureId != other.textureId)
        {
            return textureId < other.textureId;
        }
        // Then by flags
        if (flags != other.flags)
        {
            return flags < other.flags;
        }
        // Finally by texture matrix if UseTexMatrix is set
        if (flags & UseTexMatrix)
        {
            for (int i = 0; i < 16; ++i)
            {
                if (texMatrix[i] != other.texMatrix[i])
                {
                    return texMatrix[i] < other.texMatrix[i];
                }
            }
        }
        return false;
    }
};

//! Hash functor for std::unordered_map
struct rRenderStateKeyHash
{
    size_t operator()(const rRenderStateKey& key) const { return key.Hash(); }
};

//=============================================================================
// Render bucket
//=============================================================================

//! A bucket that collects geometry with identical render state.
//! All geometry in a bucket can be drawn with a single draw call.
class rRenderBucket
{
public:
    rRenderBucket();
    explicit rRenderBucket(const rRenderStateKey& state);
    ~rRenderBucket();

    // Non-copyable, movable
    rRenderBucket(const rRenderBucket&) = delete;
    rRenderBucket& operator=(const rRenderBucket&) = delete;
    rRenderBucket(rRenderBucket&& other) noexcept;
    rRenderBucket& operator=(rRenderBucket&& other) noexcept;

    //! Get the render state key for this bucket
    const rRenderStateKey& GetState() const { return state_; }

    //! Set the render state key
    void SetState(const rRenderStateKey& state) { state_ = state; }

    //! Reserve capacity for expected vertex count
    void Reserve(size_t triangleVertices, size_t lineVertices = 0);

    //! Clear all geometry (keeps capacity)
    void Clear();

    //! Add pre-triangulated geometry (3 vertices per triangle)
    void AddTriangles(const rVertex20* vertices, size_t count);

    //! Add a single triangle
    void AddTriangle(const rVertex20& v0, const rVertex20& v1, const rVertex20& v2);

    //! Add a quad as two triangles (v0-v1-v2, v0-v2-v3)
    void AddQuad(const rVertex20& v0, const rVertex20& v1, const rVertex20& v2,
                 const rVertex20& v3);

    //! Add a triangle fan as triangles
    void AddTriangleFan(const rVertex20* vertices, size_t count);

    //! Add a triangle strip as triangles
    void AddTriangleStrip(const rVertex20* vertices, size_t count);

    //! Add line segments (2 vertices per line)
    void AddLines(const rVertex20* vertices, size_t count);

    //! Add a single line
    void AddLine(const rVertex20& v0, const rVertex20& v1);

    //! Add a line strip as individual lines
    void AddLineStrip(const rVertex20* vertices, size_t count);

    //! Add a line loop as individual lines
    void AddLineLoop(const rVertex20* vertices, size_t count);

    //! Get triangle vertex data
    const std::vector<rVertex20>& GetTriangleVertices() const { return triangleVertices_; }

    //! Get line vertex data
    const std::vector<rVertex20>& GetLineVertices() const { return lineVertices_; }

    //! Get counts
    size_t GetTriangleVertexCount() const { return triangleVertices_.size(); }
    size_t GetLineVertexCount() const { return lineVertices_.size(); }
    size_t GetTriangleCount() const { return triangleVertices_.size() / 3; }
    size_t GetLineCount() const { return lineVertices_.size() / 2; }

    //! Check if bucket is empty
    bool IsEmpty() const { return triangleVertices_.empty() && lineVertices_.empty(); }

    //! Get approximate memory usage
    size_t GetMemoryUsage() const
    {
        return triangleVertices_.capacity() * sizeof(rVertex20) +
               lineVertices_.capacity() * sizeof(rVertex20);
    }

    //! Upload geometry to GPU (creates/updates VBO)
    //! Returns false if nothing to upload
    bool Upload();

    //! Render triangles (assumes shader and state already set)
    void RenderTriangles();

    //! Render lines (assumes shader and state already set)
    void RenderLines();

    //! Release GPU resources
    void ReleaseGPU();

private:
    rRenderStateKey state_;

    // CPU-side geometry
    std::vector<rVertex20> triangleVertices_;
    std::vector<rVertex20> lineVertices_;

    // Upload tracking
    size_t uploadedTriangleCount_;
    size_t uploadedLineCount_;
};

#endif // RRENDERBUCKET_H
