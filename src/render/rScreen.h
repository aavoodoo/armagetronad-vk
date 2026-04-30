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

#ifndef ArmageTron_SCREEN_H
#define ArmageTron_SCREEN_H

#include "tString.h"
#include "tCallback.h"
#include "tCallbackString.h"
#ifndef DEDICATED
#include "rSDL.h"
#endif

typedef enum {
    ArmageTron_Desktop=0,ArmageTron_320_200,ArmageTron_Min=ArmageTron_320_200, ArmageTron_320_240,ArmageTron_400_300,
    ArmageTron_512_384,ArmageTron_640_480,ArmageTron_800_600,
    ArmageTron_1024_768,ArmageTron_1280_800,ArmageTron_1280_854,ArmageTron_1280_1024,
    ArmageTron_1600_1200,ArmageTron_1680_1050,ArmageTron_2048_1572,ArmageTron_Custom, ArmageTron_Invalid=-1
}
rResolution;

typedef enum {
    ArmageTron_ColorDepth_16, ArmageTron_ColorDepth_Desktop,
    ArmageTron_ColorDepth_32
}
rColorDepth;

typedef enum {
    ArmageTron_VSync_On, ArmageTron_VSync_Default, ArmageTron_VSync_Off,
    ArmageTron_VSync_MotionBlur
}
rVSync;

struct rScreenSize
{
    rResolution         res;
    int                 width, height;

    rScreenSize( int width, int height ); //!< constructor
    explicit rScreenSize( rResolution r = ArmageTron_Invalid ); //!< constructor
    void UpdateSize();                                          //!< update size from res enum

    bool operator ==( rScreenSize const & other ) const; //!< comparison operator
    bool operator !=( rScreenSize const & other ) const; //!< comparison operator

    int Compare( rScreenSize const & other ) const; //!< comparison function
};

class rScreenSettings
{
public:
    rScreenSize			res;
    rScreenSize			windowSize;
    bool				fullscreen;
#ifdef MACOSX
    bool                lowDPIWindow{};
#endif    
    rColorDepth			colorDepth;
    rColorDepth			zDepth;
    bool				checkErrors;
    int                 displayIndex;   // display to use
    int                 refreshRate;    // screen refresh rate
    rVSync              vSync;          // whether to wait for vsync (GL only)
    REAL				aspect;			// aspect ratio of pixels ( width/height )
    int                 presentMode;    // Vulkan present mode (0=Auto, 1=VSync, 2=Immediate, 3=Mailbox)

    rScreenSettings(rResolution r,
                    bool fs=true,
                    rColorDepth cd=ArmageTron_ColorDepth_Desktop,
                    bool ce =true);
};

bool sr_DesktopScreensizeSupported();

extern rScreenSettings currentScreensetting;
extern rScreenSettings lastSuccess;

#ifndef DEDICATED
// Platform-specific screen handle (SDL3)
struct SDL_Window;
struct SDL_Renderer;
extern SDL_Window   *sr_screen;
#else
// Stub for dedicated server - always nullptr (no screen)
static void* const sr_screen = nullptr;
#endif // DEDICATED

// screen/window dimensions in pixels
extern int sr_screenWidth,sr_screenHeight;

extern bool sr_alphaBlend;
extern bool sr_screenshotIsPlanned;
extern bool sr_smoothShading;

#define rSHADOW_OFF       0
#define rSHADOW_LEGACY    1
#define rSHADOW_MAP       2

extern int sr_shadowMode;

//! Returns the fraction of the screen height occupied by the on-screen keyboard
//! (0.0 when hidden). Used to shift the rendering viewport upward.
REAL sr_ScreenKeyboardHeightFraction();

//! Returns a UI scale multiplier for touch-friendly menu sizing on mobile.
//! On small screens (phones), returns > 1.0 to enlarge menu items.
//! On tablets and desktops, returns 1.0 (no scaling).
REAL sr_TouchUIScale();

extern bool sr_glOut;           // do we have gl-output at all?
extern bool sr_textOut;          // display game text graphically?
extern bool sr_FPSOut;           // display frame counter?

//! Display list usage is disabled in GL3 renderer (legacy enum kept for compatibility)
enum rDisplayListUsage
{
    rDisplayList_Off=0, // not at all (only mode in GL3)
    rDisplayList_CAC,   // deprecated
    rDisplayList_CAE,   // deprecated
    rDisplayList_Count
};

extern rDisplayListUsage sr_useDisplayLists;   // Always rDisplayList_Off in GL3


#define rMIRROR_OFF     0
#define rMIRROR_OBJECTS 1
#define rMIRROR_WALLS   2
#define rMIRROR_ALL     10

extern int sr_floorMirror;

#define rFLOOR_OFF        0
#define rFLOOR_GRID       1
#define rFLOOR_TEXTURE    2
#define rFLOOR_TWOTEXTURE 3

extern int sr_floorDetail;

#define rFEAT_OFF    -1
#define rFEAT_DEFAULT 0
#define rFEAT_ON      1

extern bool sr_highRim;
extern bool sr_upperSky,sr_lowerSky;
extern bool sr_skyWobble;
extern bool sr_dither;
extern bool sr_infinityPlane;
extern bool sr_laggometer;
extern bool sr_predictObjects;
bool sr_ShouldPredictObjects();  //!< Returns sr_predictObjects, but forced false when spectating/dead
extern bool sr_texturesTruecolor;
extern bool sr_keepWindowActive;

extern tString renderer_identification;  // type of renderer used

extern tString gl_vendor;
extern tString gl_renderer;
extern tString gl_version;
extern tString gl_extensions;

class rPerFrameTask:public tCallback{
public:
    rPerFrameTask(AA_VOIDFUNC *f);
    static void DoPerFrameTasks();
};


class rRenderIdCallback:public tCallbackString{
public:
    rRenderIdCallback(STRINGRETFUNC *f);
    static tString RenderId();
};

class rCallbackBeforeScreenModeChange:public tCallback{
public:
    rCallbackBeforeScreenModeChange(AA_VOIDFUNC *f);
    static void Exec();
};

class rCallbackAfterScreenModeChange:public tCallback{
public:
    rCallbackAfterScreenModeChange(AA_VOIDFUNC *f);
    static void Exec();
};

bool sr_InitDisplay();
void sr_ExitDisplay();
void sr_ReinitDisplay();
void sr_GetDrawableSize();

void sr_LoadDefaultConfig();

void sr_ResetRenderState(bool menu=0);
void sr_DepthOffset(bool offset);
void sr_Activate(bool active); // set activation staus

void sr_SetWindowTitle(tOutput o);
void sr_SetWindowTitle(tString s);
void sr_SetWindowTitle();

#ifndef DEDICATED
void sr_LockSDL();
void sr_UnlockSDL();
#else
// Stub implementations for dedicated server
inline void sr_LockSDL() {}
inline void sr_UnlockSDL() {}
#endif // DEDICATED

#endif
