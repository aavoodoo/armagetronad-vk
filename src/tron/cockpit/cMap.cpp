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
#include "cockpit/cMap.h"
#include "cockpit/cCockpit.h"
#include "nConfig.h"
#include "tCoord.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rScreen.h"
#include "rVertex.h"
#include "rRenderQueue.h"
#ifdef ENABLE_ZONESV1
#include "gWinZone.h"
#endif
#ifdef ENABLE_ZONESV2
#include "zone/zZone.h"
#endif
#include "eRectangle.h"
#include "ePlayer.h"
#include "eTimer.h"

#include "rViewport.h"
#include "eGrid.h"
#include "gCycle.h"

#endif

#include <vector>

static bool stc_forbidHudMap = false;
// TODO find out why this is throwing an error: “Two tConfItems with the same name FORBID_HUD_MAP!”
// Z-Man: hmm, it doesn't throw an error for me.
static nSettingItem<bool> fcs("FORBID_HUD_MAP", stc_forbidHudMap);
extern std::vector<tCoord> se_rimWallRubberBand;

#ifndef DEDICATED

#ifdef ENABLE_ZONESV1
extern std::deque<gZone *> sg_Zones;
#endif
#ifdef ENABLE_ZONESV2
extern std::deque<zZone *> sz_Zones;
#endif

