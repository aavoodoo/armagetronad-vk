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
#include "rStaticMesh.h"

#ifndef DEDICATED
#include "rRender.h"
#include "rRenderQueue.h"
#include "rRendererState.h"
#include "rVertex.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#endif

rStaticMesh::rStaticMesh()
    : triangleCount_(0)
    , collecting_(false)
    , built_(false)
{
}

rStaticMesh::~rStaticMesh()
{
    Release();
}

void rStaticMesh::Release()
{
    vertices_.clear();
    triangleCount_ = 0;
    built_ = false;
}

void rStaticMesh::Invalidate()
{
    Release();
}

void rStaticMesh::BeginCollection()
{
    vertices_.clear();
    triangleCount_ = 0;
    collecting_ = true;
    built_ = false;
}

void rStaticMesh::AddQuad(const rStaticVertex& v0, const rStaticVertex& v1,
                          const rStaticVertex& v2, const rStaticVertex& v3)
{
    if (!collecting_) return;

    // Quad → 2 triangles (CCW winding)
    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);

    triangleCount_ += 2;
}

void rStaticMesh::AddTriangle(const rStaticVertex& v0, const rStaticVertex& v1,
                              const rStaticVertex& v2)
{
    if (!collecting_) return;

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    triangleCount_++;
}

void rStaticMesh::AddTriangleFan(const rStaticVertex& center,
                                  const std::vector<rStaticVertex>& ring)
{
    if (!collecting_ || ring.size() < 2) return;

    for (size_t i = 0; i < ring.size() - 1; i++)
    {
        vertices_.push_back(center);
        vertices_.push_back(ring[i]);
        vertices_.push_back(ring[i + 1]);
        triangleCount_++;
    }

    vertices_.push_back(center);
    vertices_.push_back(ring.back());
    vertices_.push_back(ring.front());
    triangleCount_++;
}

bool rStaticMesh::EndCollection()
{
#ifndef DEDICATED
    collecting_ = false;
    if (vertices_.empty()) return false;
    triangleCount_ = static_cast<int>(vertices_.size()) / 3;
    built_ = true;
    return true;
#else
    collecting_ = false;
    return false;
#endif
}

void rStaticMesh::Render()
{
#ifndef DEDICATED
    if (!built_ || vertices_.empty()) return;

    // Convert rStaticVertex → rVertex20 and submit to the Vulkan render queue.
    // Normalize texcoords into [-1,1] for int16 packing; compensate via tex matrix.
    float maxTC = 1.0f;
    for (const auto& v : vertices_)
    {
        maxTC = std::max(maxTC, std::abs(v.texcoord[0]));
        maxTC = std::max(maxTC, std::abs(v.texcoord[1]));
    }
    float invScale = 1.0f / maxTC;

    std::vector<rVertex20> verts;
    verts.reserve(vertices_.size());
    for (const auto& v : vertices_)
    {
        rVertex20 rv;
        rv.position[0] = v.position[0];
        rv.position[1] = v.position[1];
        rv.position[2] = v.position[2];
        rv.color[0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v.color[0] * 255.0f)));
        rv.color[1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v.color[1] * 255.0f)));
        rv.color[2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v.color[2] * 255.0f)));
        rv.color[3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v.color[3] * 255.0f)));
        rv.texcoord[0] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, v.texcoord[0] * invScale * 32767.0f)));
        rv.texcoord[1] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, v.texcoord[1] * invScale * 32767.0f)));
        verts.push_back(rv);
    }
    unsigned int texId = RenderGetBoundTexture2D();
    rRenderStateKey state = texId ? rRenderStateKey::Textured(texId, rBlendMode::Opaque)
                                  : rRenderStateKey::Colored(rBlendMode::Opaque);
    if (maxTC > 1.0f)
    {
        float texMatrix[16] = {0};
        texMatrix[0]  = maxTC;
        texMatrix[5]  = maxTC;
        texMatrix[10] = 1.0f;
        texMatrix[15] = 1.0f;
        state.SetTexMatrix(texMatrix);
    }
    state.SetRenderContext(static_cast<int>(sr_GetRenderContext()));
    rRenderQueue::Instance().Submit(rRenderPhase::OpaqueDynamic, state, verts.data(), verts.size());
    // Flush immediately — static mesh vertices are in model space and need
    // the current matrix stack (which includes the cycle transform).
    // Deferring the flush would use the wrong MVP.
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::OpaqueDynamic);
#endif
}
