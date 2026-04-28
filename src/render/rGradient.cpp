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

#include "rGradient.h"
#include "rRender.h"
#include "tError.h"

#include <deque>
#include <utility>

rGradient::rGradient() : m_dir(value), m_texScale(1,1), m_tex() {
    //(*this)[0.]=rColor(); //make sure the beginning and end are defined
    //(*this)[1.]=rColor();
}
rGradient::~rGradient() {
}

//! @param edge1 the first edge of the gradient (preferably bottom- left)
//! @param edge2 the second edge of the gradient (preferably top- right)
void rGradient::SetGradientEdges(tCoord const &edge1, tCoord const edge2) {
    if(edge1.x < edge2.x) {
        m_origin.x = edge1.x;
        m_dimensions.x = edge2.x - edge1.x;
    } else {
        m_origin.x = edge2.x;
        m_dimensions.x = edge1.x - edge2.x;
    }
    if(edge1.y < edge2.y) {
        m_origin.y = edge1.y;
        m_dimensions.y = edge2.y - edge1.y;
    } else {
        m_origin.y = edge2.y;
        m_dimensions.y = edge1.y - edge2.y;
    }
}

float rGradient::GetGradientPt(tCoord const &where) {
    float ret = 0;
    switch (m_dir) {
    case horizontal:
        ret=(where.x-m_origin.x)/m_dimensions.x;
        break;
    case vertical:
        ret=(where.y-m_origin.y)/m_dimensions.y;
        break;
    case value:
        ret=m_at;
        break;
    default: tASSERT(0);
    }
    if(ret<0.) ret=0.;
    if(ret>1.) ret=1.;
    return ret;
}

rColor rGradient::GetColor(float where) {
#ifndef DEDICATED
    if(empty()) return m_tex.Valid() ? rColor(1,1,1,1) : rColor();
    if(begin()->first >= where) return begin()->second;
    iterator upper, lower;
    iterator i=begin();
    iterator j=begin();
    ++j;
    bool finished = false;
    for(; j!=end(); ++i, ++j) {
        if(j->first >= where) {
            lower = i;
            upper = j;
            finished = true;
            break;
        }
    }
    if (!finished) return rbegin()->second;
    float diff = upper->first - lower->first;
    float pos = where - lower->first;
    rColor &c1 = lower->second;
    rColor &c2 = upper->second;
    float r = c1.r_*(diff-pos)/diff + c2.r_*pos/diff;
    float g = c1.g_*(diff-pos)/diff + c2.g_*pos/diff;
    float b = c1.b_*(diff-pos)/diff + c2.b_*pos/diff;
    float a = c1.a_*(diff-pos)/diff + c2.a_*pos/diff;
    return rColor(r,g,b,a);
#else
    return rColor(0.,0.,0.,0.);
#endif
}

//! Generate vertices for a rectangle with gradient colors (batch rendering)
//! @param edge1 First corner
//! @param edge2 Opposite corner
//! @return Vector of vertices ready for batch submission (6 vertices = 2 triangles)
std::vector<rVertex20> rGradient::GenerateRectVertices(tCoord const &edge1, tCoord const &edge2) {
    std::vector<rVertex20> vertices;
#ifndef DEDICATED
    vertices.reserve(24);  // Reserve for worst case: multiple sub-rectangles

    float tCoord::*x; //those are correct for horizontal gradients,
    float tCoord::*y; //vertical ones just get turned around

    switch(m_dir) {
    case horizontal:
        x = &tCoord::x;
        y = &tCoord::y;
        break;
    case vertical:
        x = &tCoord::y;
        y = &tCoord::x;
        break;
    default:
        // For value mode, generate simple quad (2 triangles = 6 vertices)
        {
            tCoord v1 = edge1;
            tCoord v2(edge1.x, edge2.y);
            tCoord v3 = edge2;
            tCoord v4(edge2.x, edge1.y);

            // Generate vertices for the quad corners
            rVertex20 rv1 = GeneratePointVertex(v1);
            rVertex20 rv2 = GeneratePointVertex(v2);
            rVertex20 rv3 = GeneratePointVertex(v3);
            rVertex20 rv4 = GeneratePointVertex(v4);

            // First triangle: v1, v2, v3
            vertices.push_back(rv1);
            vertices.push_back(rv2);
            vertices.push_back(rv3);

            // Second triangle: v1, v3, v4
            vertices.push_back(rv1);
            vertices.push_back(rv3);
            vertices.push_back(rv4);
        }
        return vertices;
    }

    tCoord const &left = (edge1.*x < edge2.*x) ? edge1 : edge2;
    tCoord const &right = (edge1.*x < edge2.*x) ? edge2 : edge1;
    float min = GetGradientPt(left);
    float max = GetGradientPt(right);
    iterator i = upper_bound(min);
    float last = left.*x;

    // Generate sub-rectangles at gradient transition points
    for(; i != end() && i->first < max; ++i) {
        float newpt=i->first - min;
        float newx=newpt*m_dimensions.*x+m_origin.*x;
        tCoord todraw1;
        tCoord todraw2;
        todraw1.*x = last;
        todraw2.*x = newx;
        todraw1.*y = left.*y;
        todraw2.*y = right.*y;

        // Generate atomic rect (2 triangles)
        rVertex20 rv1 = GeneratePointVertex(todraw1);
        rVertex20 rv2 = GeneratePointVertex(tCoord(todraw1.x, todraw2.y));
        rVertex20 rv3 = GeneratePointVertex(todraw2);
        rVertex20 rv4 = GeneratePointVertex(tCoord(todraw2.x, todraw1.y));

        vertices.push_back(rv1);
        vertices.push_back(rv2);
        vertices.push_back(rv3);
        vertices.push_back(rv1);
        vertices.push_back(rv3);
        vertices.push_back(rv4);

        last = newx;
    }

    // Final sub-rectangle
    tCoord todraw;
    todraw.*x = last;
    todraw.*y = left.*y;

    rVertex20 rv1 = GeneratePointVertex(todraw);
    rVertex20 rv2 = GeneratePointVertex(tCoord(todraw.x, right.*y));
    rVertex20 rv3 = GeneratePointVertex(right);
    rVertex20 rv4 = GeneratePointVertex(tCoord(right.*x, todraw.*y));

    vertices.push_back(rv1);
    vertices.push_back(rv2);
    vertices.push_back(rv3);
    vertices.push_back(rv1);
    vertices.push_back(rv3);
    vertices.push_back(rv4);
#endif
    return vertices;
}

