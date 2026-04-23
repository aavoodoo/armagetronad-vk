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

#include "gLogo.h"

#include "gStuff.h"
#include "rTexture.h"
#include "rRender.h"
#include "tDirectories.h"
#include "rScreen.h"
#include "eCoord.h"
#include "uMenu.h"
#include "tSysTime.h"

#ifndef DEDICATED
#include "rVertex.h"
#include "rRenderQueue.h"
#include "rRenderBucket.h"
#include <vector>
#endif

// static rFileTexture sg_LogoTexture(rTextureGroups::TEX_FONT, "textures/KGN_logo.png",0,0,1);
static rISurfaceTexture* sg_LogoMPTitle = NULL;

static gLogo logo;

static bool sg_Displayed = true;
static bool sg_Spinning  = false;
static bool sg_Big       = true;

static eCoord sg_SpinStatus(1,0);    // current spinning position
static REAL   sg_SizeStatus(1);    // 1 -> big      , 0 -> small
static REAL   sg_DisplayStatus(1); // 1 -> displayed, 0->invisible (start visible, no fade-in)

void gLogo::SetDisplayed(bool d, bool immediately)
{
    if (sg_Displayed == false && d == true && sg_DisplayStatus < .01)
        sg_SpinStatus = eCoord(0, 1);

    sg_Displayed = d;
    if (immediately)
        sg_DisplayStatus = d ? 1 : 0;
}

void gLogo::SetSpinning(bool s)
{
    sg_Spinning = s;
    if (!s)
        sg_SpinStatus = eCoord(1, 0);
}
void gLogo::SetBig(bool b, bool immediately)
{
    sg_Big = b;
    if (immediately)
        sg_SizeStatus = b ? 1 : 0;

}

/*
static tString sg_title("Anonymous/original/textures/title.jpg");
static nSettingItem<tString> gg_title("TEXTURE_TITLE", sg_title);

static tString sg_mp_title("Anonymous/original/moviepack/title.jpg");
static nSettingItem<tString> gg_mp_title("TEXTURE_MP_TITLE", sg_mp_title);
*/

