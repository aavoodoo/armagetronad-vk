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

#include "eDebugLine.h"
#include "tArray.h"
#include "rRender.h"
#include "rVertex.h"
#ifndef DEDICATED
#include "rRenderQueue.h"
#endif

#ifdef DEBUG
#define DEBUGLINE
#endif


static REAL se_r=1, se_g=1, se_b=1;
static REAL se_timeout=.1;

class eLineEntry
{
public:
    int    index;
    REAL   r, g, b;
    REAL   timeout;
    eCoord start , stop;
    REAL   startH, stopH;

    eLineEntry()
            :r(se_r), g(se_g), b(se_b),
            timeout(se_timeout)
    {
    }

    void Delete();
    static eLineEntry& Create();
};

static tArray<eLineEntry> se_lines;

void eLineEntry::Delete()
{
    int i = index;
    *this = se_lines(se_lines.Len()-1);
    index = i;

    if (se_lines.Len() > 0)
        se_lines.SetLen(se_lines.Len()-1);
}

eLineEntry& eLineEntry::Create()
{
    eLineEntry& ret = se_lines[se_lines.Len()];
    ret.index = se_lines.Len()-1;
    ret.timeout = se_timeout;
    ret.r       = se_r;
    ret.g       = se_g;
    ret.b       = se_b;
    return ret;
}


void eDebugLine::Update(REAL ts)
{
    for (int i = se_lines.Len()-1; i>=0; i--)
    {
        eLineEntry& entry = se_lines(i);
        entry.timeout -= ts;
        if (entry.timeout < 0)
            entry.Delete();
    }
}


void eDebugLine::Render()
{
#ifndef DEDICATED
#ifdef DEBUGLINE
    std::vector<rVertex20> lines;
    lines.reserve(se_lines.Len() * 2);
    for (int i = se_lines.Len()-1; i>=0; i--)
    {
        eLineEntry& entry = se_lines(i);
        uint8_t cr = static_cast<uint8_t>(entry.r * 255.0f);
        uint8_t cg = static_cast<uint8_t>(entry.g * 255.0f);
        uint8_t cb = static_cast<uint8_t>(entry.b * 255.0f);
        lines.push_back(rVertex20(entry.start.x, entry.start.y, entry.startH, cr, cg, cb, 255, 0, 0));
        lines.push_back(rVertex20(entry.stop.x,  entry.stop.y,  entry.stopH,  cr, cg, cb, 255, 0, 0));
    }
    if (!lines.empty()) {
        rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Opaque);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::OpaqueDynamic, state, lines.data(), lines.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::OpaqueDynamic);
    }
#endif
#endif
}

void eDebugLine::SetColor(REAL r, REAL g, REAL b)
{
    se_r = r;
    se_g = g;
    se_b = b;
}

void eDebugLine::SetTimeout(REAL time)
{
    se_timeout = time;
}

void eDebugLine::ResetOptions()
{
    se_r = 1;
    se_g = 1;
    se_b = 1;
    se_timeout = .1f;
}

void eDebugLine::Draw(const eCoord& start, REAL startH,
                      const eCoord& stop , REAL stopH)
{
#ifdef DEBUGLINE
    eLineEntry& line = eLineEntry::Create();
    line.start  = start;
    line.startH = startH;
    line.stop   = stop;
    line.stopH  = stopH;
#endif
}

