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

//void se_FetchAndStoreSDLInput();

#include "rSDL.h"

#include "tConfiguration.h"
#include <cstring>
#include <cmath>
#include <algorithm>

// floor mirror
#ifndef DEDICATED
static REAL sr_floorMirror_strength=.1;
static tSettingItem<REAL> f_m("FLOOR_MIRROR_INT",sr_floorMirror_strength);

#include "tEventQueue.h"
#include "uInputQueue.h"
#include "eTess2.h"
#include "rTexture.h"
#include "eGameObject.h"
#include "rFont.h"
#include "eTimer.h"
#include "eCamera.h"
#include "eSensor.h"
#include "rScreen.h"
#include "rRender.h"
#include "rCycleRenderer.h"
#include "rSkyFloorRenderer.h"
#include "rZoneRenderer.h"
#include "rRendererState.h"
#include "rRenderQueue.h"
#include "rVertex.h"
#include "eWall.h"
#include "eAdvWall.h"
#include "eFloor.h"
#include "ePath.h"
#include "eGrid.h"
#include "eDebugLine.h"
#include "tDirectories.h"
#include "eRectangle.h"

#define eWall_h 4
#define view_h 2.7

#ifdef DEBUG
bool debug_grid=0;
#endif

REAL se_upperSkyHeight=100;
REAL se_lowerSkyHeight=50;

static tSettingItem<REAL> sec_upperSkyHeight("UPPER_SKY_HEIGHT",se_upperSkyHeight);
static tSettingItem<REAL> sec_lowerSkyHeight("LOWER_SKY_HEIGHT",se_lowerSkyHeight);

#ifndef DEDICATED

extern bool sg_MoviePack();

// select the lower sky
static rFileTexture & se_Sky()
{
    static char const * skyPath="textures/sky.png";
    static char const * skyPathMoviepack="moviepack/sky.png";
    static rFileTexture sky(rTextureGroups::TEX_FLOOR,skyPath,1,1,true);
    static rFileTexture sky_moviepack(rTextureGroups::TEX_FLOOR,skyPathMoviepack,1,1,true);

    if (sg_MoviePack()){
        // Since old movie packs usually don't include sky.png we need to
        // be nice and fall back to the default sky tecture. -k
        tString s = tDirectories::Data().GetReadPath( skyPathMoviepack );
        if(strlen(s) > 0)
            return sky_moviepack;
    }

    return sky;
}

static void se_SelectSky()
{
    static rFileTexture & sky = se_Sky();
    sky.Select();
}

static REAL se_upperSkyScale=1;
static REAL se_upperSkyColorR=.5;
static REAL se_upperSkyColorG=.5;
static REAL se_upperSkyColorB=1;
static tSettingItem<REAL> sec_upperSkyScale("UPPER_SKY_SCALE",se_upperSkyScale);
static tSettingItem<REAL> sec_upperSkyColorR("UPPER_SKY_RED",se_upperSkyColorR);
static tSettingItem<REAL> sec_upperSkyColorG("UPPER_SKY_GREEN",se_upperSkyColorG);
static tSettingItem<REAL> sec_upperSkyColorB("UPPER_SKY_BLUE",se_upperSkyColorB);

// select the upper sky
static rFileTexture * se_UpperSky()
{
    static char const * skyPath="textures/upper_sky.png";
    static char const * skyPathMoviepack="moviepack/upper_sky.png";
    static rFileTexture sky(rTextureGroups::TEX_FLOOR,skyPath,1,1,true);
    static rFileTexture sky_moviepack(rTextureGroups::TEX_FLOOR,skyPathMoviepack,1,1,true);

    if (sg_MoviePack()){
        tString s = tDirectories::Data().GetReadPath( skyPathMoviepack );
        if(s.Len() > 1)
            return &sky_moviepack;
    }

    if( tDirectories::Data().GetReadPath( skyPath ).Len() > 1 ){
        return &sky;
    }

    return NULL;
}

static void se_SelectUpperSky()
{
    static rFileTexture * sky = se_UpperSky();
    if( sky )
    {
        sky->Select();
    }
    else
    {
        se_SelectFloorTexture();
    }
}

