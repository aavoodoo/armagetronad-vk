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
    // Handle root <TouchButton action="" player="" touchMode="1,3"> attributes
    if (cur.IsOfType("TouchButton")) {
        actionName_ = cur.GetProp("action");
        cur.GetProp("player", player_);

        // Parse touchMode: comma-separated list of mode numbers, or "all"
        if (cur.HasProp("touchMode")) {
            tString modes = cur.GetProp("touchMode");
            if (modes == "all") {
                touchModeMask_ = 0x0E; // modes 1,2,3
            } else {
                touchModeMask_ = 0;
                for (int i = 0; i < modes.Len(); i++) {
                    char c = modes[i];
                    if (c >= '1' && c <= '3')
                        touchModeMask_ |= (1 << (c - '0'));
                }
                if (touchModeMask_ == 0) touchModeMask_ = 0x08; // fallback to mode 3
            }
        }
        return true;
    }
    if (WithCoordinates::Process(cur) || WithCaption::Process(cur) || WithBackground::Process(cur))
        return true;
    DisplayError(cur);
    return false;
}

bool TouchButton::IsActiveInCurrentMode() const
{
    int mode = su_GetEnableTouch();
    if (mode < 1 || mode > 3) return false;
    return (touchModeMask_ & (1 << mode)) != 0;
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
    if (!IsActiveInCurrentMode()) return;

    const float x0 = m_position.x - m_size.x;
    const float x1 = m_position.x + m_size.x;
    const float y0 = m_position.y - m_size.y;
    const float y1 = m_position.y + m_size.y;

    const uint8_t alpha = pressed_ ? 200 : 128;

    if (m_background.HasContent())
    {
        // Use Background gradient/texture from XML (supports SDF via Graphic sdf= attribute)
        m_background.SetGradientEdges(tCoord(x0, y0), tCoord(x1, y1));
        auto verts = m_background.GenerateRectVertices(tCoord(x0, y0), tCoord(x1, y1));
        // Apply press alpha modulation
        for (auto& v : verts)
        {
            uint8_t va = v.color[3];
            v.color[3] = static_cast<uint8_t>(va * alpha / 255);
        }
        rRenderStateKey state = m_background.GetRenderStateKey(rBlendMode::Alpha);
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, verts.data(), verts.size());
    }
    else
    {
        // Default: semi-transparent blue quad
        const uint8_t r = 80, g = 100, b = 220;
        rVertex20 v0(x0, y1, 0, r, g, b, alpha, 0, 0);
        rVertex20 v1(x1, y1, 0, r, g, b, alpha, 0, 0);
        rVertex20 v2(x1, y0, 0, r, g, b, alpha, 0, 0);
        rVertex20 v3(x0, y0, 0, r, g, b, alpha, 0, 0);
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);
    }

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
