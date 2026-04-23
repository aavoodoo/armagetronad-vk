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

#ifndef RSTATICMESH_H
#define RSTATICMESH_H

#include "defs.h"
#include <vector>

//! Vertex format for static meshes (arena geometry, floors, walls)
//! Layout: position (3f), texcoord (2f), color (4f) = 36 bytes per vertex
struct rStaticVertex
{
    float position[3];
    float texcoord[2];
    float color[4];

    rStaticVertex()
    {
        position[0] = position[1] = position[2] = 0.0f;
        texcoord[0] = texcoord[1] = 0.0f;
        color[0] = color[1] = color[2] = color[3] = 1.0f;
    }

    rStaticVertex(float px, float py, float pz,
                  float tu, float tv,
                  float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
    {
        position[0] = px;
        position[1] = py;
        position[2] = pz;
        texcoord[0] = tu;
        texcoord[1] = tv;
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }
};

//! Static mesh for arena geometry - accumulates vertices then uploads once
//! Use for rim walls, floors, and other geometry that doesn't change per frame
class rStaticMesh
{
public:
    rStaticMesh();
    ~rStaticMesh();

    //! Check if mesh has been built and is ready for rendering
    bool IsBuilt() const { return built_; }

    //! Check if mesh has been built (no more per-mesh GPU objects under Vulkan).
    bool IsValid() const { return built_; }

    //! Begin collecting vertices
    //! Clears any existing collected data
    void BeginCollection();

    //! Add a quad (4 vertices, will be converted to 2 triangles)
    //! Vertices should be in counter-clockwise order
    void AddQuad(const rStaticVertex& v0, const rStaticVertex& v1,
                 const rStaticVertex& v2, const rStaticVertex& v3);

    //! Add a triangle (3 vertices)
    void AddTriangle(const rStaticVertex& v0, const rStaticVertex& v1,
                     const rStaticVertex& v2);

    //! Add raw vertices for a triangle fan
    //! @param center Center vertex
    //! @param ring Ring vertices (will form triangles with center)
    void AddTriangleFan(const rStaticVertex& center,
                        const std::vector<rStaticVertex>& ring);

    //! End collection and upload to GPU
    //! @return true if successful
    bool EndCollection();

    //! Render the mesh
    void Render();

    //! Release GPU resources
    void Release();

    //! Invalidate mesh (marks it for rebuild)
    void Invalidate();

    //! Get number of triangles
    int GetTriangleCount() const { return triangleCount_; }

    //! Get number of vertices
    int GetVertexCount() const { return static_cast<int>(vertices_.size()); }

private:
    rStaticMesh(const rStaticMesh&) = delete;
    rStaticMesh& operator=(const rStaticMesh&) = delete;

    // Collected CPU-side geometry. Under Vulkan this is the only storage —
    // on render, vertices get converted to rVertex20 and submitted to the
    // render queue. No VBO/VAO/IBO is created.
    std::vector<rStaticVertex> vertices_;
    int triangleCount_;
    bool collecting_;
    bool built_;
};

//! RAII helper for static mesh collection
class rStaticMeshCollector
{
public:
    explicit rStaticMeshCollector(rStaticMesh& mesh)
        : mesh_(mesh)
    {
        mesh_.BeginCollection();
    }

    ~rStaticMeshCollector()
    {
        mesh_.EndCollection();
    }

    void AddQuad(const rStaticVertex& v0, const rStaticVertex& v1,
                 const rStaticVertex& v2, const rStaticVertex& v3)
    {
        mesh_.AddQuad(v0, v1, v2, v3);
    }

    void AddTriangle(const rStaticVertex& v0, const rStaticVertex& v1,
                     const rStaticVertex& v2)
    {
        mesh_.AddTriangle(v0, v1, v2);
    }

private:
    rStaticMesh& mesh_;
};

#endif // RSTATICMESH_H