// if the rip bug is activated, don't use the rim to draw the floor
extern short se_bugRip;

// renders a finite rectangle
// texScaleU/V override the texture scale (0 = use default 1/gridSize)
static void finite_xy_plane( const eCoord &pos,const eCoord &dir,REAL h, eRectangle rect,
                             rBlendMode blend = rBlendMode::Alpha,
                             float texScaleU = 0, float texScaleV = 0 )
{
#ifndef DEDICATED
    // expand plane to camera position to avoid embarrasing reflection bug
    if ( sr_floorMirror )
        rect.Include( pos );

    // fetch rectangle coordinates
    REAL lx = rect.GetLow().x;
    REAL ly = rect.GetLow().y;
    REAL hx = rect.GetHigh().x;
    REAL hy = rect.GetHigh().y;

    // Convert triangle fan to batch rendering
    // Triangle fan: center vertex + ring of 5 vertices
    // Fan vertices: v0 (center), v1(lx,ly), v2(lx,hy), v3(hx,hy), v4(hx,ly), v5(lx,ly)
    // Triangles: (v0,v1,v2), (v0,v2,v3), (v0,v3,v4), (v0,v4,v5)

    // Batch rendering using the same approach as the floor.
    // Query bound texture and current color, build rVertex20 array, submit to queue.
    unsigned int textureId = RenderGetBoundTexture2D();

    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    RenderGetColor(color);
    uint8_t r = static_cast<uint8_t>(color[0] * 255.0f);
    uint8_t g = static_cast<uint8_t>(color[1] * 255.0f);
    uint8_t b = static_cast<uint8_t>(color[2] * 255.0f);
    uint8_t a = static_cast<uint8_t>(color[3] * 255.0f);


    // rVertex20 stores texcoords as int16 (range -1.0..1.0). World-space coords
    // (0-200+) would overflow, so we normalize by worldScale and compensate via
    // texture matrix: shader computes (coord/worldScale) * (worldScale/gridSize) = coord/gridSize
    REAL gridSize = se_GridSize();

    // Center vertex
    REAL cx = pos.x - dir.x;
    REAL cy = pos.y - dir.y;

    // Snap center to grid boundaries so the floor quad vertices land on
    // stable world positions. Without this, the center shifts continuously
    // during camera rotation, causing int16 UV quantization jitter.
    REAL snapSize = gridSize * 4;
    REAL snappedCx = std::floor(cx / snapSize) * snapSize;
    REAL snappedCy = std::floor(cy / snapSize) * snapSize;

    // Recompute rectangle bounds relative to snapped center
    // (expand to ensure the original rect is fully covered)
    lx = std::min(lx, snappedCx - snapSize);
    ly = std::min(ly, snappedCy - snapSize);
    hx = std::max(hx, snappedCx + snapSize);
    hy = std::max(hy, snappedCy + snapSize);
    cx = snappedCx;
    cy = snappedCy;

    // Compute worldScale from all vertex coordinates.
    // Round UP to the next power of 2 for stable quantization.
    REAL worldScale = std::abs(cx);
    worldScale = std::max(worldScale, std::abs(cy));
    worldScale = std::max(worldScale, std::abs(lx));
    worldScale = std::max(worldScale, std::abs(ly));
    worldScale = std::max(worldScale, std::abs(hx));
    worldScale = std::max(worldScale, std::abs(hy));
    if (worldScale < 1.0f) worldScale = 1.0f;
    worldScale = std::pow(2.0, std::ceil(std::log2(worldScale)));

    // Build texture matrix: scale normalized coords back to world-space tiling
    float tsU = texScaleU != 0 ? texScaleU : static_cast<float>(1.0 / gridSize);
    float tsV = texScaleV != 0 ? texScaleV : static_cast<float>(1.0 / gridSize);
    float texMatrix[16] = {0};
    texMatrix[0]  = static_cast<float>(worldScale) * tsU;
    texMatrix[5]  = static_cast<float>(worldScale) * tsV;
    texMatrix[10] = 1.0f;
    texMatrix[15] = 1.0f;

    // Build a 2x2 grid of quads centered at (cx, cy) instead of a triangle fan.
    // A fan creates a singularity at the center vertex where all triangles meet,
    // causing discontinuous texture gradients and mipmap artifacts (dark spot).
    // The grid eliminates the singularity while still wrapping around the camera.
    float invWS = static_cast<float>(1.0 / worldScale);
    auto mkv = [&](float x, float y) -> rVertex20 {
        return rVertex20(x, y, h, r, g, b, a, x * invWS, y * invWS);
    };

    // 5 unique X coordinates, 5 unique Y coordinates → 3x3 grid of vertices
    // Center column/row at (cx, cy), edges at rectangle bounds
    rVertex20 vBL = mkv(lx, ly), vBC = mkv(cx, ly), vBR = mkv(hx, ly);
    rVertex20 vML = mkv(lx, cy), vMC = mkv(cx, cy), vMR = mkv(hx, cy);
    rVertex20 vTL = mkv(lx, hy), vTC = mkv(cx, hy), vTR = mkv(hx, hy);

    // 4 quads → 8 triangles → 24 vertices
    rVertex20 verts[24] = {
        vBL, vBC, vMC,  vBL, vMC, vML,  // bottom-left quad
        vBC, vBR, vMR,  vBC, vMR, vMC,  // bottom-right quad
        vML, vMC, vTC,  vML, vTC, vTL,  // top-left quad
        vMC, vMR, vTR,  vMC, vTR, vTC,  // top-right quad
    };

    // Submit with texture
    rRenderStateKey state;
    if (textureId != 0)
    {
        state = rRenderStateKey::Textured(textureId, blend);
        state.SetTexMatrix(texMatrix);
    }
    else
    {
        state = rRenderStateKey::Colored(blend);
    }
    rRenderQueue::Instance().Submit(rRenderPhase::Sky, state, verts, 24);
#endif
}

