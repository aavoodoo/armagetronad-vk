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

#include "cockpit/cGauges.h"

#ifndef DEDICATED
#include "rRender.h"
#include "rFont.h"
#include "rScreen.h"
#include "rVertex.h"
#include "rRenderQueue.h"

namespace cWidget {

bool BarGauge::Process(tXmlParser::node cur) {
    if (
        WithAngles      ::Process(cur) ||
        WithSingleData  ::Process(cur) ||
        WithBackground  ::Process(cur) ||
        WithForeground  ::Process(cur) ||
        WithLineColor   ::Process(cur) ||
        WithCoordinates ::Process(cur) ||
        WithCaption     ::Process(cur) ||
        WithShowSettings::Process(cur) ||
        WithReverse     ::Process(cur))
        return true;
    else {
        DisplayError(cur);
        return false;
    }
}

void BarGauge::RenderMinMax(tValue::Base const &min_s, tValue::Base const &max_s) {
    //Max and min
    if((m_showmin && !m_reverse) || (m_showmax && m_reverse))
        DisplayText(
            m_position.x-m_size.x, m_position.y-.15*m_size.x,
            .24*m_size.x,
            ((m_reverse?max_s:min_s).GetString()).c_str(), sr_fontCockpit);
    if((m_showmax && !m_reverse) || (m_showmin && m_reverse))
        DisplayText( m_position.x+m_size.x, m_position.y-.15*m_size.x,
                     .24*m_size.x,
                     ((m_reverse?min_s:max_s).GetString()).c_str(), sr_fontCockpit);
}

void BarGauge::RenderCaption(void) {
    if ( m_captionloc != off ) {
        const float y=(m_captionloc==top ? m_position.y+m_size.y+.2*m_size.x : m_position.y-.15*m_size.x);
        DisplayText(m_position.x,y,.24*m_size.x,m_caption.c_str(),sr_fontCockpit);
    }
}

void VerticalBarGauge::RenderCaption(void) {
    if ( m_captionloc != off ) {
        const float y=
            (m_captionloc==top ?
             m_position.y+1.2*m_size.y :
             m_position.y-1.2*m_size.y);
        DisplayText(m_position.x,y,.24*m_size.y,m_caption.c_str(),sr_fontCockpit);
    }
}

void BarGauge::Render() {

    const tValue::Base &val_s = m_data.GetVal();
    const tValue::Base &min_s = m_data.GetMin();
    const tValue::Base &max_s = m_data.GetMax();

    float min = min_s.GetFloat();
    float max = max_s.GetFloat();
    float val = val_s.GetFloat();

    if (val > max) val = max;
    if (val < min) val = min;
    if (min >= max) return;

    RenderGraph(min, max, val,m_reverse?-1.:1., val_s);

    RenderMinMax(min_s, max_s);
    RenderCaption();

    //if (!sr_glOut)
    //    return;

    //glColor3f(1,1,1);
}

void BarGauge::RenderGraph(float min, float max, float val, float factor, tValue::Base const &val_s) {
    float x= factor * ((val-min)/(max-min)*2. - 1.);
    float y= (x+1.)/2.;

    const tCoord edge1(tCoord(m_position.x-m_size.x, m_position.y));
    const tCoord edge2(tCoord(m_position.x+m_size.x, m_position.y+m_size.y));

    rGradient &left = (factor < .0) ? m_background : m_foreground;
    rGradient &right = (factor < .0) ? m_foreground : m_background;

    left.SetGradientEdges(edge1, edge2);
    right.SetGradientEdges(edge1, edge2);
    m_line_color.SetGradientEdges(edge1, edge2);

    left.SetValue(y);
    right.SetValue(y);
    m_line_color.SetValue(y);

    {
        std::vector<rVertex20> rv = right.GenerateRectVertices(
            tCoord(m_size.x*x+m_position.x, m_position.y),
            tCoord(m_size.x+m_position.x, m_position.y+m_size.y));
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, right.GetRenderStateKey(), rv.data(), rv.size());

        std::vector<rVertex20> lv = left.GenerateRectVertices(
            tCoord(m_size.x*x+m_position.x, m_position.y),
            tCoord(m_position.x-m_size.x, m_position.y+m_size.y));
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, left.GetRenderStateKey(), lv.data(), lv.size());

