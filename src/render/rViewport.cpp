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

#include "rFont.h"
#include "rRender.h"
#include "rVertex.h"
#ifndef DEDICATED
#include "rRenderQueue.h"
#endif
#include "rScreen.h"
#include "rViewport.h"
#include "rConsole.h"
#include "tConfiguration.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifndef DEDICATED
//#include <GL/glu>
#ifdef POWERPAK_DEB
#include "PowerPak/powerdraw.h"
#endif
#endif

#ifndef DEDICATED
void rViewport::Select(){
    if (sr_glOut)
        RenderViewport(int(sr_screenWidth*left),
                       int(sr_screenHeight*bottom),
                       int(sr_screenWidth*width),
                       int(sr_screenHeight*height));
}
#endif


REAL rViewport::UpDownFOV(REAL fov){
    REAL ratio=currentScreensetting.aspect*(width*sr_screenWidth)/(height*sr_screenHeight);

    // clamp ratio to 5/3
    REAL maxratio = 5.0/3.0;
    if (ratio > maxratio)
        ratio = maxratio;

    return 360*atan(tan(M_PI*fov/360)/ratio)/M_PI;
}

void rViewport::Perspective(REAL fov,REAL nnear,REAL ffar,REAL xshift){
#ifndef DEDICATED
    if (!sr_glOut)
        return;

#if 1
    // Jonathan's improved version (fixed again)

    // the true aspect ratio of the viewport. 16/9, 16/10, 4/3, 5/4 (or halves of that for splitscreen)
    REAL aspectratio = (width * sr_screenWidth * currentScreensetting.aspect)/(height * sr_screenHeight);

    // usually, fov is the horizontal fov. However, for widescreen, we want to expand the
    // horizontal fov without distorting the image in such a way that we don't sacrifice too much of
    // the vertical fov. For non-widescreen, the following number will be 1, for aspect ratios > 1.5 (semi-widescreen),
    // it'll be higher.
    REAL ensureverticalfov = fmax(aspectratio/1.5, 1.0);

    // calculate the horizontal fov. For widescreen, make it extra wide.
    REAL xmul = ensureverticalfov * tan((M_PI / 360.0) * fov);

    // transfer that directly to the vertical fov.
    REAL ymul = xmul/aspectratio;
    ProjMatrix();
    xshift *= nnear;
    Frustum(-nnear * xmul + xshift, nnear * xmul + xshift, -nnear * ymul, nnear * ymul, nnear, ffar);
    TranslateMatrix(xshift, 0.f, 0.f);
#endif

#endif
}

bool rViewport::Contains(float sx, float sy) const {
    // sy is SDL touch (y=0 top). Viewport y-axis is OpenGL (y=0 bottom).
    const float yFlipped = 1.0f - sy;
    return sx >= left && sx < left + width &&
           yFlipped >= bottom && yFlipped < bottom + height;
}

