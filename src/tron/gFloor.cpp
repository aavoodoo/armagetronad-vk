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

#include "rSDL.h"

#include "defs.h"
#include "gStuff.h"
#include "gLogo.h"
#include "eFloor.h"
#include "tConfiguration.h"
#include "rScreen.h"


// grid size
static REAL sg_gridSize=1;
static tSettingItem<REAL> g_s("GRID_SIZE",sg_gridSize);
static REAL sg_gridSizeMoviePack=2;
static tSettingItem<REAL> g_sm("GRID_SIZE_MOVIEPACK",sg_gridSizeMoviePack);

static REAL moviepack_floor_red=.5,moviepack_floor_green=.5,moviepack_floor_blue=.5;
static REAL floor_red=.15,floor_green=.3,floor_blue=.15;

static tSettingItem<REAL>
mfr("MOVIEPACK_FLOOR_RED",moviepack_floor_red),
mfg("MOVIEPACK_FLOOR_GREEN",moviepack_floor_green),
mfb("MOVIEPACK_FLOOR_BLUE",moviepack_floor_blue),
fr("FLOOR_RED",floor_red),
fg("FLOOR_GREEN",floor_green),
fb("FLOOR_BLUE",floor_blue);

#ifndef DEDICATED
#include "rTexture.h"
#include "rRender.h"
#include "uMenu.h"
#include "tSysTime.h"
#include "rVertex.h"
#include "rRenderQueue.h"
#include "rRenderBucket.h"
#include "gMoviepack.h"
#include <vector>

#include "nConfig.h"
/*
static tString lala_floor_a("Anonymous/original/textures/floor_a.png");
static nSettingItem<tString> lalala_floor_a("TEXTURE_FLOOR_A", lala_floor_a);
rFileTexture floor_a(rTextureGroups::TEX_FLOOR, lala_floor_a, 1,1);

static tString lala_floor_b("Anonymous/original/textures/floor_b.png");
static nSettingItem<tString> lalala_floor_b("TEXTURE_FLOOR_B", lala_floor_b);
rFileTexture floor_b(rTextureGroups::TEX_FLOOR, lala_floor_b, 1,1);

static tString lala_mp_floor_a("Anonymous/original/moviepack/floor_a.png");
static nSettingItem<tString> lalala_mp_floor_a("TEXTURE_MP_FLOOR_A", lala_mp_floor_a);
rFileTexture mp_floor_a(rTextureGroups::TEX_FLOOR, lala_mp_floor_a, 1,1,true);

static tString lala_mp_floor_b("Anonymous/original/moviepack/floor_b.png");
static nSettingItem<tString> lalala_mp_floor_b("TEXTURE_MP_FLOOR_B", lala_mp_floor_b);
rFileTexture mp_floor_b(rTextureGroups::TEX_FLOOR, lala_mp_floor_b, 1,1,true);

static tString lala_floor("Anonymous/original/textures/floor.png");
static nSettingItem<tString> lalala_floor("TEXTURE_FLOOR", lala_floor);
rFileTexture ArmageTron_floor(rTextureGroups::TEX_FLOOR, lala_floor, 1,1);

static tString lala_mp_floor("Anonymous/original/moviepack/floor.png");
static nSettingItem<tString> lalala_mp_floor("TEXTURE_MP_FLOOR", lala_mp_floor);
rFileTexture ArmageTron_mp_floor(rTextureGroups::TEX_FLOOR, lala_mp_floor, 1,1);
*/

static rFileTexture floor_a(rTextureGroups::TEX_FLOOR,"textures/floor_a.png",1,1);
static rFileTexture floor_b(rTextureGroups::TEX_FLOOR,"textures/floor_b.png",1,1);
static rFileTexture mp_floor_a(rTextureGroups::TEX_FLOOR,"moviepack/floor_a.png",1,1);
static rFileTexture mp_floor_b(rTextureGroups::TEX_FLOOR,"moviepack/floor_b.png",1,1);
rFileTexture ArmageTron_floor(rTextureGroups::TEX_FLOOR,"textures/floor.png",1,1);
rFileTexture ArmageTron_mp_floor(rTextureGroups::TEX_FLOOR,"moviepack/floor.png",1,1);

