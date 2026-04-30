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

#ifndef RMODELMESH_H
#define RMODELMESH_H

#include "defs.h"
#include <vector>
#include <cstdint>

//! Interleaved vertex format for model meshes
//! Layout: position (3f), normal (3f), texcoord (3f) = 36 bytes per vertex
struct rModelVertex
{
    float position[3];
    float normal[3];
    float texcoord[3];

    rModelVertex()
    {
        position[0] = position[1] = position[2] = 0.0f;
        normal[0] = normal[1] = normal[2] = 0.0f;
        texcoord[0] = texcoord[1] = texcoord[2] = 0.0f;
    }

    rModelVertex(float px, float py, float pz,
                 float nx, float ny, float nz,
                 float tu, float tv, float tw = 0.0f)
    {
        position[0] = px;
        position[1] = py;
        position[2] = pz;
        normal[0] = nx;
        normal[1] = ny;
        normal[2] = nz;
        texcoord[0] = tu;
        texcoord[1] = tv;
        texcoord[2] = tw;
    }
};

//! GPU mesh for rModel - holds VBO and VAO for rendering
//! This replaces display list caching with modern VBO-based rendering
class rModelMesh
{
public:
    rModelMesh();
    ~rModelMesh();

    //! Check if mesh is valid and ready for rendering
    bool IsValid() const { return valid_; }

    //! Stable mesh identity for renderer cache keying.
    //! Assigned from a monotonic counter at construction and on every Build()
    //! call, so each distinct geometry gets a unique ID even if the object is
    //! reused at the same address after destruction. Never 0.
    uint64_t GetMeshId() const { return meshId_; }

    //! Build mesh from vertex and index data
    //! @param vertices Interleaved vertex data
    //! @param indices Triangle indices
    //! @return true if successful
    bool Build(const std::vector<rModelVertex>& vertices,
               const std::vector<unsigned int>& indices);

    //! Build mesh from vertex data only (no indices)
    //! @param vertices Interleaved vertex data
    //! @return true if successful
    bool Build(const std::vector<rModelVertex>& vertices);

    //! Render the mesh
    void Render();

    //! Release GPU resources
    void Release();

    //! Get number of triangles
    int GetTriangleCount() const { return triangleCount_; }

    //! Get number of vertices
    int GetVertexCount() const { return vertexCount_; }

private:
    rModelMesh(const rModelMesh&) = delete;
    rModelMesh& operator=(const rModelMesh&) = delete;

    uint64_t meshId_;   //!< monotonic stable ID; re-assigned on every Build()
    static uint64_t s_nextMeshId_;  //!< global counter, starts at 1

    int vertexCount_;
    int indexCount_;
    int triangleCount_;
    bool valid_;
    bool useIndices_;

    // CPU-side copy used by the Vulkan renderer — this is the only storage
    // now that the GL VBO/VAO path is gone. The renderer consumes these
    // vectors through rVulkanRenderQueue at draw time.
    std::vector<rModelVertex> cpuVertices_;
    std::vector<unsigned int> cpuIndices_;

public:
    //! Get CPU vertices (for instanced rendering cache key)
    const std::vector<rModelVertex>& GetVertices() const { return cpuVertices_; }
};

#endif // RMODELMESH_H