namespace cWidget {

void Map::ClipperRect::Begin(Map &map, tCoord const &e1, tCoord const &e2) {
    // Scissor is already set by the enclosing Draw() call; just emit geometry.

    // Frame border
    {
        rVertex20 frame[5] = {
            map.m_foreground.GeneratePointVertex(e1),
            map.m_foreground.GeneratePointVertex(tCoord(e2.x, e1.y)),
            map.m_foreground.GeneratePointVertex(e2),
            map.m_foreground.GeneratePointVertex(tCoord(e1.x, e2.y)),
            map.m_foreground.GeneratePointVertex(e1)
        };
        rRenderQueue::Instance().SubmitLineStrip(rRenderPhase::Sky, map.m_foreground.GetRenderStateKey(), frame, 5);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
    // Background
    {
        map.m_background.SetGradientEdges(e1, e2);
        std::vector<rVertex20> bg = map.m_background.GenerateRectVertices(e1, e2);

        rRenderQueue::Instance().Submit(rRenderPhase::Sky, map.m_background.GetRenderStateKey(), bg.data(), bg.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
}

void Map::ClipperRect::End() {
    // No-op: scissor state belongs to the enclosing Draw() call.
}

// ClipperCircle now delegates to rectangular scissor (simplified)
Map::ClipperCircle::ClipperCircle() {
    m_edges = 4; // unused, kept for interface compatibility
}
void Map::ClipperCircle::Clip(int, tCoord const &, tCoord const &) {}
void Map::ClipperCircle::Begin(Map &map, tCoord const &e1, tCoord const &e2) {
    // Scissor is already set by the enclosing Draw() call; just emit geometry.

    // Frame border
    {
        rVertex20 frame[5] = {
            map.m_foreground.GeneratePointVertex(e1),
            map.m_foreground.GeneratePointVertex(tCoord(e2.x, e1.y)),
            map.m_foreground.GeneratePointVertex(e2),
            map.m_foreground.GeneratePointVertex(tCoord(e1.x, e2.y)),
            map.m_foreground.GeneratePointVertex(e1)
        };
        rRenderQueue::Instance().SubmitLineStrip(rRenderPhase::Sky, map.m_foreground.GetRenderStateKey(), frame, 5);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
    // Background
    {
        map.m_background.SetGradientEdges(e1, e2);
        std::vector<rVertex20> bg = map.m_background.GenerateRectVertices(e1, e2);
        rRenderQueue::Instance().Submit(rRenderPhase::Sky, map.m_background.GetRenderStateKey(), bg.data(), bg.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
}
void Map::ClipperCircle::End() {
    // No-op: scissor state belongs to the enclosing Draw() call.
}

bool Map::Process(tXmlParser::node cur) {
    if (
        WithCoordinates ::Process(cur) ||
        WithForeground ::Process(cur) ||
        WithBackground ::Process(cur))
        return true;
    if(cur.IsOfType("MapModes")) {
        int toggleKey;
        cur.GetProp("toggleKey", toggleKey);
        if(toggleKey > 0) {
            m_toggleKey = toggleKey;
            m_Cockpit->AddEventHandler(toggleKey, this);
        }
        for(cur = cur.GetFirstChild(); cur; ++cur) {
            if(cur.IsOfType("MapMode")) {
                m_modes.push_back(Mode(cur));
                if(m_modes.size() == 1) {
                    Apply(m_modes[0]); // apply the first mode and get rid of the defaults
                }
            }
        }
        return true;
    }
    DisplayError(cur);
    return false;
}
Map::Mode::Mode(tXmlParser::node cur) {
    cur.GetProp("zoomFactor", m_zoom);
    tString mode = cur.GetProp("mode");
    if(mode == "closestZone") m_mode = MODE_ZONE;
    else if(mode == "cycle") m_mode = MODE_CYCLE;
    else m_mode = MODE_STD;

    tString rotation = cur.GetProp("rotation");
    if(rotation == "fixed") m_rotation = ROTATION_FIXED;
    else if(rotation == "cycle") m_rotation = ROTATION_CYCLE;
    else if(rotation == "camera") m_rotation = ROTATION_CAMERA;
    else m_rotation = ROTATION_SPAWN;

    tString clipper = cur.GetProp("clipMode");
    if(clipper == "ellipse") {
        m_clipper = ClipperCircle::create;
    } else {
        m_clipper = ClipperRect::create;
    }
}
void Map::Apply(Mode const &mode) {
    m_mode = mode.m_mode;
    m_rotation = mode.m_rotation;
    m_zoom = mode.m_zoom;
    m_clipper.reset((*mode.m_clipper)());
}
void Map::HandleEvent(bool state, int id) {
    if(id == m_toggleKey) {
        if(state) {
            ToggleMode();
        }
    } else {
        Base::HandleEvent(state, id);
    }
}

void Map::Render() {
    // I haven't checked possible initial matrix state, so init to identity and modelview
    if(stc_forbidHudMap) return; // the server doesn't want us to do that
    ProjMatrix();
    IdentityMatrix();
    ModelMatrix();
    IdentityMatrix();

    // Save states that we disable for map rendering
    bool hadTexture = RenderIsEnabled(rCapability::Texture2D);
    bool hadLighting = RenderIsEnabled(rCapability::Lighting);
    bool hadLineSmooth = RenderIsEnabled(rCapability::LineSmooth);

    RenderDisableState(rCapability::Texture2D);
    RenderDisableState(rCapability::Lighting);
    RenderDisableState(rCapability::LineSmooth);
    RenderHint(rHintTarget::LineSmoothHint, rHintMode::Fastest);
    DrawMap(true, true,
            5.5, 0.,
            m_position.x-m_size.x, m_position.y-m_size.y, 2.*m_size.x, 2.*m_size.y,
            sr_screenWidth*m_size.x, sr_screenWidth*m_size.y, .5, .5);

    // Restore states
    if (hadTexture) RenderEnableState(rCapability::Texture2D);
    if (hadLighting) RenderEnableState(rCapability::Lighting);
    if (hadLineSmooth) RenderEnableState(rCapability::LineSmooth);
}
void Map::DrawMap(bool rimWalls, bool cycleWalls,
                  double cycleSize, double border,
                  double x, double y, double w, double h,
                  double rw, double rh, double ix, double iy) {
    double pl_CurSpeed, min_dist2, dist2, rad, zoom = 1;
    tCoord pl_CurPos, rotate; // rotate will hold the cos and sin of the rotation to apply
    cCockpit* cp = m_Cockpit;
    if(!rimWalls && !cycleWalls) return;
    const eRectangle &bounds = eWallRim::GetBounds();
    double lx = bounds.GetLow().x - border, hx = bounds.GetHigh().x + border;
    double ly = bounds.GetLow().y - border, hy = bounds.GetHigh().y + border;
    double mw = hx - lx, mh = hy - ly;
    double xpos, ypos, xscale, yscale;
    double xrat = (rw * mh) / (rh * mw);
    double yrat = (rh * mw) / (rw * mh);
    // set scale and position
    if(xrat > yrat) {
        xscale = (w * rh) / (mh * rw);
        yscale = h / mh;
    } else {
        xscale = w / mw;
        yscale = (h * rw) / (mw * rh);
    }
    rotate.x = 1; // no rotation
    rotate.y = 0;

    //do the rotation
    switch(m_rotation) {
    case ROTATION_SPAWN:
    if(!cp->GetFocusCycle()) { break; }
        rotate = cp->GetFocusCycle()->SpawnDirection().Turn(0,-1);
        break;
    case ROTATION_CYCLE:
    if(!cp->GetFocusCycle()) { break; }
        rotate = cp->GetFocusCycle()->Direction().Turn(0,-1);
        break;
    case ROTATION_CAMERA:
        {
            ePlayer const *player = cp->GetPlayer();
            if(!player) break;
            eCamera const *cam = player->cam;
            if(cam) {
                rotate = cam->CameraDir().Turn(0,-1);
            } else {
                rotate = tCoord(1, 0);
            }
        }
        break;
    default:
        rotate = tCoord(1, 0);
        break;
    }

    // manage mode
    switch (m_mode) {
    case MODE_ZONE:
    if(!cp->GetFocusCycle()) { break; }
        pl_CurPos = cp->GetFocusCycle()->Position();
        min_dist2 = (mw*mw+mh*mh)*1000;
        rad = 0;
#ifdef ENABLE_ZONESV1
// FIXME: I don't know what this actually does; should it be replaced? -- Luke-Jr
        for(std::deque<gZone *>::const_iterator i = sg_Zones.begin(); i != sg_Zones.end(); ++i) {
            tASSERT(*i);
            tCoord const &position = (*i)->GetPosition();
            const float radius = (*i)->GetRadius();
            dist2 = (position-pl_CurPos).Norm();
            if (dist2<min_dist2) {
                min_dist2 = dist2;
                m_centre = position;
                rad = radius;
            }
        }
#endif
#ifdef ENABLE_ZONESV2
        // NOTE: old version compared distance from zone center; this code compares distance from zone border
        for(std::deque<zZone *>::const_iterator i = sz_Zones.begin(); i != sz_Zones.end(); ++i) {
            tASSERT(*i);
            tASSERT((*i)->getShape());
            zShape & shape = *((*i)->getShape());
            dist2 = shape.calcDistanceNear(pl_CurPos);
            if (dist2<min_dist2) {
                min_dist2 = dist2;
                m_centre = shape.findCenter();
                rad = shape.calcBoundSq() / 2;
            }
        }
#endif
        rad = (rad<15)?15:rad;
        zoom = (w>h)?h/(yscale*m_zoom*rad):w/(xscale*m_zoom*rad);
        zoom = (zoom<1)?1:zoom;
        // check if at least 1 zone was found, if not, toggle to mode 2 ...
        if (rad==0) {
            m_mode = MODE_CYCLE;
            // [[fallthrough]];
        } else {
            break;
        }
        // fallthrough on purpose
    case MODE_CYCLE:
    if(!cp->GetFocusCycle()) { break; }
        m_centre = cp->GetFocusCycle()->Position();
        pl_CurSpeed = cp->GetFocusCycle()->Speed();
        zoom = (w>h)?h/(yscale*m_zoom*pl_CurSpeed):w/(xscale*m_zoom*pl_CurSpeed);
        zoom = (zoom<1)?1:zoom;
        break;
    default :
        zoom = 1;
        m_centre.x = lx + mw / 2;
        m_centre.y = ly + mh / 2;
        break;
    }
    xscale *=zoom;
    yscale *=zoom;
    xpos = x - m_centre.x * xscale + w / 2;
    ypos = y - m_centre.y * yscale + h / 2;
    // Enable scissor for the widget area (all modes).
    int scissorVP[4];
    {
        RenderGetViewport(scissorVP);
        int sx = scissorVP[0] + static_cast<int>((x + 1.0f) * 0.5f * scissorVP[2]);
        int sy = scissorVP[1] + static_cast<int>((y + 1.0f) * 0.5f * scissorVP[3]);
        int sw = static_cast<int>(w * 0.5f * scissorVP[2]);
        int sh = static_cast<int>(h * 0.5f * scissorVP[3]);
        RenderScissor(sx, sy, sw, sh);
    }
    if(m_mode != MODE_STD) {
        m_clipper->Begin(*this, tCoord(x,y), tCoord(x+w, y+h));
    }
    // set projection matrix
    PushMatrix();
    TranslateMatrix(xpos, ypos, 0);
    ScaleMatrix(xscale, yscale, 1);
    // translate and rotate
    float r[16] = {
          rotate.x,  -rotate.y, 0, 0,
          rotate.y,   rotate.x, 0, 0,
                 0,          0, 1, 0,
        m_centre.x, m_centre.y, 0, 1};
    MultMatrix(r);
    TranslateMatrix(-m_centre.x,-m_centre.y,0);
    if(rimWalls)
        DrawRimWalls(se_rimWalls);
    if(cycleWalls) {
        DrawWalls(sg_netPlayerWallsGridded);
        DrawWalls(sg_netPlayerWalls);
    }
    DrawObjects(tCoord((cycleSize * w) / (rw * xscale), (cycleSize * h) / (rh * yscale)));
    // Flush zone lines (submitted to Sky by Render2D) before the map MVP is popped.
    rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
    PopMatrix();
    if(m_mode != MODE_STD) {
        m_clipper->End();
    }
    // Reset scissor to full viewport (Vulkan has no "disable scissor"; set to viewport bounds).
    RenderScissor(scissorVP[0], scissorVP[1], scissorVP[2], scissorVP[3]);
}

void Map::DrawRimWalls( tList<eWallRim> &list ) {
    if(sr_alphaBlend && m_mode == MODE_STD) {
        const eRectangle &bounds = eWallRim::GetBounds();
        const tCoord dims = bounds.GetHigh() - bounds.GetLow();
        const float max = fmax(dims.x, dims.y);
        m_background.SetGradientEdges(bounds.GetLow(), tCoord(bounds.GetLow().x + max, bounds.GetLow().y + max));
        // Polygon → triangle fan
        if (se_rimWallRubberBand.size() >= 3) {
            std::vector<rVertex20> polyVerts;
            polyVerts.reserve((se_rimWallRubberBand.size() - 1) * 3);
            rVertex20 v0 = m_background.GeneratePointVertex(se_rimWallRubberBand[0]);
            for (size_t i = 1; i + 1 < se_rimWallRubberBand.size(); ++i) {
                polyVerts.push_back(v0);
                polyVerts.push_back(m_background.GeneratePointVertex(se_rimWallRubberBand[i]));
                polyVerts.push_back(m_background.GeneratePointVertex(se_rimWallRubberBand[i+1]));
            }


            // Don't TransformVerticesByMVP — vertices are in world coords,
            // the shader applies the map's MVP (currently on the matrix stack)
            rRenderQueue::Instance().Submit(rRenderPhase::Sky, m_background.GetRenderStateKey(), polyVerts.data(), polyVerts.size());
            rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
            ModelMatrix();
        }
    }
    // Rim wall outlines
    {
        std::vector<rVertex20> lines;
        lines.reserve(list.Len() * 2);
        for (int i=list.Len()-1; i >= 0; --i)
        {
            eWallRim *wall = list[i];
            eCoord begin = wall->EndPoint(0), end = wall->EndPoint(1);
            lines.push_back(rVertex20(begin.x, begin.y, 0, 255, 255, 255, 127, 0, 0));
            lines.push_back(rVertex20(end.x, end.y, 0, 255, 255, 255, 127, 0, 0));
        }
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::Sky, state, lines.data(), lines.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
}

void Map::DrawWalls(tList<gNetPlayerWall> &list) {
    unsigned i, len=list.Len();
    double currentTime = se_GameTime();
    bool limitedLength = gCycle::WallsLength() > 0;
    double wallsStayUpDelay = gCycle::WallsStayUpDelay();
    std::vector<rVertex20> wallLines;
    wallLines.reserve(len * 4);
    for(i=0; i<len; i++) {
        gNetPlayerWall *wall = list[i];
        gCycle *cycle = wall->Cycle();
        if(!cycle) continue;
        double wallsLength = cycle->ThisWallsLength();
        double alpha = 1;
        if(!cycle->Alive() && wallsStayUpDelay >= 0) {
            alpha -= 2 * (currentTime - cycle->DeathTime() - wallsStayUpDelay);
            if(alpha <= 0) continue;
        }
        uint8_t cr = static_cast<uint8_t>(cycle->color_.r_ * 255.0f);
        uint8_t cg = static_cast<uint8_t>(cycle->color_.g_ * 255.0f);
        uint8_t cb = static_cast<uint8_t>(cycle->color_.b_ * 255.0f);
        uint8_t ca = static_cast<uint8_t>(alpha * 255.0f);
        double cycleDist = cycle->GetDistance();
        double minDist = limitedLength && cycleDist > wallsLength ? cycleDist - wallsLength : 0;
        const eCoord &begPos = wall->EndPoint(0), &endPos = wall->EndPoint(1);
        tArray<gPlayerWallCoord> &coords = wall->Coords();
        double begDist = wall->BegPos();
        double lenDist = wall->EndPos() - begDist;
        unsigned j, numcoords = coords.Len();
        if(numcoords < 2) continue;
        bool prevDangerous = coords[0].IsDangerous;
        double prevDist = coords[0].Pos;
        if(prevDist < minDist) prevDist = minDist;
        prevDist = (prevDist - begDist) / lenDist;
        for(j=1; j<numcoords; j++) {
            bool curDangerous = coords[j].IsDangerous;
            double curDist = coords[j].Pos;
            if(curDist < minDist) curDist = minDist;
            curDist = (curDist - begDist) / lenDist;
            if(prevDangerous) {
                float px = begPos.x + prevDist * (endPos.x - begPos.x);
                float py = begPos.y + prevDist * (endPos.y - begPos.y);
                float cx = begPos.x + curDist * (endPos.x - begPos.x);
                float cy = begPos.y + curDist * (endPos.y - begPos.y);
                wallLines.push_back(rVertex20(px, py, 0, cr, cg, cb, ca, 0, 0));
                wallLines.push_back(rVertex20(cx, cy, 0, cr, cg, cb, ca, 0, 0));
            }
            prevDangerous = curDangerous;
            prevDist = curDist;
        }
    }
    if (!wallLines.empty()) {
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::Sky, state, wallLines.data(), wallLines.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        ModelMatrix();
    }
}

void Map::DrawObjects(tCoord scale) {
    tList<eGameObject> const &gameObjects = eGrid::CurrentGrid()->GameObjects();
    size_t len = gameObjects.Len();
    for(size_t i = 0; i < len; ++i) {
        eGameObject const *obj = gameObjects(i);
        tASSERT(obj);
        obj->Render2D(scale);
    }
}

void Map::ToggleMode(void) {
    if(m_modes.empty()) return;
    m_currentMode = (m_currentMode+1) % m_modes.size();
    Apply(m_modes[m_currentMode]);
}

Map::Map():
        m_mode(MODE_STD),
        m_zoom(3),
        m_rotation(ROTATION_SPAWN),
        m_toggleKey(0),
        m_clipper(new ClipperRect()),
        m_currentMode(0)
{
}

}
#endif