void rViewport::TouchToCockpitHud(float sx, float sy, float& hx, float& hy, int rotDeg) const {
    // Step 1: SDL touch → viewport-local [0,1] (lx: 0=left,1=right; ly: 0=top,1=bottom).
    const float topInSdl = 1.0f - bottom - height;
    const float invW = (width  > 0.0f) ? 1.0f / width  : 1.0f;
    const float invH = (height > 0.0f) ? 1.0f / height : 1.0f;
    const float lx = (sx - left)     * invW;
    const float ly = (sy - topInSdl) * invH;

    // Step 2: Compute FBO UV — the texture coordinate sampled at screen (lx, ly).
    // Derived analytically from the uvTable in CompositeViewportFBOs (rVulkanRender.cpp).
    // FBO UV convention: u=0 left, u=1 right; v=0 FBO-top, v=1 FBO-bottom.
    //
    //   0°:   (u,v) = (lx,   ly  )   — no rotation
    //   90°:  (u,v) = (ly,   1-lx)   — CW 90° for left-side player
    //  180°:  (u,v) = (1-lx, 1-ly)   — 180° for top-side player (top-bottom split)
    //  270°:  (u,v) = (1-ly, lx  )   — CCW 90° for right-side player
    const int normRot = ((rotDeg % 360) + 360) % 360;
    float fbo_u, fbo_v;
    switch (normRot) {
    case  90: fbo_u = ly;        fbo_v = 1.0f - lx; break;
    case 180: fbo_u = 1.0f - lx; fbo_v = 1.0f - ly; break;
    case 270: fbo_u = 1.0f - ly; fbo_v = lx;         break;
    default:  fbo_u = lx;        fbo_v = ly;          break; // 0°
    }

    // Step 3: FBO UV → cockpit NDC (hx, hy).
    // For 90°/270° the FBO is created with W and H swapped relative to the
    // screen viewport dimensions (BeginViewportFBO). sr_RenderViewportCockpit
    // sees the swapped dims as sr_screenWidth/Height and computes factor and
    // visTopY from them, so the touch mapping must use the same swapped FBO size.
    //
    // NDC x = 2*fbo_u − 1
    // NDC y = (1 − fbo_v) × fboH × 2 / max(fboW,fboH) − 1
    //   (mirrors EqualAspectBottom, which sets viewport height =
    //    max(fboW/fboH, 1), so visTopY = min(1, 2*fboH/fboAxisMax − 1))
    const float vpW_px = width  * (float)sr_screenWidth;
    const float vpH_px = height * (float)sr_screenHeight;
    float fboW, fboH;
    if (normRot == 90 || normRot == 270) {
        fboW = vpH_px;   // FBO created with swapped W×H for 90°/270°
        fboH = vpW_px;
    } else {
        fboW = vpW_px;
        fboH = vpH_px;
    }
    const float fboAxisMax = std::max(fboW, fboH);

    hx = fbo_u * 2.0f - 1.0f;
    if (fboAxisMax > 0.0f) {
        hy = (1.0f - fbo_v) * fboH * 2.0f / fboAxisMax - 1.0f;
    } else {
        hy = 1.0f - fbo_v * 2.0f;
    }
}

rViewport rViewport::s_viewportFullscreen(0,0,1,1);

rViewport rViewport::s_viewportTop(0,.5,1,.5);
rViewport rViewport::s_viewportBottom(0,0,1,.5);

rViewport rViewport::s_viewportLeft(0,0,.5,1);
rViewport rViewport::s_viewportRight(.5,0,.5,1);

rViewport rViewport::s_viewportTopLeft(0,.5,.5,.5);
rViewport rViewport::s_viewportBottomLeft(0,0,.5,.5);
rViewport rViewport::s_viewportTopRight(.5,.5,.5,.5);
rViewport rViewport::s_viewportBottomRight(.5,0,.5,.5);
rViewport rViewport::s_viewportDemonstation(.55,.05,.4,.4);

int   sr_viewportBelongsToPlayer[MAX_VIEWPORTS],
s_newViewportBelongsToPlayer[MAX_VIEWPORTS];

// ***********************************************************


rViewportConfiguration::rViewportConfiguration(rViewport *first)
        :num_viewports(1){
    viewports[0]=first;
}

rViewportConfiguration::rViewportConfiguration(rViewport *first,
        rViewport *second)
        :num_viewports(2){
    viewports[0]=first;
    viewports[1]=second;
}

rViewportConfiguration::rViewportConfiguration(rViewport *first,
        rViewport *second,
        rViewport *third)
        :num_viewports(3){
    viewports[0]=first;
    viewports[1]=second;
    viewports[2]=third;
}

rViewportConfiguration::rViewportConfiguration(rViewport *first,
        rViewport *second,
        rViewport *third,
        rViewport *forth)
        :num_viewports(4){
    viewports[0]=first;
    viewports[1]=second;
    viewports[2]=third;
    viewports[3]=forth;
}

#ifndef DEDICATED
void rViewportConfiguration::Select(int i){
    if (i>=0 && i <num_viewports)
        viewports[i]->Select();
}
#endif

rViewport * rViewportConfiguration::Port(int i){
    if (i>=0 && i <num_viewports)
        return viewports[i];
    else
        return NULL;
}

