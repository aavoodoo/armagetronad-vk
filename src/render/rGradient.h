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

#ifndef ArmageTron_rGRADIENT_H
#define ArmageTron_rGRADIENT_H

#include "defs.h"
#include "rColor.h"
#include "tCoord.h"
#include "rTexture.h"
#include "rVertex.h"
#include "utilities/rRenderBucket.h"
#include <map>
#include <deque>
#include <utility>
#include <vector>

//! Gradient class, able to store a gradient and perform basic render functions with it
class rGradient: public std::map<float, rColor> {
    int m_dir; //!< the direction the gardient is laid in
    float m_at; //!< current value, used when m_dir == value
    tCoord m_origin; //!< bottom-left point of the gradient
    tCoord m_dimensions; //!< width and height of it
    tCoord m_texScale; //!< scale factor of the texture

    //! return the relevant value (x, y or m_at) depending on m_dir
    float GetGradientPt(tCoord const &where);
    //! get the color for a given point on the gradient, using only
    //! the relevant coordinate (as returned by GetGradientPt)
    rColor GetColor(float where);

    rResourceTexture m_tex;
    int m_sdfMode = 0;          // 0=normal, 1=SDF, 2=MSDF, 3=MTSDF
    float m_sdfOutlineWidth = 0.0f;
    float m_sdfOutlineR = 0.0f, m_sdfOutlineG = 0.0f, m_sdfOutlineB = 0.0f;
public:
    rGradient(); //!< Constructor
    ~rGradient(); //!< Destructor

    //! Enum for describing the direction of the gradient
    enum direction {
        horizontal, //!< For a gradient going from the left to the right
        vertical, //!< For a gradient going from the bottom to the top
        value //!< For a soild area changing its color based on some other condition
    };
    //! Sets the type/direction of the gradient
    //! @param dir the desired type/direction
    void SetDir(direction dir) { m_dir = dir; }
    //! set the value, only used when the type is "value"
    //! @param at the value, 1 should be the maximum and 0 the minimum
    void SetValue(float at) { m_at = at; }
    //! set the boundaries of the gradient
    void SetGradientEdges(tCoord const &edge1, tCoord const edge2);

    //! Set the texture to be overlaid with the gradient
    void SetTexture(rResourceTexture const &tex) {m_tex = tex;}
    void SetTextureScale(tCoord const &scale) {m_texScale = scale;}

    //! Check if the gradient has any content (colors or texture)
    bool HasContent() { return !empty() || m_tex.Valid() || m_sdfMode > 0; }

    //! SDF rendering mode (0=normal, 1=SDF, 2=MSDF, 3=MTSDF)
    void SetSDFMode(int mode) { m_sdfMode = mode; }
    int GetSDFMode() const { return m_sdfMode; }
    void SetSDFOutline(float width, float outR, float outG, float outB) {
        m_sdfOutlineWidth = width;
        m_sdfOutlineR = outR; m_sdfOutlineG = outG; m_sdfOutlineB = outB;
    }

    //! Generate vertices for a rectangle with gradient colors (batch rendering)
    //! @param edge1 First corner
    //! @param edge2 Opposite corner
    //! @return Vector of vertices ready for batch submission (6 vertices = 2 triangles)
    std::vector<rVertex20> GenerateRectVertices(tCoord const &edge1, tCoord const &edge2);

    //! Generate a single vertex with gradient color (batch rendering)
    //! @param where Position for the vertex
    //! @return Single vertex with color and texture coordinates
    rVertex20 GeneratePointVertex(tCoord const &where);

    //! Create render state key for this gradient (batch rendering)
    //! @param blendMode Blend mode to use (defaults to Alpha)
    //! @return Render state key for rRenderQueue submission
    rRenderStateKey GetRenderStateKey(rBlendMode blendMode = rBlendMode::Alpha);
};

#endif