//! Generate a single vertex with gradient color (batch rendering)
//! @param where Position for the vertex
//! @return Single vertex with color and texture coordinates
rVertex20 rGradient::GeneratePointVertex(tCoord const &where) {
    rVertex20 vertex;
#ifndef DEDICATED
    // Get color at this point
    rColor c = GetColor(GetGradientPt(where));

    // Set vertex position (Z = 0 for 2D)
    vertex.SetPosition(where.x, where.y, 0.0f);

    // Set vertex color (convert from float 0-1 to byte 0-255)
    vertex.SetColorF(c.r_, c.g_, c.b_, c.a_);

    // Set texture coordinates if texture is valid
    // Store normalized UVs (0-1 range, fits int16). texScale is handled by the
    // texture matrix in GetRenderStateKey to avoid int16 overflow for tiled textures.
    if(m_tex.Valid()) {
        float u = (where.x - m_origin.x) / m_dimensions.x;
        float v = (m_origin.y - where.y) / m_dimensions.y;
        vertex.SetTexCoord(u, v);
    } else {
        vertex.SetTexCoord(0.0f, 0.0f);
    }
#endif
    return vertex;
}

//! Create render state key for this gradient (batch rendering)
//! @param blendMode Blend mode to use (defaults to Alpha)
//! @return Render state key for rRenderQueue submission
rRenderStateKey rGradient::GetRenderStateKey(rBlendMode blendMode) {
    rRenderStateKey key;
#ifndef DEDICATED
    if(m_tex.Valid()) {
        // Select texture to bind it
        m_tex.Select();

        // Query the bound texture ID
        unsigned int textureId = RenderGetBoundTexture2D();

        if (m_sdfMode > 0) {
            // SDF/MSDF/MTSDF rendering: use SDFTextured state key
            // screenPxRange = spread * (outputPixels / texturePixels).
            // For 256px textures with spread=24 at typical HUD sizes (~150px),
            // this gives ~14. Higher = sharper edges.
            float screenPxRange = 14.0f;
            key = rRenderStateKey::SDFTextured(
                textureId,
                false,             // useTexture=false: inside color from push constants
                screenPxRange,
                m_sdfOutlineWidth,
                m_sdfOutlineR, m_sdfOutlineG, m_sdfOutlineB,
                1.0f, 1.0f, 1.0f, 1.0f,  // inside color: white (modulated by vColor)
                false,             // invert
                m_texScale.x != 1.0f ? 1.0f / m_texScale.x : 0.0f,
                m_texScale.y != 1.0f ? 1.0f / m_texScale.y : 0.0f
            );
        } else {
            // Normal textured rendering
            key = rRenderStateKey::Textured(textureId, blendMode);

            // Apply texture scale via texture matrix (avoids int16 UV overflow)
            if (m_texScale.x != 1.0f || m_texScale.y != 1.0f) {
                float texMatrix[16] = {0};
                texMatrix[0]  = 1.0f / m_texScale.x;
                texMatrix[5]  = 1.0f / m_texScale.y;
                texMatrix[10] = 1.0f;
                texMatrix[15] = 1.0f;
                key.SetTexMatrix(texMatrix);
            }
        }
    } else {
        // Untextured gradient (vertex color only)
        key.textureId = 0;
        key.blendMode = blendMode;
        key.flags = rRenderStateKey::UseVertexColor;
    }
#endif
    return key;
}