// Reset moviepack floor textures (for moviepack switching)
void gFloor_ResetTextures()
{
    mp_floor_a.Unload();
    mp_floor_b.Unload();
    ArmageTron_mp_floor.Unload();
    // Also unload regular floor textures so they reload properly when switching to "None"
    floor_a.Unload();
    floor_b.Unload();
    ArmageTron_floor.Unload();
}

class gFloor: public eFloor{
public:
    gFloor(){};
    virtual ~gFloor(){};

    virtual void SetFloorColor(REAL alpha, REAL intens){
        if (!sr_glOut)
            return;

        REAL r, g, b;
        FloorColor(r, g, b);
        renderer->Color(r, g, b, alpha);
    }

    virtual void FloorColor(REAL& r, REAL& g, REAL&b){
        if (sg_MoviePack())
        {
            r = moviepack_floor_red;
            g = moviepack_floor_green;
            b = moviepack_floor_blue;
        }
        else
        {
            r = floor_red;
            g = floor_green;
            b = floor_blue;
        }
    }


    // For each floor texture: prefer the moviepack-shipped variant when the
    // active pack actually provides the file, fall back to the default
    // textures/<file>.png otherwise. Lets minimal moviepacks (e.g. shader-
    // only customisations) skip shipping textures they don't touch.
    virtual void SelectFloorTexture(){
        if (sg_MoviepackHasFile("floor.png"))
            ArmageTron_mp_floor.Select();
        else
            ArmageTron_floor.Select();
    }

    virtual void SelectFloorTextureA(){
        if (sg_MoviepackHasFile("floor_a.png"))
            mp_floor_a.Select();
        else
            floor_a.Select();
    }

    virtual void SelectFloorTextureB(){
        if (sg_MoviepackHasFile("floor_b.png"))
            mp_floor_b.Select();
        else
            floor_b.Select();
    }

    virtual REAL GridSize(){
        if (sg_MoviePack())
            return sg_gridSizeMoviePack;
        else
            return sg_gridSize;
    }

    virtual bool BlackSky(){
        return sg_MoviePack();
    }

};

static gFloor GFLOOR;

