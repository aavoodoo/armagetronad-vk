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

#include "cockpit/cRectangle.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rScreen.h"
#include "rVertex.h"
#include "rRenderQueue.h"

namespace cWidget {

bool Rectangle::Process(tXmlParser::node cur) {
    if (
        WithSingleData  ::Process(cur) ||
        WithBackground  ::Process(cur) ||
        WithForeground  ::Process(cur) ||
        WithCoordinates ::Process(cur))
        return true;
    else {
        DisplayError(cur);
        return false;
    }
}

void Rectangle::Render() {
    float val=m_data.GetVal().GetFloat();
    float min=m_data.GetMin().GetFloat();
    float max=m_data.GetMax().GetFloat();
    float where = (val-min)/(max-min);

    const tCoord edge1(tCoord(m_position.x-m_size.x, m_position.y+m_size.y));
    const tCoord edge2(tCoord(m_position.x+m_size.x, m_position.y-m_size.y));

    m_background.SetGradientEdges(edge1, edge2);
    m_background.SetValue(where);

    std::vector<rVertex20> verts = m_background.GenerateRectVertices(edge1, edge2);
    rRenderStateKey state = m_background.GetRenderStateKey(rBlendMode::Alpha);
    rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, verts.data(), verts.size());
}

}

#endif
