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

#include "rRenderBucket.h"

#ifndef DEDICATED
#include "tConsole.h"  // For tERR_WARN macro
#include "rRender.h"   // For sr_PrepareForVBODraw()
#endif

//=============================================================================
// rRenderBucket implementation
//=============================================================================

rRenderBucket::rRenderBucket()
    : uploadedTriangleCount_(0)
    , uploadedLineCount_(0)
{
}

rRenderBucket::rRenderBucket(const rRenderStateKey& state)
    : state_(state)
    , uploadedTriangleCount_(0)
    , uploadedLineCount_(0)
{
}

rRenderBucket::~rRenderBucket()
{
    ReleaseGPU();
}

rRenderBucket::rRenderBucket(rRenderBucket&& other) noexcept
    : state_(other.state_)
    , triangleVertices_(std::move(other.triangleVertices_))
    , lineVertices_(std::move(other.lineVertices_))
    , uploadedTriangleCount_(other.uploadedTriangleCount_)
    , uploadedLineCount_(other.uploadedLineCount_)
{
    other.uploadedTriangleCount_ = 0;
    other.uploadedLineCount_ = 0;
}

rRenderBucket& rRenderBucket::operator=(rRenderBucket&& other) noexcept
{
    if (this != &other)
    {
        ReleaseGPU();

        state_ = other.state_;
        triangleVertices_ = std::move(other.triangleVertices_);
        lineVertices_ = std::move(other.lineVertices_);
        uploadedTriangleCount_ = other.uploadedTriangleCount_;
        uploadedLineCount_ = other.uploadedLineCount_;

        other.uploadedTriangleCount_ = 0;
        other.uploadedLineCount_ = 0;
    }
    return *this;
}

void rRenderBucket::Reserve(size_t triangleVertices, size_t lineVertices)
{
    triangleVertices_.reserve(triangleVertices);
    lineVertices_.reserve(lineVertices);
}

void rRenderBucket::Clear()
{
    triangleVertices_.clear();
    lineVertices_.clear();
    // CRITICAL: Reset upload tracking so new data gets uploaded on next frame
    // Without this, the size check in Upload() would skip uploading new vertex data
    // because the size would be the same (but content different, e.g. alpha values for fading)
    uploadedTriangleCount_ = 0;
    uploadedLineCount_ = 0;
}

void rRenderBucket::AddTriangles(const rVertex20* vertices, size_t count)
{
    triangleVertices_.insert(triangleVertices_.end(), vertices, vertices + count);
}

void rRenderBucket::AddTriangle(const rVertex20& v0, const rVertex20& v1, const rVertex20& v2)
{
    triangleVertices_.push_back(v0);
    triangleVertices_.push_back(v1);
    triangleVertices_.push_back(v2);
}

void rRenderBucket::AddQuad(const rVertex20& v0, const rVertex20& v1, const rVertex20& v2,
                            const rVertex20& v3)
{
    // Triangle 1: v0, v1, v2
    triangleVertices_.push_back(v0);
    triangleVertices_.push_back(v1);
    triangleVertices_.push_back(v2);

    // Triangle 2: v0, v2, v3
    triangleVertices_.push_back(v0);
    triangleVertices_.push_back(v2);
    triangleVertices_.push_back(v3);
}

void rRenderBucket::AddTriangleFan(const rVertex20* vertices, size_t count)
{
    if (count < 3)
        return;

    // Convert fan to triangles: center vertex is vertices[0]
    for (size_t i = 1; i < count - 1; ++i)
    {
        triangleVertices_.push_back(vertices[0]);
        triangleVertices_.push_back(vertices[i]);
        triangleVertices_.push_back(vertices[i + 1]);
    }
}

void rRenderBucket::AddTriangleStrip(const rVertex20* vertices, size_t count)
{
    if (count < 3)
        return;

    // Convert strip to triangles
    for (size_t i = 0; i < count - 2; ++i)
    {
        if (i % 2 == 0)
        {
            // Even: v[i], v[i+1], v[i+2]
            triangleVertices_.push_back(vertices[i]);
            triangleVertices_.push_back(vertices[i + 1]);
            triangleVertices_.push_back(vertices[i + 2]);
        }
        else
        {
            // Odd: v[i+1], v[i], v[i+2] (to maintain winding)
            triangleVertices_.push_back(vertices[i + 1]);
            triangleVertices_.push_back(vertices[i]);
            triangleVertices_.push_back(vertices[i + 2]);
        }
    }
}

void rRenderBucket::AddLines(const rVertex20* vertices, size_t count)
{
    lineVertices_.insert(lineVertices_.end(), vertices, vertices + count);
}

void rRenderBucket::AddLine(const rVertex20& v0, const rVertex20& v1)
{
    lineVertices_.push_back(v0);
    lineVertices_.push_back(v1);
}

void rRenderBucket::AddLineStrip(const rVertex20* vertices, size_t count)
{
    if (count < 2)
        return;

    // Convert strip to individual lines
    for (size_t i = 0; i < count - 1; ++i)
    {
        lineVertices_.push_back(vertices[i]);
        lineVertices_.push_back(vertices[i + 1]);
    }
}

void rRenderBucket::AddLineLoop(const rVertex20* vertices, size_t count)
{
    if (count < 2)
        return;

    // Convert loop to individual lines
    for (size_t i = 0; i < count - 1; ++i)
    {
        lineVertices_.push_back(vertices[i]);
        lineVertices_.push_back(vertices[i + 1]);
    }

    // Close the loop
    lineVertices_.push_back(vertices[count - 1]);
    lineVertices_.push_back(vertices[0]);
}


bool rRenderBucket::Upload()
{
#ifndef DEDICATED
    // Upload is now handled by the renderer's DrawBatch* methods.
    // The bucket just holds CPU-side vertex data.
    return !triangleVertices_.empty() || !lineVertices_.empty();
#else
    return false;
#endif
}

void rRenderBucket::RenderTriangles()
{
#ifndef DEDICATED
    if (!triangleVertices_.empty() && renderer)
    {
        // Set texture matrix on renderer's stack
        if (state_.flags & rRenderStateKey::UseTexMatrix)
        {
            TexMatrix();
            LoadMatrix(state_.texMatrix);
        }
        else
        {
            TexMatrix();
            IdentityMatrix();
        }

        // Draw through backend-agnostic renderer interface
        renderer->DrawBatchTriangles(triangleVertices_.data(), triangleVertices_.size(), &state_);

        ModelMatrix();
    }
#endif
}

void rRenderBucket::RenderLines()
{
#ifndef DEDICATED
    if (!lineVertices_.empty() && renderer)
    {
        if (state_.flags & rRenderStateKey::UseTexMatrix)
        {
            TexMatrix();
            LoadMatrix(state_.texMatrix);
        }
        else
        {
            TexMatrix();
            IdentityMatrix();
        }

        renderer->DrawBatchLines(lineVertices_.data(), lineVertices_.size(), &state_);

        ModelMatrix();
    }
#endif
}

void rRenderBucket::ReleaseGPU()
{
    uploadedTriangleCount_ = 0;
    uploadedLineCount_ = 0;
}