static void infinity_xy_plane(eCoord const & pos, const eCoord &dir,REAL h=0,
                              rBlendMode blend = rBlendMode::Alpha,
                              float texScaleU = 0, float texScaleV = 0){
    bool use_rim = !sr_infinityPlane;

    if ( se_bugRip )
        use_rim=false;

    if (use_rim){
        finite_xy_plane( pos, dir, h, eWallRim::GetBounds(), blend, texScaleU, texScaleV );
    }
    else
    {
#ifndef DEDICATED
        // The old GL1 code used projective ring vertices (±1.0 world coords) that
        // only worked as a fixed-function "infinity" trick.  In the modern renderer
        // those coords are treated as real world-space positions (≈origin), making
        // the floor invisible.  Simulate infinity with a large-radius rectangle
        // centered on the camera position, extending well beyond the arena bounds.
        const tRectangle& bounds = eWallRim::GetBounds();
        REAL maxEdge = std::max({
            std::abs(bounds.GetLow().x), std::abs(bounds.GetLow().y),
            std::abs(bounds.GetHigh().x), std::abs(bounds.GetHigh().y),
            1.0f
        });
        REAL largeRadius = maxEdge * 100.0f;
        eRectangle largeRect;
        largeRect.Include( eCoord(pos.x - largeRadius, pos.y - largeRadius) );
        largeRect.Include( eCoord(pos.x + largeRadius, pos.y + largeRadius) );
        finite_xy_plane( pos, dir, h, largeRect, blend, texScaleU, texScaleV );
#endif
    }
}

static REAL z=0;

/* Try to get ride of these functions as it seems useless to use them instead of the cam parameter itself
int           eGrid::NumberOfCameras(){return cameras.Len();}
const eCoord& eGrid::CameraPos(int i){return cameras(i)->CameraPos();}
eCoord eGrid::CameraGlancePos(int i){return cameras(i)->CameraGlancePos();}
const eCoord& eGrid::CameraDir(int i){return cameras(i)->CameraDir();}
REAL          eGrid::CameraHeight(int i){return cameras(i)->CameraZ();}
*/



