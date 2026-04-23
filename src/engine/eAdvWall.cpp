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

#include "eWall.h"
#include "math.h"
#include "rTexture.h"
#include "rScreen.h"
#include "eAdvWall.h"
#include "eCamera.h"
#include "tConfiguration.h"
#include "eRectangle.h"
#include "rRender.h"
#include "../tron/gWall.h"

#include <vector>

/* **********************************************
   RimWall
   ********************************************** */

static eRectangle se_rimWallBounds;
tList<eWallRim> se_rimWalls;

eWallRim::eWallRim(eGrid *grid, bool backface_cull, REAL h)
        :eWall(grid), rim_id(-1),bf_cull(backface_cull), height(h)
{
    if (h>100 && !sr_highRim)
        height=4;

    //  if(bf_cull && sr_ZTrick)
    se_rimWalls.Add(this,rim_id);
    DestroyDisplayList();}


eWallRim::~eWallRim()
{
    se_rimWalls.Remove(this,rim_id);
    DestroyDisplayList();
}

//ArmageTron_eWalltype gWallRim::type(){return ArmageTron_RIM;}

//void eWallRim::Flip(){}

bool eWallRim::Splittable() const
{
    return 0;
}

static inline eRectangle se_OffsetBounds( REAL offset )
{
    return eRectangle( se_rimWallBounds.GetLow()  + eCoord(offset,offset),
                       se_rimWallBounds.GetHigh() - eCoord(offset,offset) );
}

// *******************************************************************************************
// *
// *    Bound
// *
// *******************************************************************************************
//!
//!        @param  x       point to be clamped to the inside of the rim walls
//!        @param  offset  extra distance kept to the walls
//!        @return         if positive, that's the distance moved. If negative, it's the
//!                        distance to the rim.
//!
// *******************************************************************************************

REAL eWallRim::Bound(eCoord &x,REAL offset)
{
    //return -1E+30;
    return se_OffsetBounds( offset ).Clamp( x );
}

// *******************************************************************************************
// *
// *    IsBound
// *
// *******************************************************************************************
//!
//!        @param  x       point to be checked
//!        @param  offset  extra distance kept to the walls
//!        @return         true if the point is inside the rim
//!
// *******************************************************************************************

bool eWallRim::IsBound(const eCoord &x, REAL offset)
{
    // return true;
    return se_OffsetBounds( offset ).Contains( x );
}

static bool se_RimWrapY=true;
static tSettingItem<bool> se_RimWrapYConf
("RIM_WALL_WRAP_Y",se_RimWrapY);

#ifndef DEDICATED

extern bool sg_MoviePack();

static rFileTexture se_RimWallNoWrap(rTextureGroups::TEX_WALL,"textures/rim_wall.png",1,0);
static rFileTexture se_RimWallWrap(rTextureGroups::TEX_WALL,"textures/rim_wall.png",1,1);

#endif

// *******************************************************************************************
// *
// *    RenderAll
// *
// *******************************************************************************************
//!        @param  camera  camera used for rendering
// *******************************************************************************************
void eWallRim::RenderAll( eCamera * camera )
{
#ifndef DEDICATED
    RenderEnableState(rCapability::DepthTest);
    RenderDisableState(rCapability::CullFace);

    if ( !sg_MoviePack() )
    {
        ( se_RimWrapY ? se_RimWallWrap : se_RimWallNoWrap).Select();
    }

    // Collect all rim wall geometry into batch accumulators
    gWallRim_BeginBatch();

    for(int i=se_rimWalls.Len()-1;i>=0;i--){
        se_rimWalls(i)->RenderReal( camera );
    }

    // Submit and render all accumulated geometry
    rITexture* defaultTex = sg_MoviePack() ? nullptr
        : &( se_RimWrapY ? se_RimWallWrap : se_RimWallNoWrap );
    gWallRim_FlushBatch(defaultTex);

    RenderEnableState(rCapability::CullFace);
#endif
}

// *******************************************************************************************
// *
// *    DestroyDisplayList
// *
// *******************************************************************************************
void eWallRim::DestroyDisplayList( int inhibitGeneration )
{
#ifndef DEDICATED
    (void)inhibitGeneration;
    // No cached state to invalidate — rim walls submit to render queue each frame
#endif
}

static rCallbackBeforeScreenModeChange unload(&eWallRim::DestroyDisplayList);

// *******************************************************************************************
// *
// *	Clip
// *
// *******************************************************************************************
//!
//!		@param	in		point inside of the rim
//!		@param	out		point possibly outside of the rim to correct
//!		@param	offset	extra distance kept to the walls
//!		@return			fraciton of line in->out left (1 for no clipping)
//!
// *******************************************************************************************

REAL eWallRim::Clip( const eCoord & in, eCoord & out, REAL offset )
{
    //return 1;
    return se_OffsetBounds( offset ).Clip(in, out);
}

// ****************************************************************************
// *
// *	GetBounds
// *
// ****************************************************************************
//!
//!		@return
//!
// ****************************************************************************

const eRectangle & eWallRim::GetBounds( void )
{
    // NOTE: se_rimWallBounds is accessed from the camera/render path (read) and
    // updated from the game loop via UpdateBounds() (write). Both calls happen
    // on the same thread (ArmagetronAD has a single game/render thread), so no
    // synchronization is needed. If threading is added later, add a mutex here.
    return se_rimWallBounds;
}

std::vector<eCoord> se_rimWallRubberBand;

// ****************************************************************************
// *
// *	UpdateBounds
// *
// ****************************************************************************
//!
//!
// ****************************************************************************

void eWallRim::UpdateBounds( void )
{
    std::vector<tCoord> allPoints;

    // calculate bounding box
    se_rimWallBounds.Clear();
    for(int i = se_rimWalls.Len()-1; i>=0; --i )
    {
        eWall * w = se_rimWalls(i);
        se_rimWallBounds.Include(w->EndPoint(0)).Include(w->EndPoint(1));
        allPoints.push_back(w->EndPoint(0));
        allPoints.push_back(w->EndPoint(1));
    }

    //calculate the bounding polygon of the map
    tCoord::GrahamScan(allPoints, se_rimWallRubberBand);
}