static void MenuBackground(){
    if (rTextureGroups::TextureMode[rTextureGroups::TEX_FLOOR]>=0){
        se_SelectFloorTexture();
        se_SetFloorColor(1,1);

        // Get currently bound texture ID
        unsigned int textureId = RenderGetBoundTexture2D();

        // Force texture to repeat mode for tiling
        RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapS, rGLConst::Repeat);
        RenderTexParameter(rGLConst::Texture2D, rGLConst::TextureWrapT, rGLConst::Repeat);

        // Get floor color for tinting (dark greenish-blue)
        REAL floorR, floorG, floorB;
        se_FloorColor(floorR, floorG, floorB);
        uint8_t colorR = static_cast<uint8_t>(floorR * 255.0f);
        uint8_t colorG = static_cast<uint8_t>(floorG * 255.0f);
        uint8_t colorB = static_cast<uint8_t>(floorB * 255.0f);

        // Calculate animated texture offset (same as original)
        double x1=tSysTimeFloat()/3.0;
        double y1=tSysTimeFloat()/5.0;
        REAL width=16;
        REAL height=12;

        // Build rotation/scale matrix (same as original)
        float tm[4][4]={{.8f,.2f,0,0},
                        {-.2f,.8f,0,0},
                        {0,0,1,0},
                        {0,0,0,1}};

        REAL aspectScale = (sr_screenWidth*3.0)/(sr_screenHeight*4.0);
        tm[0][0] *= aspectScale;
        tm[0][1] *= aspectScale;

        // Calculate wrapped animated offset (same as original)
        // Transform offset by rotation matrix, wrap to 0-1, then inverse transform
        double x2 = x1*tm[0][0] + y1*tm[1][0];
        double y2 = x1*tm[0][1] + y1*tm[1][1];
        x2 -= floor(x2);
        y2 -= floor(y2);
        REAL offsetX = x2*tm[1][1] - y2*tm[1][0];
        REAL offsetY = -x2*tm[0][1] + y2*tm[0][0];
        const REAL det = 1.0f/(tm[0][0]*tm[1][1] - tm[0][1]*tm[1][0]);
        offsetX *= det;
        offsetY *= det;

        // Build combined matrix: M = tm * translate(offset) * scale(width, -height)
        // C++ row-major storage interpreted as OpenGL column-major:
        // - Row 0 becomes column 0
        // - Row 3 becomes column 3 (translation)
        // OpenGL matrix layout:
        //   [tm[0][0]*width   tm[1][0]*(-h)  0  tx]   where tx = tm[0][0]*x + tm[1][0]*y
        //   [tm[0][1]*width   tm[1][1]*(-h)  0  ty]   where ty = tm[0][1]*x + tm[1][1]*y
        //   [0                0              1  0 ]
        //   [0                0              0  1 ]
        float finalTm[4][4];

        // Row 0 -> OpenGL column 0
        finalTm[0][0] = tm[0][0] * width;
        finalTm[0][1] = tm[0][1] * width;
        finalTm[0][2] = 0;
        finalTm[0][3] = 0;

        // Row 1 -> OpenGL column 1
        finalTm[1][0] = tm[1][0] * (-height);
        finalTm[1][1] = tm[1][1] * (-height);
        finalTm[1][2] = 0;
        finalTm[1][3] = 0;

        // Row 2 -> OpenGL column 2
        finalTm[2][0] = 0;
        finalTm[2][1] = 0;
        finalTm[2][2] = 1;
        finalTm[2][3] = 0;

        // Row 3 -> OpenGL column 3 (translation, transformed by rotation)
        finalTm[3][0] = tm[0][0] * offsetX + tm[1][0] * offsetY;
        finalTm[3][1] = tm[0][1] * offsetX + tm[1][1] * offsetY;
        finalTm[3][2] = 0;
        finalTm[3][3] = 1;

        // Generate fullscreen quad with 0-1 UVs and floor color tint
        std::vector<rVertex20> vertices;
        vertices.reserve(6);

        // Triangle 1 (bottom-left, bottom-right, top-right)
        vertices.push_back(rVertex20(-1.0f, -1.0f, 0.0f, colorR, colorG, colorB, 255, 0.0f, 0.0f));
        vertices.push_back(rVertex20(1.0f, -1.0f, 0.0f, colorR, colorG, colorB, 255, 1.0f, 0.0f));
        vertices.push_back(rVertex20(1.0f, 1.0f, 0.0f, colorR, colorG, colorB, 255, 1.0f, 1.0f));

        // Triangle 2 (bottom-left, top-right, top-left)
        vertices.push_back(rVertex20(-1.0f, -1.0f, 0.0f, colorR, colorG, colorB, 255, 0.0f, 0.0f));
        vertices.push_back(rVertex20(1.0f, 1.0f, 0.0f, colorR, colorG, colorB, 255, 1.0f, 1.0f));
        vertices.push_back(rVertex20(-1.0f, 1.0f, 0.0f, colorR, colorG, colorB, 255, 0.0f, 1.0f));

        // Create render state with texture AND texture matrix stored in the state key
        // This is critical because RenderTriangles uses the state key's matrix, not the renderer's stack
        rRenderStateKey state = rRenderStateKey::HUDWithTexMatrix(textureId, rBlendMode::Alpha, &finalTm[0][0]);

        // Set up 2D orthographic projection for fullscreen quad
        ModelMatrix();
        IdentityMatrix();

        ProjMatrix();
        IdentityMatrix();

        // Submit to render queue for deferred rendering
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, vertices.data(), vertices.size());

        // Must execute immediately - there's no other place that executes HUD phase
        rRenderQueue::Instance().ExecutePhase(rRenderPhase::HUD);
    }

    gLogo::Display();
}

static uCallbackMenuBackground backgr(&MenuBackground);

#endif