class eZNearSensor: public eSensor
{
public:
    static bool AdaptZNear( REAL & zNear, eWall const * wall, eCamera const * camera )
    {
        REAL len = wall->Len();
        if ( len > .01)
        {
            REAL zDist = ::z - wall->Height();
            if ( zDist < zNear )
            {
                const eCoord& camPos = camera->CameraPos();
                const eCoord& camDir = camera->CameraDir();
                eCoord base = wall->EndPoint(0);
                eCoord end = wall->EndPoint(1);
                
                if ( eCoord::F( base-camPos, camDir ) > 0.01f || eCoord::F( end-camPos, camDir ) > 0.01f )
                {
                    eCoord dirNorm = end - base;
                    dirNorm.Normalize();
                    eCoord camRelative = ( camPos - base ).Turn( dirNorm.Conj() );
                    REAL dist = fabs( camRelative.y );
                    if ( camRelative.x < 0 )
                    {
                        dist -= camRelative.x;
                    }
                    if ( camRelative.x > len )
                    {
                        dist += camRelative.x - len;
                    }
                    if ( dist < zDist )
                    {
                        dist = zDist;
                    }
                    // TODO: better criterion for ingoring of walls
                    if ( dist < zNear && dist > 0.001f )
                    {
                        zNear = dist;
                        return true;
                    }
                }
            }
        }

        return false;
    }

    eZNearSensor(eGameObject *o,const eCoord &start,const eCoord &d, REAL & zNear, eCamera const * camera )
    :eSensor( o, start, d ), zNear_( zNear ), camera_( camera )
    {}

    void Detect()
    {
        detect( zNear_ * 2 );
    }

    // called when passing an edge
    ePassEdgeResult PassEdge( const eWall * w, REAL time, REAL, int) override
    {
        // adapt zNear and be done
        if( AdaptZNear( zNear_, w, camera_ ) )
        {
            return eAbort;
        }

        return eContinue;
    }
private:
    REAL & zNear_; // the reference to the near clipping plane value
    eCamera const * camera_; // the camera currently in use
};

void paint_sr_lowerSky(eGrid *grid, int viewer,bool sr_upperSky, eCamera* cam ){
    // TODO: Sky texture scale (.005) and wobble effect are not yet passed to
    // infinity_xy_plane's batch texture matrix. Currently uses gridSize-based scale.
    se_SelectSky();

    REAL sa=(se_lowerSkyHeight-z)*.1;
    if (sa>1) sa=1;
    if (!sr_upperSky){
        sa=1;
    }
    if (sa>0){
        Color(1,1,1,sa);
        infinity_xy_plane(cam->CameraPos(),cam->CameraDir(),se_lowerSkyHeight);
    }
}