static rViewportConfiguration single_vp(&rViewport::s_viewportFullscreen);
static rViewportConfiguration two_vp(&rViewport::s_viewportTop,
                                     &rViewport::s_viewportBottom);
static rViewportConfiguration two_b(&rViewport::s_viewportLeft,
                                    &rViewport::s_viewportRight);
static rViewportConfiguration three_a(&rViewport::s_viewportTop,
                                      &rViewport::s_viewportBottomLeft,
                                      &rViewport::s_viewportBottomRight);
static rViewportConfiguration three_b(&rViewport::s_viewportTopLeft,
                                      &rViewport::s_viewportTopRight,
                                      &rViewport::s_viewportBottom);
static rViewportConfiguration four_vp(&rViewport::s_viewportTopLeft,
                                      &rViewport::s_viewportTopRight,
                                      &rViewport::s_viewportBottomLeft,
                                      &rViewport::s_viewportBottomRight);

rViewportConfiguration *rViewportConfiguration::s_viewportConfigurations[]={
            &single_vp,&two_vp,&two_b,&three_a,&three_b,&four_vp};

char const * rViewportConfiguration::s_viewportConfigurationNames[]=
    {"$viewport_conf_name_0",
     "$viewport_conf_name_1",
     "$viewport_conf_name_2",
     "$viewport_conf_name_3",
     "$viewport_conf_name_4",
     "$viewport_conf_name_5"};

const int  rViewportConfiguration::s_viewportNumConfigurations=6;



// *******************************************************
//   Player menu
// *******************************************************

static int conf_num=0;
int rViewportConfiguration::next_conf_num=0;

static tConfItem<int> confn("VIEWPORT_CONF",
                            rViewportConfiguration::next_conf_num);

rViewportConfiguration *rViewportConfiguration::CurrentViewportConfiguration(){
    if (conf_num<0) conf_num=0;
    if (conf_num>=s_viewportNumConfigurations)
        conf_num=s_viewportNumConfigurations-1;

    return s_viewportConfigurations[conf_num];
}

#ifndef DEDICATED
void rViewportConfiguration::DemonstrateViewport(tString *titles){
    if (!sr_glOut)
        return;

    for(int i=s_viewportConfigurations[next_conf_num]->num_viewports-1;i>=0;i--){
        rViewport sub(rViewport::s_viewportDemonstation,*(s_viewportConfigurations[next_conf_num]->Port(i)));

        // Convert sub-viewport local NDC to fullscreen NDC
        tCoord pos = sub.GetPosition();    // (left, bottom) in 0..1 screen fractions
        tCoord dim = sub.GetDimensions();  // (width, height) in 0..1 screen fractions

        // Map local NDC point to fullscreen NDC: ndc = (frac * 2) - 1
        // where frac = pos + (local + 1) * 0.5 * dim
        auto toNDC = [&](float lx, float ly, float &nx, float &ny) {
            nx = (pos.x + (lx + 1.0f) * 0.5f * dim.x) * 2.0f - 1.0f;
            ny = (pos.y + (ly + 1.0f) * 0.5f * dim.y) * 2.0f - 1.0f;
        };

        // Background quad (local -0.9..0.9)
        float x0, y0, x1, y1;
        toNDC(-0.9f, -0.9f, x0, y0);
        toNDC( 0.9f,  0.9f, x1, y1);
        rVertex20 q0(x0, y0, 0, 25, 25, 102, 255, 0, 0);
        rVertex20 q1(x1, y0, 0, 25, 25, 102, 255, 0, 0);
        rVertex20 q2(x1, y1, 0, 25, 25, 102, 255, 0, 0);
        rVertex20 q3(x0, y1, 0, 25, 25, 102, 255, 0, 0);
        rRenderStateKey qState = rRenderStateKey::HUD(0, rBlendMode::Opaque);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, qState, q0, q1, q2, q3);

        // Border line loop (local -1..1 = full sub-viewport edge)
        float bx0, by0, bx1, by1;
        toNDC(-1.0f, -1.0f, bx0, by0);
        toNDC( 1.0f,  1.0f, bx1, by1);
        rVertex20 border[5] = {
            rVertex20(bx0, by0, 0, 153, 153, 153, 255, 0, 0),
            rVertex20(bx0, by1, 0, 153, 153, 153, 255, 0, 0),
            rVertex20(bx1, by1, 0, 153, 153, 153, 255, 0, 0),
            rVertex20(bx1, by0, 0, 153, 153, 153, 255, 0, 0),
            rVertex20(bx0, by0, 0, 153, 153, 153, 255, 0, 0)
        };
        rRenderStateKey lState = rRenderStateKey::HUD(0, rBlendMode::Opaque);
        rRenderQueue::Instance().SubmitLineStrip(rRenderPhase::HUD, lState, border, 5);

        // Render the label in fullscreen NDC space so the font's global scaleX/Y
        // mapping (2/sr_screenWidth, 2/sr_screenHeight) is never distorted by a
        // sub-viewport scissor.  DisplayText x,y are NDC (−1..+1); cheight is
        // also in NDC units.  We centre the digit in the sub-viewport.
        {
            // NDC centre of the sub-viewport
            float cx = (pos.x + dim.x * 0.5f) * 2.0f - 1.0f;
            float cy = (pos.y + dim.y * 0.5f) * 2.0f - 1.0f;

            // cheight: 40 % of sub-viewport height in NDC
            float cheight = dim.y * 0.8f;

            // centre=0 means DisplayText centres horizontally on x.
            // Vertical: place baseline so text midpoint aligns with viewport centre.
            Color(1,1,1);
            DisplayText(cx, cy + cheight * 0.1f, cheight, titles[i], sr_fontMenu, 0);
        }
    }

    rViewport::s_viewportFullscreen.Select();
}
#endif


