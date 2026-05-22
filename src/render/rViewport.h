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

#ifndef ArmageTron_VIEWPORT_H
#define ArmageTron_VIEWPORT_H

#define MAX_VIEWPORTS 4

#include "defs.h"
#include "tString.h"
#include "tCoord.h"
#include "tSafePTR.h"

class rViewport{
    REAL left,bottom,width,height;
	rViewport *rootViewport;
public:
	void SetRootViewport(rViewport *root) {rootViewport=root;}
	rViewport *GetRootViewport() {return rootViewport;}
    rViewport(REAL l,REAL b,REAL w,REAL h):left(l),bottom(b),width(w),height(h),rootViewport(0){}
    // create a subviewport of top
    rViewport(rViewport &top,rViewport &sub)
            :left  (top.left+top.width*sub.left),
            bottom(top.bottom+top.height*sub.bottom),
            width(top.width*sub.width),
    		height(top.height*sub.height),
    		rootViewport(0){}

    ~rViewport(){tCHECK_DEST;}

#ifndef DEDICATED
    void Select();
#endif

    void Perspective(REAL fov,REAL zNear=1,REAL zFar=10000000,REAL xShift = 0);

    REAL UpDownFOV(REAL fov);

    //! returns a viewport with normal aspect ratio that coincides with this viewport in the bottom line
    rViewport CorrectAspectBottom() const;

    //! returns a viewport that has the same scale horizontally and vertically
    rViewport EqualAspectBottom() const;

    //! returns the height and width of the viewport
    tCoord GetDimensions() const {return tCoord(width, height);}
    tCoord GetPosition() const {return tCoord(left, bottom);}

    //! True iff (sx, sy) — SDL touch coordinate with y=0 at top — falls
    //! inside this viewport's screen rect. Used by the touch dispatcher
    //! to route a finger event to the right per-viewport cockpit
    //! (replaces FOREACH_COCKPIT in cCockpit::ProcessTouch).
    bool Contains(float sx, float sy) const;

    //! Convert SDL touch (sx, sy) into the cockpit HUD coordinate space
    //! that lives inside *this* viewport — i.e. the (-1, +1) NDC square
    //! that EqualAspectBottom().Select() establishes per viewport.
    //!   - X: viewport-local left edge → -1, right edge → +1.
    //!   - Y: bottom of viewport's EqualAspectBottom square → -1, top → +1.
    //!     The square is vpW × vpW pixels at the bottom of the vpW × vpH
    //!     viewport, so a Y above the square ends up > +1 (a miss).
    //!   - `rotDeg` is the viewport's visual rotation (0/90/180/270);
    //!     this undoes it so the touch lands in the cockpit's
    //!     pre-rotation frame.
    //! Caller must have verified Contains(sx, sy) first.
    void TouchToCockpitHud(float sx, float sy, float& hx, float& hy, int rotDeg) const;

    static rViewport s_viewportFullscreen,
    s_viewportLeft,s_viewportRight,
    s_viewportTop,s_viewportBottom,
    s_viewportTopLeft,s_viewportBottomLeft,
    s_viewportTopRight,s_viewportBottomRight, s_viewportDemonstation;

    static void CorrectViewport(int i, int mp);
    static void CorrectViewports(int mp);
    static void SetDirectionOfCorrection(int vp, int dir);
    static void Update(int mp);

};

extern int  sr_viewportBelongsToPlayer[MAX_VIEWPORTS];
extern int  s_newViewportBelongsToPlayer[MAX_VIEWPORTS];


class rViewportConfiguration{
    rViewport *viewports[MAX_VIEWPORTS];
public:
    const int num_viewports;

    static int next_conf_num;

    rViewportConfiguration(rViewport *first);
    rViewportConfiguration(rViewport *first,rViewport *second);
    rViewportConfiguration(rViewport *first,rViewport *second,rViewport *third);
    rViewportConfiguration(rViewport *first,rViewport *second,rViewport *third,
                           rViewport *forth);
#ifndef DEDICATED
    void Select(int i);
    static void DemonstrateViewport(tString *titles);
#endif
    rViewport * Port(int i);

    static rViewportConfiguration *s_viewportConfigurations[];
    static const int               s_viewportNumConfigurations;
    static char const *            s_viewportConfigurationNames[];

    static rViewportConfiguration *CurrentViewportConfiguration();

    static rViewport * CurrentViewport(int i);

    static void UpdateConf();

    //! Returns the index of the currently active viewport configuration.
    static int CurrentConfNum();
};

//! Returns the rotation in degrees (0/90/180/270) applied to viewport \a vpIdx
//! in configuration \a confNum. Used by touch input to reverse-map coordinates.
int sr_GetViewportRotationDeg(int confNum, int vpIdx);

#endif



