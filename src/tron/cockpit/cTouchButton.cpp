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

#include "cockpit/cTouchButton.h"

#ifndef DEDICATED

#include "rRenderQueue.h"
#include "rVertex.h"
#include "rFont.h"
#include "uInput.h"

namespace cWidget {

TouchButton::TouchButton()
    : WithCaption()
{
}

bool TouchButton::Process(tXmlParser::node cur)
{
    // Handle root <TouchButton action="" player=""> attributes
    if (cur.IsOfType("TouchButton")) {
        actionName_ = cur.GetProp("action");
        cur.GetProp("player", player_);
        return true;
    }
    if (WithCoordinates::Process(cur) || WithCaption::Process(cur))
        return true;
    DisplayError(cur);
    return false;
}

void TouchButton::ResolveAction()
{
    if (!action_ && !actionName_.empty())
        action_ = uAction::Find(actionName_.c_str());
}

bool TouchButton::HitTest(float hx, float hy) const
{
    return hx >= m_position.x - m_size.x && hx <= m_position.x + m_size.x &&
           hy >= m_position.y - m_size.y && hy <= m_position.y + m_size.y;
}

void TouchButton::Activate(bool on)
{
    pressed_ = on;
    ResolveAction();
    if (!action_) return;
    uPlayerPrototype* pc = uPlayerPrototype::PlayerConfig(player_ - 1);
    if (pc) pc->Act(action_, on ? 1.0f : 0.0f);
}

void TouchButton::Render()
{
    if (su_GetEnableTouch() != 3) return;

    // Semi-transparent blue-ish quad: dim idle (alpha ~64), bright when pressed (alpha ~153)
    const uint8_t alpha = pressed_ ? 153 : 64;
    const uint8_t r = 80, g = 100, b = 220;

    const float x0 = m_position.x - m_size.x;
    const float x1 = m_position.x + m_size.x;
    const float y0 = m_position.y - m_size.y;
    const float y1 = m_position.y + m_size.y;

    // Build quad (top-left, top-right, bottom-right, bottom-left) in [-1,1] HUD space
    rVertex20 verts[4] = {
        rVertex20(x0, y1, 0, r, g, b, alpha, 0, 0),  // top-left
        rVertex20(x1, y1, 0, r, g, b, alpha, 0, 0),  // top-right
        rVertex20(x1, y0, 0, r, g, b, alpha, 0, 0),  // bottom-right
        rVertex20(x0, y0, 0, r, g, b, alpha, 0, 0),  // bottom-left
    };

    rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
    rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state,
                                        verts[0], verts[1], verts[2], verts[3]);

    // Render caption text centered in button
    if (!m_caption.empty()) {
        const float textH = m_size.y * 0.7f;
        const float textW = rTextField::GetTextLength(m_caption, textH, true);
        rTextField tf(m_position.x - textW * 0.5f, m_position.y + textH * 0.5f,
                      textH, sr_fontCockpit);
        tf << m_caption;
    }
}

} // namespace cWidget

#endif // !DEDICATED
