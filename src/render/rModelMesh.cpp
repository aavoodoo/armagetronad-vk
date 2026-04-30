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
#include "rModelMesh.h"

#ifndef DEDICATED
#include "rRender.h"
#endif

uint64_t rModelMesh::s_nextMeshId_ = 1;

rModelMesh::rModelMesh()
    : meshId_(s_nextMeshId_++)
    , vertexCount_(0)
    , indexCount_(0)
    , triangleCount_(0)
    , valid_(false)
    , useIndices_(false)
{
}

rModelMesh::~rModelMesh()
{
    Release();
}

void rModelMesh::Release()
{
    cpuVertices_.clear();
    cpuIndices_.clear();
    vertexCount_ = 0;
    indexCount_ = 0;
    triangleCount_ = 0;
    valid_ = false;
    useIndices_ = false;
}

bool rModelMesh::Build(const std::vector<rModelVertex>& vertices,
                       const std::vector<unsigned int>& indices)
{
#ifndef DEDICATED
    meshId_ = s_nextMeshId_++;  // new geometry = new identity
    cpuVertices_ = vertices;
    cpuIndices_ = indices;
    vertexCount_ = static_cast<int>(vertices.size());
    indexCount_ = static_cast<int>(indices.size());
    triangleCount_ = indexCount_ / 3;
    useIndices_ = true;
    valid_ = !vertices.empty() && !indices.empty();
    return valid_;
#else
    return false;
#endif
}

bool rModelMesh::Build(const std::vector<rModelVertex>& vertices)
{
#ifndef DEDICATED
    meshId_ = s_nextMeshId_++;  // new geometry = new identity
    cpuVertices_ = vertices;
    cpuIndices_.clear();
    vertexCount_ = static_cast<int>(vertices.size());
    indexCount_ = 0;
    triangleCount_ = vertexCount_ / 3;
    useIndices_ = false;
    valid_ = !vertices.empty();
    return valid_;
#else
    return false;
#endif
}

void rModelMesh::Render()
{
#ifndef DEDICATED
    if (!valid_ || cpuVertices_.empty()) return;
    unsigned int texId = RenderGetBoundTexture2D();
    renderer->DrawModelMesh(meshId_, cpuVertices_, cpuIndices_, texId);
#endif
}