void eGrid::display_simple( eCamera* cam, int viewer,bool floor,
                            bool sr_upperSky,bool sr_lowerSky,
                            REAL flooralpha,
                            bool eWalls,bool gameObjects,
                            REAL& zNear){
    RenderDisableState(rCapability::DepthTest);
    RenderDepthMask(false);

    RenderDisableState(rCapability::CullFace);

    eCoord camPos = cam->CameraGlancePos(); 
    // eWallRim::Bound( camPos, 10 );

    // Set render context for sky
    sr_SetRenderContext(rRenderContext::Game3D_Sky);

    if (sr_upperSky || se_BlackSky()){
        if (se_BlackSky()){
            if (sr_useBatchedSkyFloor && z < se_lowerSkyHeight)
            {
                rSubmitBlackSky(
                    static_cast<float>(cam->CameraPos().x),
                    static_cast<float>(cam->CameraPos().y),
                    static_cast<float>(cam->CameraDir().x),
                    static_cast<float>(cam->CameraDir().y),
                    static_cast<float>(se_lowerSkyHeight));
            }
            else
            {
                Color(0,0,0);
                if ( z < se_lowerSkyHeight )
                    infinity_xy_plane(cam->CameraPos(), cam->CameraDir(), se_lowerSkyHeight);
            }
            rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        }
        else {
            se_SelectUpperSky();
            Color(se_upperSkyColorR,se_upperSkyColorG,se_upperSkyColorB);
            if ( z < se_upperSkyHeight )
                infinity_xy_plane(cam->CameraPos(), cam->CameraDir(), se_upperSkyHeight);
            rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
        }
    }

    if (sr_lowerSky && !sr_highRim){
        paint_sr_lowerSky(this, viewer,sr_upperSky, cam);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
    }

    if (floor){
        // Set render context for floor
        sr_SetRenderContext(rRenderContext::Game3D_Floor);

        // Set arena bounds for shader effects (floor centering)
        const tRectangle& bounds = eWallRim::GetBounds();
        sr_SetArenaBounds(bounds.GetLow().x, bounds.GetLow().y,
                          bounds.GetHigh().x, bounds.GetHigh().y);

        sr_DepthOffset(false);

        su_FetchAndStoreSDLInput();
        int floorDetail = sr_floorDetail;

        // no multitexturing without alpha blending
        if ( !sr_alphaBlend && floorDetail > rFLOOR_TEXTURE )
            floorDetail = rFLOOR_TEXTURE;

        switch(floorDetail){
        case rFLOOR_OFF:
            break;
        case rFLOOR_GRID:
            {
	#define SIDELEN   (se_GridSize())
	#define EXTENSION 10

                eCoord center = cam->CameraPos() + cam->CameraDir() * (SIDELEN * EXTENSION * .8);

                REAL x=center.x;
                REAL y=center.y;
                int xn=static_cast<int>(x/SIDELEN);
                int yn=static_cast<int>(y/SIDELEN);

	#define INTENSITY(x,xx) (1-(((x)-(xx))*((x)-(xx))/(EXTENSION*SIDELEN*EXTENSION*SIDELEN)))

                // Get base floor color
                se_SetFloorColor(1.0, 1.0);
                float floorCol[4];
                RenderGetColor(floorCol);
                uint8_t fr = static_cast<uint8_t>(floorCol[0] * 255.0f);
                uint8_t fg = static_cast<uint8_t>(floorCol[1] * 255.0f);
                uint8_t fb = static_cast<uint8_t>(floorCol[2] * 255.0f);

                std::vector<rVertex20> gridLines;
                gridLines.reserve((2*EXTENSION+1) * 4);
                for(int i=xn-EXTENSION;i<=xn+EXTENSION;i++){
                    REAL intens=INTENSITY(i*SIDELEN,x);
                    if (intens<0) intens=0;
                    uint8_t a = rFloatToU8(intens);
                    gridLines.push_back(rVertex20(i*SIDELEN, y-SIDELEN*(EXTENSION+1), 0, fr, fg, fb, a, 0, 0));
                    gridLines.push_back(rVertex20(i*SIDELEN, y+SIDELEN*(EXTENSION+1), 0, fr, fg, fb, a, 0, 0));
                }
                for(int j=yn-EXTENSION;j<=yn+EXTENSION;j++){
                    REAL intens=INTENSITY(j*SIDELEN,y);
                    if (intens<0) intens=0;
                    uint8_t a = rFloatToU8(intens);
                    gridLines.push_back(rVertex20(x-(EXTENSION+1)*SIDELEN, j*SIDELEN, 0, fr, fg, fb, a, 0, 0));
                    gridLines.push_back(rVertex20(x+(EXTENSION+1)*SIDELEN, j*SIDELEN, 0, fr, fg, fb, a, 0, 0));
                }
                rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Alpha);
                rRenderQueue::Instance().SubmitLines(rRenderPhase::Sky, state, gridLines.data(), gridLines.size());
                rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
            }
            break;

        case rFLOOR_TEXTURE:
            // Texture matrix setup handled by batch state key in infinity_xy_plane
            se_SelectFloorTexture();
            se_SetFloorColor(flooralpha);

            infinity_xy_plane( cam->CameraPos(), cam->CameraDir());
            rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);

            break;

        case rFLOOR_TWOTEXTURE:
            {
                REAL gs = 1.0f / se_GridSize();

                // Original GL: two draws, same geometry, different texture scales.
                // Pass A: normal alpha blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
                // Pass B: additive blend (SRC_ALPHA, ONE) on top at same depth.
                // The additive blend brightens where the second texture's stripes
                // overlap with the first, creating a visible grid pattern.

                // Pass A: horizontal stretch, normal alpha blend
                se_SetFloorColor(flooralpha);
                se_SelectFloorTextureA();
                infinity_xy_plane( cam->CameraPos(), cam->CameraDir(), 0,
                                   rBlendMode::Alpha, 0.01f*gs, gs );
                rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);

                // Pass B: vertical stretch, additive blend on top
                se_SetFloorColor(flooralpha);
                se_SelectFloorTextureB();
                infinity_xy_plane( cam->CameraPos(), cam->CameraDir(), 0,
                                   rBlendMode::Additive, gs, 0.01f*gs );
                rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);
            }
            break;
        }
    }

    // TODO: Lower sky rendering needs investigation - the batch queue's state cache
    // desyncs with the renderer's depth state, causing the sky to be depth-tested away.
    // The sky geometry IS submitted correctly (verified via debug) but is invisible.
    // Needs a proper fix for the state cache sync issue.
    if(eWalls && sr_lowerSky && sr_highRim){
        paint_sr_lowerSky(this, viewer,sr_upperSky, cam);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Sky);

        TexMatrix();
        IdentityMatrix();
        ModelMatrix();
    }

    RenderEnableState(rCapability::DepthTest);
    RenderDepthMask(true);

    TexMatrix();
    IdentityMatrix();
    ModelMatrix();

    if(eWalls){
        {
            // Set render context for rim walls
            sr_SetRenderContext(rRenderContext::Game3D_RimWalls);

            su_FetchAndStoreSDLInput();

            eWallRim::RenderAll( cam );
        }
    }

    if (eWalls){
        // send out sensors to find walls close to the camera
        if ( z < 3 )
        {
            eCamera const * camera = cam;
            if( camera && camera->Center() )
            {
                eCoord dir = camera->CameraDir().Turn(1,.5);
                for(int i = 8; i > 0; --i)
                {
                    dir = dir.Turn(sqrt(.5),sqrt(.5));
                    eZNearSensor s( camera->Center(), camPos, dir, zNear, camera );
                    s.Detect();
                }
            }
        }

    }

    // Floor geometry is now executed immediately within each floor detail case
    // using the Sky phase (no depth test/write) to match the original rendering state.

    if (gameObjects)
    {
        // Set render context for game objects (cycles, walls, etc.)
        // Note: This is a generic context; specific object rendering may set more specific contexts
        sr_SetRenderContext(rRenderContext::Game3D);
        rBeginCycleRendering();
        eGameObject::RenderAll(this, cam);

        // Flush opaque dynamic geometry (legacy shadows, etc.) BEFORE instanced
        // cycle draws so that cycles render on top via depth test.
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::OpaqueDynamic);

        rEndCycleRendering();

        // Execute transparent phase (zones) — set Zones context so the
        // uber shader emissive hook for zones fires. zShape.cpp only
        // SUBMITS to the queue; the actual draw (and the context that
        // reaches the shader) happens here when the phase flushes.
        sr_SetRenderContext(rRenderContext::Game3D_Zones);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Transparent);

        // Execute effects phase (explosions, sparks) - must be done while 3D camera is active
        sr_SetRenderContext(rRenderContext::Game3D_Effects);
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::Effects);
    }

    eDebugLine::Render();