        tCoord lp1(m_size.x*x+m_position.x, m_position.y);
        tCoord lp2(m_size.x*x+m_position.x, m_size.y+m_position.y);
        rVertex20 line[2] = { m_line_color.GeneratePointVertex(lp1), m_line_color.GeneratePointVertex(lp2) };
        rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, m_line_color.GetRenderStateKey(), line, 2);
    }

    //Value
    if(m_showvalue)
        DisplayText( m_position.x, m_position.y+.20*m_size.x, .24*m_size.x, (val_s.GetString()).c_str(), sr_fontCockpit);
}


void VerticalBarGauge::RenderGraph(float min, float max, float val, float factor, tValue::Base const &val_s) {
    float x= factor * ((val-min)/(max-min)*2. - 1.);
    float y= (x+1.)/2.;

    const tCoord edge1(tCoord(m_position.x-m_size.x, m_position.y-m_size.y));
    const tCoord edge2(tCoord(m_position.x+m_size.x, m_position.y+m_size.y));

    m_foreground.SetGradientEdges(edge1, edge2);
    m_background.SetGradientEdges(edge1, edge2);
    m_line_color.SetGradientEdges(edge1, edge2);

    m_foreground.SetValue(y);
    m_background.SetValue(y);
    m_line_color.SetValue(y);

    {
        std::vector<rVertex20> bv = m_background.GenerateRectVertices(
            tCoord(m_position.x+m_size.x, m_position.y+m_size.y*x),
            tCoord(m_position.x-m_size.x, m_position.y+m_size.y));
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, m_background.GetRenderStateKey(), bv.data(), bv.size());

        std::vector<rVertex20> fv = m_foreground.GenerateRectVertices(
            tCoord(m_position.x+m_size.x, m_position.y-m_size.y),
            tCoord(m_position.x-m_size.x, m_position.y+m_size.y*x));
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, m_foreground.GetRenderStateKey(), fv.data(), fv.size());

        tCoord lp1(m_position.x+m_size.x, m_position.y+m_size.y*x);
        tCoord lp2(m_position.x-m_size.x, m_position.y+m_size.y*x);
        rVertex20 line[2] = { m_line_color.GeneratePointVertex(lp1), m_line_color.GeneratePointVertex(lp2) };
        rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, m_line_color.GetRenderStateKey(), line, 2);
    }

    //Value
    if(m_showvalue)
        DisplayText( m_position.x, m_position.y, .05*m_size.y, (val_s.GetString()).c_str(), sr_fontCockpit);
}


void NeedleGauge::RenderGraph(float min, float max, float val, float factor, tValue::Base const &val_s) {
    float x, y;
    float t = m_reverse ? (val - max) / (min - max) : (val - min) / (max - min);
    float a = m_angle_min * (1 - t) + m_angle_max * t;
    x= cos(a);
    y= sin(a);

    {
        m_foreground.SetValue((factor * ((val-min)/(max-min)*2. - 1.)+1.)/2.);
        tCoord np1(-.1*x*m_size.x+m_position.x, .1*y*m_size.y+m_position.y);
        tCoord np2(-x*m_size.x+m_position.x, y*m_size.y+m_position.y);
        rVertex20 line[2] = { m_foreground.GeneratePointVertex(np1), m_foreground.GeneratePointVertex(np2) };
        rRenderQueue::Instance().SubmitLines(rRenderPhase::HUD, m_foreground.GetRenderStateKey(), line, 2);
    }

    if(m_showvalue)
        DisplayText( -x*1.45*m_size.x+m_position.x, y*1.35*m_size.y+m_position.y,
                     .2*m_size.x,
                     val_s.GetString().c_str(),
                     sr_fontCockpit);
    if (!sr_glOut)
        return;
}

}

#endif