rViewport * rViewportConfiguration::CurrentViewport(int i){
    return CurrentViewportConfiguration()->Port(i);
}

void rViewportConfiguration::UpdateConf(){
    conf_num = next_conf_num;
}


static int vpb_dir[MAX_VIEWPORTS];

void rViewport::CorrectViewport(int i, int MAX_PLAYERS){
    if (vpb_dir[i]!=1 && vpb_dir[i]!=-1)
        vpb_dir[i]=1;

    int starta=rViewportConfiguration::s_viewportConfigurations[rViewportConfiguration::next_conf_num]->num_viewports-1;
    int startb=rViewportConfiguration::s_viewportConfigurations[     conf_num]->num_viewports-1;
    if (starta>startb)
        startb=starta;
    
    s_newViewportBelongsToPlayer[i]+=MAX_PLAYERS-vpb_dir[i];
    s_newViewportBelongsToPlayer[i]%=MAX_PLAYERS;

    int oldValue = s_newViewportBelongsToPlayer[i];

    bool again;
    bool expectChange = false;
    do{
        // rotate player assignemnt
        s_newViewportBelongsToPlayer[i]+=MAX_PLAYERS+vpb_dir[i];
        s_newViewportBelongsToPlayer[i]%=MAX_PLAYERS;

        // check for conflicts
        again=false;
        for(int j=starta;j>=0;j--)
            if (i!=j && s_newViewportBelongsToPlayer[i]
                    ==s_newViewportBelongsToPlayer[j])
            {
                again=true;
                expectChange=true;
            }
    } while(again);

    if ( oldValue == s_newViewportBelongsToPlayer[i] && expectChange )
    {
        // no change? swap players.
        s_newViewportBelongsToPlayer[i]+=MAX_PLAYERS+vpb_dir[i];
        s_newViewportBelongsToPlayer[i]%=MAX_PLAYERS;

        for(int j=starta;j>=0;j--)
            if (i!=j && s_newViewportBelongsToPlayer[i]
                    ==s_newViewportBelongsToPlayer[j])
            {
                s_newViewportBelongsToPlayer[j] = oldValue;
            }
    }
}

void rViewport::CorrectViewports(int MAX_PLAYERS){
    for (int i=rViewportConfiguration::s_viewportConfigurations[conf_num]->num_viewports-1;i>=0;i--)
        CorrectViewport(i, MAX_PLAYERS);
}