#ifdef DEBUG

    ePath::RenderLast();

    if (debug_grid){
        std::vector<rVertex20> debugLines;
        debugLines.reserve(edges.Len() * 6 + points.Len() * 2);

        int i;
        for(i=edges.Len()-1;i>=0;i--){
            eHalfEdge *e=edges[i];
            uint8_t cr, cg, cb;
            if (e->Face()) { cr=255; cg=255; cb=255; }
            else           { cr=0;   cg=0;   cb=255; }

            debugLines.push_back(rVertex20(e->Point()->x, e->Point()->y, 10, cr, cg, cb, 255, 0, 0));
            debugLines.push_back(rVertex20(e->Point()->x, e->Point()->y, 15, cr, cg, cb, 255, 0, 0));
            debugLines.push_back(rVertex20(e->Point()->x, e->Point()->y, .1f, cr, cg, cb, 255, 0, 0));
            debugLines.push_back(rVertex20(e->other->Point()->x, e->other->Point()->y, .1f, cr, cg, cb, 255, 0, 0));
            debugLines.push_back(rVertex20(e->other->Point()->x, e->other->Point()->y, 10, cr, cg, cb, 255, 0, 0));
            debugLines.push_back(rVertex20(e->other->Point()->x, e->other->Point()->y, 15, cr, cg, cb, 255, 0, 0));
        }

        for(i=points.Len()-1;i>=0;i--){
            ePoint *p=points[i];
            debugLines.push_back(rVertex20(p->x, p->y, 0, 255, 0, 0, 255, 0, 0));
            debugLines.push_back(rVertex20(p->x, p->y, (p->GetRefcount()+1)*5, 255, 0, 0, 255, 0, 0));
        }

        rRenderStateKey state = rRenderStateKey::Colored(rBlendMode::Opaque);
        rRenderQueue::Instance().SubmitLines(rRenderPhase::OpaqueDynamic, state, debugLines.data(), debugLines.size());
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::OpaqueDynamic);
    }