void gLogo::Display()
{
#ifndef DEDICATED
    if (!sr_glOut)
        return;

    if (sg_MoviePack() && !sg_LogoMPTitle)
    {
        // Check if moviepack has its own title; fall back to default if not
        tString mpTitle = tDirectories::Data().GetReadPath("moviepack/title.jpg");
        if (mpTitle.Len() > 1)
            sg_LogoMPTitle = tNEW(rFileTexture)(rTextureGroups::TEX_FONT, "moviepack/title.jpg",0,0,1);
        else
            sg_LogoMPTitle = tNEW(rFileTexture)(rTextureGroups::TEX_FONT, "textures/title.jpg",0,0,1);
    }

    renderer->SetFlag(rRenderer::DEPTH_TEST, false);

    static REAL lasttime = 0;
    REAL time = tSysTimeFloat();
    REAL dt = time - lasttime;
    lasttime = time;

    if (!sg_Displayed && sg_DisplayStatus < .00001)
        return;

    if (sg_LogoMPTitle)
    {
        // update state variables
        if (sg_Displayed && sg_Big)
        {
            sg_DisplayStatus += dt;
            if (sg_DisplayStatus > 1)
                sg_DisplayStatus = 1;
        }
        else
        {
            sg_DisplayStatus -= dt;
            if (sg_DisplayStatus < 0)
                sg_DisplayStatus = 0;
        }

        if (sg_DisplayStatus <= .01)
            return;

        sg_LogoMPTitle->Select();

        if(!sg_LogoMPTitle->Loaded())
            return;

        // Get texture ID for batch rendering
        unsigned int textureId = RenderGetBoundTexture2D();

        // Create vertices for fullscreen quad with alpha
        uint8_t alpha = static_cast<uint8_t>(sg_DisplayStatus * 255.0f);
        std::vector<rVertex20> vertices;
        vertices.reserve(6);

        // Triangle 1 (top-left, bottom-left, bottom-right)
        vertices.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(-1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 1.0f));
        vertices.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));

        // Triangle 2 (top-left, bottom-right, top-right)
        vertices.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));
        vertices.push_back(rVertex20(1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 0.0f));

        // Set up identity matrices for HUD rendering (clip space coordinates)
        ModelMatrix();
        IdentityMatrix();
        ProjMatrix();
        IdentityMatrix();

        // Submit to render queue
        rRenderStateKey state = rRenderStateKey::HUD(textureId, rBlendMode::Alpha);
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, vertices.data(), vertices.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
    }
    else
    {
#ifndef KRAWALL
        sg_LogoMPTitle = tNEW(rFileTexture)(rTextureGroups::TEX_FONT, "textures/title.jpg",0,0,1);
        // sg_LogoMPTitle = tNEW(rFileTexture)(rTextureGroups::TEX_FONT, sg_title,0,0,1);

        sg_DisplayStatus = 1;

        // update state variables
        if (sg_Displayed && sg_Big)
        {
            sg_DisplayStatus += dt;
            if (sg_DisplayStatus > 1)
                sg_DisplayStatus = 1;
        }
        else
        {
            sg_DisplayStatus -= dt;
            if (sg_DisplayStatus < 0)
                sg_DisplayStatus = 0;
        }

        if (sg_DisplayStatus <= .01)
            return;

        sg_LogoMPTitle->Select();

        if (!sg_LogoMPTitle->Loaded())
            return;

        // Get texture ID for batch rendering
        unsigned int textureId = RenderGetBoundTexture2D();

        // Create vertices for fullscreen quad with alpha
        uint8_t alpha = static_cast<uint8_t>(sg_DisplayStatus * 255.0f);
        std::vector<rVertex20> vertices;
        vertices.reserve(6);

        // Triangle 1 (top-left, bottom-left, bottom-right)
        vertices.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(-1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 1.0f));
        vertices.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));

        // Triangle 2 (top-left, bottom-right, top-right)
        vertices.push_back(rVertex20(-1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(1.0f, -1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));
        vertices.push_back(rVertex20(1.0f, 1.0f, 0.0f, 255, 255, 255, alpha, 1.0f, 0.0f));

        // Set up identity matrices for HUD rendering (clip space coordinates)
        ModelMatrix();
        IdentityMatrix();
        ProjMatrix();
        IdentityMatrix();

        // Submit to render queue
        rRenderStateKey state = rRenderStateKey::HUD(textureId, rBlendMode::Alpha);
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, vertices.data(), vertices.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
#endif

#ifdef KRAWALL
        sg_LogoTexture.Select();

        if ( !sg_LogoTexture.Loaded() )
        {
            return;
        }

        // update state variables
        if (sg_Spinning)
        {
            sg_SpinStatus = sg_SpinStatus.Turn(1, dt * 2 * .2 / (sg_SizeStatus + .2));
            sg_SpinStatus = sg_SpinStatus * (1/sqrt(sg_SpinStatus.NormSquared()));
        }

        if (sg_Big)
        {
            sg_SizeStatus += dt;
            if (sg_SizeStatus > 1)
                sg_SizeStatus = 1;
        }
        else
        {
            sg_SizeStatus *= (1 - 2 * dt);
            //      if (sg_SizeStatus < 0)
            //	sg_SizeStatus = 0;
        }

        if (sg_Displayed)
        {
            sg_DisplayStatus += dt;
            if (sg_DisplayStatus > 1)
                sg_DisplayStatus = 1;
        }
        else
        {
            sg_DisplayStatus -= dt*.3;
            if (sg_DisplayStatus < 0)
                sg_DisplayStatus = 0;
        }

        if (sg_DisplayStatus <= 0)
            return;

        eCoord center(.8*(sg_SizeStatus*sg_SizeStatus-1), .8*(1-sg_SizeStatus));
        REAL e =.8 * (sg_SizeStatus + .1);
        eCoord extension(e, - .7 * e * fabs(sg_SpinStatus.x));

        eCoord ur = center - extension;
        eCoord ll = center + extension;

        // Get texture ID for batch rendering
        unsigned int textureId = RenderGetBoundTexture2D();

        // Create vertices for positioned quad with alpha
        uint8_t alpha = static_cast<uint8_t>(sg_DisplayStatus * 255.0f);
        std::vector<rVertex20> vertices;
        vertices.reserve(6);

        // Triangle 1 (top-left, bottom-left, bottom-right)
        vertices.push_back(rVertex20(ur.x, ur.y, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(ur.x, ll.y, 0.0f, 255, 255, 255, alpha, 0.0f, 1.0f));
        vertices.push_back(rVertex20(ll.x, ll.y, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));

        // Triangle 2 (top-left, bottom-right, top-right)
        vertices.push_back(rVertex20(ur.x, ur.y, 0.0f, 255, 255, 255, alpha, 0.0f, 0.0f));
        vertices.push_back(rVertex20(ll.x, ll.y, 0.0f, 255, 255, 255, alpha, 1.0f, 1.0f));
        vertices.push_back(rVertex20(ll.x, ur.y, 0.0f, 255, 255, 255, alpha, 1.0f, 0.0f));

        // Set up identity matrices for HUD rendering (clip space coordinates)
        ModelMatrix();
        IdentityMatrix();
        ProjMatrix();
        IdentityMatrix();

        // Submit to render queue
        rRenderStateKey state = rRenderStateKey::HUD(textureId, rBlendMode::Alpha);
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, vertices.data(), vertices.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
#endif

    }

#endif
}

gLogo::~gLogo()
{
    if (sg_LogoMPTitle)
    {
        tDESTROY(sg_LogoMPTitle);
    }
}

void gLogo::ResetTexture()
{
    if (sg_LogoMPTitle)
    {
        tDESTROY(sg_LogoMPTitle);
        sg_LogoMPTitle = NULL;
    }
    // Keep current display status — no fade-in on texture reload
}