void rViewport::Update(int MAX_PLAYERS){
    rViewportConfiguration::UpdateConf();
    CorrectViewports(MAX_PLAYERS);

    int i;
    for(i=MAX_VIEWPORTS-1;i>=0;i--)
        sr_viewportBelongsToPlayer[i]=s_newViewportBelongsToPlayer[i];
}

void rViewport::SetDirectionOfCorrection(int vp, int dir){
    vpb_dir[vp] = dir;
}


static REAL sr_HUDMaxWidth{1.333333};
static tConfItem<REAL> sr_HUDAspectModeConf("HUD_MAX_WIDTH", sr_HUDMaxWidth);

// *******************************************************************************************
// *
// *	CorrectAspectBottom
// *
// *******************************************************************************************
//!
//!		@return
//!
// *******************************************************************************************

rViewport rViewport::CorrectAspectBottom( void ) const
{
    rViewport ret( *this );

    if(sr_HUDMaxWidth <= 0)
    {
        ret.height = width * 4.0 / 3.0;
        return ret;
    }

    // start with giving it the correct aspect ratio
    REAL aspect = 4.0 / 3.0 /  rTextField::AspectWidthMultiplier();
    ret.height = width * aspect;

    REAL const max_aspect = sr_HUDMaxWidth * std::max(height/width, 1.0f);

    if(max_aspect < aspect)
    {
        // clamp the width down, keep viewport centered
        ret.height = width * max_aspect;
        REAL clampedWidth = ret.height/aspect;
        ret.left = ret.left + (ret.width - clampedWidth)*.5f;
        ret.width = clampedWidth;
    }

    return ret;
}

rViewport rViewport::EqualAspectBottom( void ) const
{
    rViewport ret( *this );
    // Square in pixels: viewport height = width × (screen_W / screen_H) in
    // pixels. For landscape FBOs (W ≥ H) this gives a viewport TALLER than
    // the FBO — extends above the visible area, clipped at the FBO top by
    // the scissor (intentional: keeps cockpit NDC square in pixels).
    //
    // For portrait-leaning FBOs (W < H) the square is SHORTER than the
    // FBO, which used to leave an empty strip at the FBO top that
    // anchorV="top" widgets couldn't reach (cockpit NDC y=+1 = top of
    // square, not top of FBO). The std::max(…, 1.0) extends the viewport
    // upward to cover the full FBO in that case — NDC y=+1 then lands at
    // the actual FBO top, no clipping. Gauges in such FBOs render with the
    // taller NDC y range (their pixel position moves accordingly — see
    // ApplyAnchorLayout).
    ret.height = std::max(width * sr_screenWidth / sr_screenHeight, 1.0f);

    return ret;
}

int rViewportConfiguration::CurrentConfNum()
{
    return conf_num;
}

// Rotation degrees applied to a given viewport in a given configuration.
// On mobile, rotates viewports so each player around a tablet sees their
// viewport right-side-up. Top-half viewports get 180° (player across table).
// Left-right viewports get ±90°.
//
// Configs: 0=single, 1=top/bottom, 2=left/right,
//          3=top+BL+BR, 4=TL+TR+bottom, 5=TL+TR+BL+BR
int sr_GetViewportRotationDeg(int confNum, int vpIdx)
{
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    switch (confNum)
    {
    case 1: // top-bottom split
        if (vpIdx == 0) return 180;
        break;
    case 2: // left-right split
        if (vpIdx == 0) return 90;  // left viewport
        if (vpIdx == 1) return 270; // right viewport
        break;
    case 3: // top + bottom-left + bottom-right
        if (vpIdx == 0) return 180;
        break;
    case 4: // top-left + top-right + bottom
        if (vpIdx == 0 || vpIdx == 1) return 180;
        break;
    case 5: // TL + TR + BL + BR
        if (vpIdx == 0 || vpIdx == 1) return 180;
        break;
    }
#else
    (void)confNum;
    (void)vpIdx;
#endif
    return 0;
}