#endif

}
#endif

void eGrid::Render( eCamera* cam, int viewer, REAL& zNear ){
    if (!sr_glOut)
        return;
#ifndef DEDICATED
    ProjMatrix();

    z=cam->CameraZ();
    if ( zNear > z )
    {
        zNear = z;
    }

	if (sr_floorMirror){
		ModelMatrix();
		ScaleMatrix(1,1,-1);

		if (z>10) z=10;
		RenderFrontFace((cam->MirrorView()) ? rFrontFace::CCW : rFrontFace::CW);

		bool us=false;
		bool ls=false;

		if (sr_floorMirror>=rMIRROR_ALL){
			us=sr_upperSky;
			ls=sr_lowerSky;
		}
		else if (sr_floorMirror>=rMIRROR_WALLS){
			if (sr_lowerSky)
				ls=true;
			else if (sr_upperSky)
				us=true;
		}

		cam->SetRenderingMain(false && cam->CameraMain());
		display_simple(cam, viewer,false,
					   us,ls,
					   0,
					   sr_floorMirror>=rMIRROR_WALLS,
					   sr_floorMirror>=rMIRROR_OBJECTS,
					   zNear);
		z=cam->CameraZ();
		RenderFrontFace((cam->MirrorView()) ? rFrontFace::CW : rFrontFace::CCW);
		ModelMatrix();
		ScaleMatrix(1,1,-1);


		cam->SetRenderingMain(true && cam->CameraMain());
		display_simple(cam, viewer,true,
					   sr_upperSky,sr_lowerSky,
					   1-sr_floorMirror_strength,
					   true,true,zNear);

	}
	else
	{
		RenderFrontFace((cam->MirrorView()) ? rFrontFace::CW : rFrontFace::CCW);
		cam->SetRenderingMain(true && cam->CameraMain());
		display_simple(cam, viewer,true,
					   sr_upperSky,sr_lowerSky,
					   1,
					   true,true,zNear);
	}
	
#ifdef EVENT_DEB
    //  for(int i=eEdge_crossing.Len()-1;i>=0;i--){
    //    eEdge_crossing(i)->Render();
    //  }
#endif
#endif
}


//void eEdgeViewer::Render(){}

/*
void eViewerCrossesEdge::Render(){
#ifndef DEDICATED
  ePoint *p1=e->Point();
  ePoint *p2=e->other->Point();

  REAL timeLeft=value-se_GameTime();

  REAL h;

  if (viewer==1){
    if (timeLeft>0){
      h=timeLeft+4;
      Color(0,0,1,.5);
    }
    else{
      h=-timeLeft+4;
      Color(1,0,0,.5);
    }

    //  else
    //Color(1,0,0,.5);

    static rTexture ArmageTron_invis_eWall(rTEX_WALL,"textures/eWall2.png",1,0);
    
    ArmageTron_invis_eWall.Select();
    
    eWall::Render_helper(e,(p1->x+p1->y)/4,(p2->x+p2->y)/4,h,1,4);
  }
#endif
}
*/

#endif


