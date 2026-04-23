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

    #include "rTexture.h"

    #include "defs.h"

    #include <string>
    #include "rTexture.h"
    #include "rScreen.h"
    #include "rSysdep.h"
    #include "rConsole.h"
    #include "rViewport.h"
    #ifndef DEDICATED
    #include "rRender.h"
    #include "rRenderQueue.h"
    #endif
    #include "tConfiguration.h"
    #include "tRecorder.h"
    #include "tSysTime.h"

    #ifndef DEDICATED
// #include "../network/nNetwork.h"
    #include "rSDL.h"

    #ifdef POWERPAK_DEB
    #include <PowerPak/powerdraw>
    #endif
    #endif

    #ifdef DEBUG
//#ifdef WIN32
    #define FORCE_WINDOW
//#endif
    #endif

tCONFIG_ENUM( rResolution );
tCONFIG_ENUM( rColorDepth );
tCONFIG_ENUM( rVSync );

#ifndef DEDICATED
// SDL3: always use SDL_Window and SDL_GLContext
SDL_Window   *sr_screen=NULL;
SDL_GLContext sr_glcontext=NULL;

static int default_texturemode = rGLConst::LinearMipmapLinear;
#endif // DEDICATED

rDisplayListUsage sr_useDisplayLists=rDisplayList_Off;

static int width[ArmageTron_Custom+2]  = {0, 320, 320, 400, 512, 640, 800, 1024	, 1280, 1280, 1280, 1600, 1680, 2048,800,320};
static int height[ArmageTron_Custom+2] = {0, 200, 240, 300, 384, 480, 600,  768	,  800,  854, 1024, 1200, 1050, 1572,600,200};
static REAL aspect[ArmageTron_Custom+2]= {1, 1	, 1  , 1  , 1  , 1  , 1	 , 1	,    1,    1, 1   ,    1,    1,    1,1,  1};


// screen/window dimensions in pixels
int sr_screenWidth{960},sr_screenHeight{540};

// screen/window dimensions in whatever the system uses for screen coordinates (points/pixels)
int sr_screenWidthInPoints{960},sr_screenHeightInPoints{540};

REAL sr_ScreenKeyboardHeightFraction()
{
#ifndef DEDICATED
#if defined(__APPLE__) && TARGET_OS_IOS
    // Use the real keyboard height from UIKit notifications (rScreenIOS.mm)
    extern float sr_iOSKeyboardHeightFraction(void);
    float frac = sr_iOSKeyboardHeightFraction();
    if (frac > 0.01f) return frac;
#endif
    extern SDL_Window *sr_screen;
#ifdef __ANDROID__
    // On Android, SDL_ScreenKeyboardShown returns true whenever SDL_StartTextInput
    // is active — including menu string editing, not just chat/console. Only apply
    // the viewport offset when the game explicitly requested the keyboard via the
    // touch overlay (chat/console actions), not for generic menu text input.
    extern bool sr_androidKeyboardExplicit;
    if (sr_screen && SDL_ScreenKeyboardShown(sr_screen))
    {
        if (sr_androidKeyboardExplicit)
            return (sr_screenHeightInPoints < 500) ? 0.55f : 0.40f;
    }
    else
    {
        sr_androidKeyboardExplicit = false; // reset when keyboard dismissed
    }
#else
    // Desktop platforms (macOS, Linux, Windows): no on-screen keyboard.
    // SDL3 may spuriously return true for SDL_ScreenKeyboardShown when
    // SDL_StartTextInput is active (menu editing), so we ignore it entirely.
#endif
#endif
    return 0.0f;
}

REAL sr_TouchUIScale()
{
    // Compute a UI scale multiplier for touch-friendly menus on mobile.
    // Uses the screen diagonal in "points" (logical pixels) as a proxy for
    // physical screen size.
    //
    // Reference values (landscape, points = native pixels / scale factor):
    //   iPhone 16e:      667x375 pts → diag  765  → scale 2.00
    //   iPhone 17 Pro:   874x402 pts → diag  962  → scale 1.60
    //   iPhone 17 PM:    932x430 pts → diag 1026  → scale 1.50
    //   iPad Mini 7:    1133x744 pts → diag 1355  → scale 1.13
    //   iPad Pro 11":   1194x834 pts → diag 1457  → scale 1.05
    //   iPad Pro 13":   1366x1024pts → diag 1708  → scale 1.00
    //
    // Large tablets (diag >= 1500 pts) get 1.0x. Phones get up to 2.0x.
    // Small iPads (Mini) get a mild boost too.
    float diagPts = sqrtf(float(sr_screenWidthInPoints) * sr_screenWidthInPoints
                        + float(sr_screenHeightInPoints) * sr_screenHeightInPoints);
    if (diagPts >= 1500.0f) return 1.0f;
    if (diagPts <= 0.0f) return 1.0f;
    REAL scale = 1500.0f / diagPts;
    if (scale > 2.0f) scale = 2.0f;
    return scale;
}

static tSettingItem<int>  at_ch("CUSTOM_SCREEN_HEIGHT"	, height[ArmageTron_Custom]);
static tSettingItem<int>  at_cw("CUSTOM_SCREEN_WIDTH" 	, width	[ArmageTron_Custom]);
static tSettingItem<REAL> at_ca("CUSTOM_SCREEN_ASPECT" , aspect[ArmageTron_Custom]);

    #define MAXEMERGENCY 7

rScreenSettings lastSuccess(ArmageTron_Desktop, true);
rScreenSettings lastSuccessLowZBuffer(ArmageTron_640_480, false);
rScreenSettings lastSuccessLowColor(ArmageTron_640_480, false);

/*
std::ostream & operator << ( std::ostream & s, rScreenSize const & size )
{
    return s;
}

std::istream & operator >> ( std::istream & s, rScreenSize const & size )
{
    return s;
}
*/

static rScreenSettings em5(ArmageTron_320_240, false, ArmageTron_ColorDepth_16, false);
static rScreenSettings em4(ArmageTron_320_240, false, ArmageTron_ColorDepth_Desktop, false);
static rScreenSettings em3(ArmageTron_640_480, false,ArmageTron_ColorDepth_16);
static rScreenSettings em2(ArmageTron_640_480, true, ArmageTron_ColorDepth_16);
static rScreenSettings em1(ArmageTron_640_480);

static rScreenSettings *emergency[MAXEMERGENCY+2]={ &lastSuccess, &lastSuccess, &lastSuccessLowZBuffer, &lastSuccessLowColor, &em1, &em2 , &em3, &em4, &em5};

    #ifdef DEBUG
rScreenSettings currentScreensetting(ArmageTron_640_480);
    #else
rScreenSettings currentScreensetting(sr_DesktopScreensizeSupported() ? ArmageTron_Desktop : ArmageTron_800_600, true);
    #endif

bool sr_DesktopScreensizeSupported()
{
#ifndef DEDICATED
    // SDL3 always supports desktop screen size
    return true;
#else
    return false;
#endif
}

static int failed_attempts = 0;

static tConfItem<int>  at_di("ARMAGETRON_DISPLAY_INDEX"	, currentScreensetting.displayIndex);
static tConfItem<int>  at_ldi("ARMAGETRON_LAST_DISPLAY_INDEX"	, lastSuccess.displayIndex);

static tConfItem<int>  at_rr("ARMAGETRON_REFRESH_RATE"	, currentScreensetting.refreshRate);
static tConfItem<int>  at_lrr("ARMAGETRON_LAST_REFRESH_RATE"	, lastSuccess.refreshRate);

static tConfItem<rResolution> screenres("ARMAGETRON_SCREENMODE",currentScreensetting.res.res);
static tConfItem<rResolution> screenresLast("ARMAGETRON_LAST_SCREENMODE",lastSuccess.res.res);

static tConfItem<rResolution> winsize("ARMAGETRON_WINDOWSIZE",currentScreensetting.windowSize.res);
static tConfItem<rResolution> winsizeLast("ARMAGETRON_LAST_WINDOWSIZE",lastSuccess.windowSize.res);

static tConfItem<rVSync> vSync("ARMAGETRON_VSYNC",currentScreensetting.vSync);
static tConfItem<rVSync> vSyncLast("ARMAGETRON_VSYNC_LAST",lastSuccess.vSync);

static tConfItem<int> presentMode("VULKAN_PRESENT_MODE",currentScreensetting.presentMode);

static tConfItem<int> screenres_w("ARMAGETRON_SCREENMODE_W",currentScreensetting.res.width);
static tConfItem<int> screenresLast_w("ARMAGETRON_LAST_SCREENMODE_W", lastSuccess.res.width);

static tConfItem<int> winsize_w("ARMAGETRON_WINDOWSIZE_W",currentScreensetting.windowSize.width);
static tConfItem<int> winsizeLast_w("ARMAGETRON_LAST_WINDOWSIZE_W",lastSuccess.windowSize.width);

static tConfItem<int> screenres_h("ARMAGETRON_SCREENMODE_H",currentScreensetting.res.height);
static tConfItem<int> screenresLast_h("ARMAGETRON_LAST_SCREENMODE_H", lastSuccess.res.height);

// static tConfItem<rScreenSize> winsize_wh("ARMAGETRON_WINDOWSIZE_WH",currentScreensetting.windowSize);

static tConfItem<int> winsize_h("ARMAGETRON_WINDOWSIZE_H",currentScreensetting.windowSize.height);
static tConfItem<int> winsizeLast_h("ARMAGETRON_LAST_WINDOWSIZE_H",lastSuccess.windowSize.height);

static tConfItem<bool> fs_ci("FULLSCREEN",currentScreensetting.fullscreen);
static tConfItem<bool> fs_lci("LAST_FULLSCREEN",lastSuccess.fullscreen);

#ifdef MACOSX
static tConfItem<bool> lowdpi_ci("LOW_DPI_WINDOW",currentScreensetting.lowDPIWindow);
static tConfItem<bool> lowdip_lci("LAST_LOW_DPI_WINDOW",lastSuccess.lowDPIWindow);
#endif

static tConfItem<rColorDepth> tc("COLORDEPTH",currentScreensetting.colorDepth);
static tConfItem<rColorDepth> ltc("LAST_COLORDEPTH",lastSuccess.colorDepth);
static tConfItem<rColorDepth> tzd("ZDEPTH",currentScreensetting.zDepth);
static tConfItem<rColorDepth> ltzd("LAST_ZDEPTH",lastSuccess.zDepth);

static tConfItem<int> fa("FAILED_ATTEMPTS", failed_attempts);

// *******************************************

static tCallback *rPerFrameTask_anchor;

    #ifdef HAVE_LIBRUBY
static tCallbackRuby * rPerFrameTaskRuby_anchor;
    #endif

bool sr_True(){return true;}

rPerFrameTask::rPerFrameTask(AA_VOIDFUNC *f):tCallback(rPerFrameTask_anchor, f){}
void rPerFrameTask::DoPerFrameTasks(){
    // prevent console rendering, that can cause nasty recursions
    rNoAutoDisplayAtNewlineCallback noAutoDisplay( sr_True );
    Exec(rPerFrameTask_anchor);
}

    #ifdef HAVE_LIBRUBY
rPerFrameTaskRuby::rPerFrameTaskRuby()
        :tCallbackRuby(rPerFrameTaskRuby_anchor)
{
}

void rPerFrameTaskRuby::DoPerFrameTasks(){
    rNoAutoDisplayAtNewlineCallback noAutoDisplay( sr_True );
    Exec(rPerFrameTaskRuby_anchor);
}
    #endif



// *******************************************

static tCallbackString *RenderId_anchor;

rRenderIdCallback::rRenderIdCallback(STRINGRETFUNC *f)
        :tCallbackString(RenderId_anchor, f){}
tString rRenderIdCallback::RenderId(){return Exec(RenderId_anchor);}

// *******************************************

// *******************************************************************************************
// *
// *   rScreenSize
// *
// *******************************************************************************************
//!
//!        @param  w   screen width
//!        @param  h  screen height
//!
// *******************************************************************************************

rScreenSize::rScreenSize( int w, int h )
        :res( ArmageTron_Invalid ), width(w), height(h)
{
}

// *******************************************************************************************
// *
// *   rScreenSize
// *
// *******************************************************************************************
//!
//!        @param  r
//!
// *******************************************************************************************

rScreenSize::rScreenSize( rResolution r )
        :res( r ), width(0), height(0)
{
    UpdateSize();
}

// *******************************************************************************************
// *
// *   UpdateSize
// *
// *******************************************************************************************
//!
//!
// *******************************************************************************************

void rScreenSize::UpdateSize( void )
{
    if ( res != ArmageTron_Invalid )
    {
        width = ::width[res];
        height = ::height[res];
        // res = ArmageTron_Invalid;
    }
}

// *******************************************************************************************
// *
// *   operator ==
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return true iff equal
//!
// *******************************************************************************************

bool rScreenSize::operator ==( rScreenSize const & other ) const
{
    return Compare( other ) == 0;
}

// *******************************************************************************************
// *
// *   operator !=
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return  true iff not equal
//!
// *******************************************************************************************

bool rScreenSize::operator !=( rScreenSize const & other ) const
{
    return Compare( other ) != 0;
}

// *******************************************************************************************
// *
// *   Compare
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return         0 if eqal, -1 if this is smaller, +1 if other is smaller
//!
// *******************************************************************************************

int rScreenSize::Compare( rScreenSize const & other ) const
{
    // desktop size dominates all
    if ( width == 0 && other.width != 0 )
        return 1;
    if ( other.width == 0 && width != 0 )
        return -1;

    if ( width < other.width )
        return -1;
    else if ( width > other.width )
        return 1;

    if ( height < other.height )
        return -1;
    else if ( height > other.height )
        return 1;

    /* res is not really a criterion, ignore it
    if ( res < other.res )
        return -1;
    else if ( res > other.res )
        return 1;
    */

    return 0;
}


// *******************************************************************************************
// *
// *   rScreenSettings
// *
// *******************************************************************************************
//!
//!        @param  r   the resolution
//!        @param  fs  fullscreen flag
//!        @param  cd  color depth
//!        @param  sdl use clean sdl initialization
//!        @param  ce  check for errors
//!
// *******************************************************************************************

rScreenSettings::rScreenSettings( rResolution r, bool fs, rColorDepth cd, bool ce )
:res(r), windowSize(r), fullscreen(fs), colorDepth(cd), zDepth( ArmageTron_ColorDepth_Desktop ), checkErrors(true), displayIndex(0), refreshRate(0), vSync( ArmageTron_VSync_Default ), aspect (1), presentMode(0)
{
    // special case for desktop resolution: window size of 640x480
    if ( r == ArmageTron_Desktop )
    {
        windowSize = rScreenSize( ArmageTron_640_480 );
    }
}

void sr_ReinitDisplay(){
    if (!sr_InitDisplay()){
        tERR_ERROR("Oops. Failed to reinit video hardware. "
                   "Resetting to defaults..\n");
        exit(-1);
    }
}


// *******************************************



// GL information

tString gl_vendor;
tString gl_renderer;
tString gl_version;
tString gl_extensions;

bool software_renderer=false;
bool last_software_renderer=false;

static tConfItem<bool> lsr("SOFTWARE_RENDERER",last_software_renderer);

tString lastError("Unknown");

    #ifndef DEDICATED
// sets the number of vsync signals to wait for each frame
static bool sr_SetSwapControl( int frames, bool after = false )
{
    // SDL3: Use SDL_GL_SetSwapInterval directly
    return SDL_GL_SetSwapInterval( frames );
}

static bool sr_SetSwapControlAuto( bool after = false )
{
    bool success = true;

    // requires SDL 1.2.10
    if ( tRecorder::IsRecording() )
    {
        // recordings are always done with VSync enabled
#ifndef DEBUG
        success = sr_SetSwapControl( 1, after );
#endif
    }
    else if( rSysDep::IsBenchmark() )
    {
#ifndef DEBUG
        success = sr_SetSwapControl( 0, after );
#endif
    }
    else
    {
        switch (currentScreensetting.vSync)
        {
        case ArmageTron_VSync_On:
            success = sr_SetSwapControl( 1, after );
            break;
        case ArmageTron_VSync_Off:
        case ArmageTron_VSync_MotionBlur:
            success = sr_SetSwapControl( 0, after );
            break;
        case ArmageTron_VSync_Default:
            break;
        }
    }

    return success;
}

static void sr_SetGLAttributes( int rDepth, int gDepth, int bDepth, int zDepth )
{
    // SDL 1.1 required
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, rDepth );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, gDepth );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, bDepth );
#ifdef WIN32
    if(zDepth > 24)
        zDepth = 24;
#endif
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, zDepth );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
}

// to be called after screen initialization
static void sr_CompleteGLAttributes()
{
    sr_SetSwapControlAuto( true );
}
    #endif // DEDICATED

// flag indicating whether directX is supposed to be used for input (defaults to false, crashes on my Win7)
// bool sr_useDirectX = false;
// static bool use_directx_back = false;

#ifndef DEDICATED

bool IsWindowActive(void) {
    Uint32 flags = 0;

    flags = SDL_GetWindowFlags(sr_screen);
    // SDL3: SDL_WINDOW_SHOWN is removed, check for not hidden and not minimized
    if (!(flags & SDL_WINDOW_HIDDEN) && !(flags & SDL_WINDOW_MINIMIZED)) {
        return true;
    }
    return false;
}

int SDL_EnableUNICODE(int enable) {
    static int SDL_enabled_UNICODE=0;
    int previous = SDL_enabled_UNICODE;

    switch (enable) {
    case 1:
        SDL_enabled_UNICODE = 1;
        // SDL3: SDL_StartTextInput takes window parameter
        SDL_StartTextInput(sr_screen);
        break;
    case 0:
        SDL_enabled_UNICODE = 0;
        SDL_StopTextInput(sr_screen);
        break;
    }
    return previous;
}
#endif


#ifndef DEDICATED
static int CountBits(int toCount)
{
    int ret = 0;
    while(toCount != 0)
    {
        if(toCount & 1)
            ret++;

        toCount >>= 1;
    }

    return ret;
}

static bool lowlevel_sr_InitDisplay(){
    rCallbackBeforeScreenModeChange::Exec();

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    // On mobile (Android/iOS), always force fullscreen at native screen resolution.
    // Configured resolution values are meaningless — the OS owns the display size.
    // Zero width+height causes the "desktop/borderless fullscreen" path below,
    // which calls SDL_SetWindowFullscreenMode(nullptr) to use native resolution.
    currentScreensetting.fullscreen = true;
    // Do NOT force aspect = 1.0 — let SDL report the real drawable size so the
    // viewport fills the entire screen without black bars on widescreen iPhones.
    currentScreensetting.res = rScreenSize(0, 0);
#endif

    rScreenSize & res = currentScreensetting.fullscreen ? currentScreensetting.res : currentScreensetting.windowSize;

    // update pixel aspect ratio
#ifndef __ANDROID__
    if ( res.res != ArmageTron_Invalid && size_t(res.res) < sizeof(aspect)/sizeof(aspect[0]) )
        currentScreensetting.aspect = aspect[res.res];
#endif

    res.UpdateSize();
    sr_screenWidthInPoints = res.width;
    sr_screenHeightInPoints= res.height;

    // desktop color depth
    static int desktopCD_R = 8;
    static int desktopCD_G = 8;
    static int desktopCD_B = 8;
    // static int desktopCD   = 16;
    // desktop resolution
    static int sr_desktopWidth = 0, sr_desktopHeight = 0;

    int minWidth = 640;
    int minHeight = 480;

    // last diplay index in use
    static int sr_lastDisplayIndex = -1;

    // last window position
    static int lastWindowX = SDL_WINDOWPOS_CENTERED, lastWindowY = SDL_WINDOWPOS_CENTERED;

    static int lastFactualDisplayIndex = currentScreensetting.displayIndex;
    if( sr_screen )
    {
        // fetch actual display index in case user dragged window
        int factualDisplayIndex = AA_GetWindowDisplayIndex(sr_screen);
        if(factualDisplayIndex != lastFactualDisplayIndex)
        {
            currentScreensetting.displayIndex = factualDisplayIndex;
            lastFactualDisplayIndex = factualDisplayIndex;
        }

        // last window position
        if(!lastSuccess.fullscreen)
        {
            SDL_GetWindowPosition(sr_screen, &lastWindowX, &lastWindowY);
        }
    }

    if(0 > currentScreensetting.displayIndex || currentScreensetting.displayIndex >= AA_GetNumVideoDisplays())
        currentScreensetting.displayIndex = 0;
    lastFactualDisplayIndex = currentScreensetting.displayIndex;

    static const SDL_DisplayMode* desktopMode = nullptr;

    if ( sr_lastDisplayIndex != currentScreensetting.displayIndex )
    {
        // determine desktop mode

        // select sane defaults in case the following operation fails
        sr_desktopWidth = minWidth;
        sr_desktopHeight = minHeight;

        SDL_DisplayID displayID = AA_GetDisplayID(currentScreensetting.displayIndex);
        desktopMode = SDL_GetDesktopDisplayMode(displayID);
        if (desktopMode) {
            sr_desktopWidth  = desktopMode->w;
            sr_desktopHeight = desktopMode->h;

            int bpp;
            Uint32 Rmask, Gmask, Bmask, Amask;

            if (SDL_GetMasksForPixelFormat(desktopMode->format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
                // desktopCD    = bpp;
                desktopCD_R  = CountBits(Rmask);
                desktopCD_G  = CountBits(Gmask);
                desktopCD_B  = CountBits(Bmask);
            }
        }
    }

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    // SDL_GetDesktopDisplayMode may return portrait dims (iOS always reports portrait base).
    // The game is landscape-only, so ensure width > height.
    if (sr_desktopWidth < sr_desktopHeight)
        std::swap(sr_desktopWidth, sr_desktopHeight);
#endif

    // determine layout of current screen
    SDL_Rect screenBounds;
    SDL_DisplayID currentDisplayID = AA_GetDisplayID(currentScreensetting.displayIndex);
    SDL_GetDisplayBounds(currentDisplayID, &screenBounds);

#if defined(__APPLE__) && TARGET_OS_IOS
    // On iOS, SDL_GetDesktopDisplayMode and SDL_GetDisplayBounds may return
    // a lower-resolution "Display Zoom" compatibility mode.  Query
    // UIScreen.nativeBounds directly for the true hardware pixel resolution.
    {
        void sr_GetNativeScreenPixels(int* w, int* h); // defined in rScreenIOS.mm
        int nativeW = 0, nativeH = 0;
        sr_GetNativeScreenPixels(&nativeW, &nativeH);
        if (nativeW < nativeH) std::swap(nativeW, nativeH); // landscape
        float density = desktopMode ? desktopMode->pixel_density : 3.0f;
        int nativePtsW = (int)(nativeW / density);
        int nativePtsH = (int)(nativeH / density);
        if (nativePtsW > sr_desktopWidth || nativePtsH > sr_desktopHeight) {
            sr_desktopWidth  = nativePtsW;
            sr_desktopHeight = nativePtsH;
        }
    }
#endif

    // default start window size and position
    int defaultWidth = sr_desktopWidth;
    int defaultHeight = sr_desktopHeight;
    if(!currentScreensetting.fullscreen)
    {
        defaultWidth = sr_screenWidthInPoints;
        defaultHeight = sr_screenHeightInPoints;
    }

    int defaultX = screenBounds.x + (screenBounds.w-defaultWidth)/2;
    int defaultY = screenBounds.y + (screenBounds.h-defaultHeight)/2;

    if(!currentScreensetting.fullscreen && (
           lastWindowX != SDL_WINDOWPOS_CENTERED || lastWindowY != SDL_WINDOWPOS_CENTERED))
    {
        defaultX = lastWindowX;
        defaultY = lastWindowY;
    }

    bool highDPI=true;
#ifdef MACOSX
    if(currentScreensetting.lowDPIWindow)
    {
        highDPI = false;
    }
#endif
    static bool lastHighDPI = highDPI;

    // reinit on color/z depth or resolution change
    if(currentScreensetting.zDepth != lastSuccess.zDepth ||
       currentScreensetting.colorDepth != lastSuccess.colorDepth ||
       lastHighDPI != highDPI
       )
    {
        if(sr_screen)
        {
            SDL_DestroyWindow(sr_screen);
            sr_screen=nullptr;
        }
        lastHighDPI = highDPI;
    }

    if (!sr_screen)
    {
        int singleCD_R	= 5;
        int singleCD_G	= 5;
        int singleCD_B	= 5;
        // int fullCD		= 16;
        int zDepth		= 16;

        switch (currentScreensetting.colorDepth)
        {
        case ArmageTron_ColorDepth_16:
            // parameters already set for this depth
            break;
        case ArmageTron_ColorDepth_Desktop:
            {
                // fullCD     = desktopCD;
                singleCD_R = desktopCD_R;
                singleCD_G = desktopCD_G;
                singleCD_B = desktopCD_B;
                zDepth		= 32;
            }
            break;
        case ArmageTron_ColorDepth_32:
            singleCD_R	= 8;
            singleCD_G	= 8;
            singleCD_B	= 8;
            // fullCD		= 24;
            zDepth		= 32;
            break;
        }

        switch ( currentScreensetting.zDepth )
        {
        case ArmageTron_ColorDepth_16: zDepth = 16; break;
        case ArmageTron_ColorDepth_32: zDepth = 32; break;
        default: break;
        }

        // Vulkan: no GL attributes needed, use Vulkan window flag
        // sr_SetGLAttributes( singleCD_R, singleCD_G, singleCD_B, zDepth );

        SDL_WindowFlags attrib = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

        if(highDPI)
        {
            attrib |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }

        // Note: SDL_SetRelativeMouseMode will be set after window creation

    #ifdef FORCE_WINDOW
    #ifdef WIN32
        //		sr_screenWidthInPoints  = 400;
        //		sr_screenHeightInPoints = 300;
    #else
        //		sr_screenWidthInPoints  = minWidth;
        //		sr_screenHeightInPoints = minHeight;
    #endif
    #endif
        // int CD = fullCD;

#ifdef FORCE_WINDOW_X
        // Force windowed mode for debugging
        attrib &= ~SDL_WINDOW_FULLSCREEN;
#endif

        // try fullscreen first if requested and sensible (only display 0 is supported)
        if (currentScreensetting.fullscreen && currentScreensetting.displayIndex == 0)
        {
            // SDL3: Create window then set fullscreen mode
            sr_screen = SDL_CreateWindow("", sr_desktopWidth, sr_desktopHeight, attrib | SDL_WINDOW_FULLSCREEN);
            if (sr_screen)
            {
                // Set desktop fullscreen mode (NULL for borderless fullscreen)
                SDL_SetWindowFullscreenMode(sr_screen, nullptr);
                SDL_SetWindowPosition(sr_screen, defaultX, defaultY);
            }
        }

        // only reinit the screen if the desktop res detection hasn't left us
        // with a perfectly good one.
        if (!sr_screen)
        {
            sr_screen = SDL_CreateWindow("", defaultWidth, defaultHeight, attrib);
            if (sr_screen)
            {
                SDL_SetWindowPosition(sr_screen, defaultX, defaultY);
            }
        }

        if (!sr_screen)
        {
            lastError.Clear();
            lastError << "Couldn't set video mode: ";
            lastError << SDL_GetError();
            std::cerr << lastError << '\n';
            return false;
        }

        sr_SetWindowTitle();

        SDL_EnableUNICODE(1);
    }

    if(!sr_screen)
        return false;

    if (AA_GetWindowDisplayIndex(sr_screen) != currentScreensetting.displayIndex ||
        (sr_lastDisplayIndex >= 0 && sr_lastDisplayIndex != currentScreensetting.displayIndex) ||
        (currentScreensetting.fullscreen && !lastSuccess.fullscreen)
        )
    {
        // go to window mode, position window on center of selected display
        SDL_SetWindowFullscreen(sr_screen, 0);
        SDL_SetWindowSize(sr_screen, defaultWidth, defaultHeight);
        SDL_SetWindowPosition(sr_screen, defaultX, defaultY);

        SDL_Delay(10);
        SDL_PumpEvents();
    }

    // SDL2 can resize window or toggle fullscreen without recreating a new window and therefore keeping existing GL context.
    if (currentScreensetting.fullscreen)
    {
        bool fullscreenSuccess = true;

        // do we need a custom display mode? if a display mode
        // is set, yes, but also if a non-default custom refresh rate
        // is set.
        float desktopRefreshRate = desktopMode ? desktopMode->refresh_rate : 60.0f;
        if ( sr_screenWidthInPoints + sr_screenHeightInPoints > 0 || (currentScreensetting.refreshRate != static_cast<int>(desktopRefreshRate) && currentScreensetting.refreshRate > 0))
        {
            fullscreenSuccess = false;

            // find best display mode
            int desiredW = sr_screenWidthInPoints;
            int desiredH = sr_screenHeightInPoints;
            if ( sr_screenWidthInPoints + sr_screenHeightInPoints <= 0 && desktopMode)
            {
                desiredW = desktopMode->w;
                desiredH = desktopMode->h;
            }

            SDL_DisplayID currentDisplayID = AA_GetDisplayID(currentScreensetting.displayIndex);
            // SDL3: SDL_GetClosestFullscreenDisplayMode returns bool and takes output param
            SDL_DisplayMode closestMode;
            bool foundClosest = SDL_GetClosestFullscreenDisplayMode(
                currentDisplayID, desiredW, desiredH,
                static_cast<float>(currentScreensetting.refreshRate), true, &closestMode);
            const SDL_DisplayMode *closest = foundClosest ? &closestMode : nullptr;
            if(foundClosest)
            {
                const SDL_DisplayMode *lastMode = SDL_GetWindowFullscreenMode(sr_screen);
                if(!lastMode ||
                   lastMode->format != closest->format ||
                   lastMode->w != closest->w ||
                   lastMode->h != closest->h ||
                   lastMode->refresh_rate != closest->refresh_rate)
                {
                    SDL_SetWindowFullscreen(sr_screen, false);
                    SDL_Delay(100);
                    SDL_PumpEvents();
                }

                // set the display mode
                sr_screenWidthInPoints = closest->w;
                sr_screenHeightInPoints = closest->h;

                if(SDL_SetWindowFullscreenMode(sr_screen, closest))
                {
                    SDL_Delay(100);
                    SDL_PumpEvents();
                    SDL_SetWindowSize(sr_screen, sr_screenWidthInPoints, sr_screenHeightInPoints);
                    SDL_Delay(100);
                    SDL_PumpEvents();
                    fullscreenSuccess = SDL_SetWindowFullscreen(sr_screen, true);
                }
            }

            if(!fullscreenSuccess)
            {
                lastError.Clear();
                lastError << "Couldn't set video mode: ";
                lastError << SDL_GetError();
                std::cerr << static_cast<const char*>(lastError) << '\n';
            }
        }
        else
        {
            // simply set fullscreen mode (desktop/borderless fullscreen via NULL mode)
            SDL_SetWindowFullscreenMode(sr_screen, nullptr);
            fullscreenSuccess = SDL_SetWindowFullscreen(sr_screen, true);
        }

        // if desktop resolution was selected or custom mode setting failed, pick desktop mode with explicit resolution
        if(!fullscreenSuccess)
        {
            sr_screenWidthInPoints = sr_desktopWidth;
            sr_screenHeightInPoints = sr_desktopHeight;
            SDL_SetWindowFullscreenMode(sr_screen, nullptr);
            SDL_SetWindowFullscreen(sr_screen, false);
            SDL_Delay(100);
            SDL_PumpEvents();
            SDL_SetWindowSize(sr_screen, sr_screenWidthInPoints, sr_screenHeightInPoints);
            SDL_Delay(100);
            SDL_PumpEvents();
            SDL_SetWindowFullscreenMode(sr_screen, nullptr);
            fullscreenSuccess = SDL_SetWindowFullscreen(sr_screen, true);
        }

        if(fullscreenSuccess)
        {
            // SDL3: Sync window to ensure fullscreen mode change is complete
            SDL_SyncWindow(sr_screen);
            SDL_SetWindowRelativeMouseMode(sr_screen, true);
        }
        else
        {
            lastError.Clear();
            lastError << "Couldn't set desktop video mode: ";
            lastError << SDL_GetError();
            std::cerr << static_cast<const char*>(lastError) << '\n';

            currentScreensetting.fullscreen = false;

            sr_screenWidthInPoints  = currentScreensetting.windowSize.width;
            sr_screenHeightInPoints = currentScreensetting.windowSize.height;
        }
    }
    if (!currentScreensetting.fullscreen)
    {
        // Set windowed mode and size accordingly
        if (SDL_SetWindowFullscreen(sr_screen, false))
        {
            SDL_SetWindowSize(sr_screen, sr_screenWidthInPoints, sr_screenHeightInPoints);
            {
                // Sometimes, setting the window size fails. Check the actual size to ver
                int w = sr_screenWidthInPoints, h = sr_screenHeightInPoints;
                SDL_GetWindowSize(sr_screen, &w, &h);
                if(w != sr_screenWidthInPoints || h != sr_screenHeightInPoints ||
                 SDL_GetWindowFlags(sr_screen) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
                {
                    // Mismatch. Try to shake it free.
                    SDL_MaximizeWindow(sr_screen);
                    SDL_RestoreWindow(sr_screen);
                    SDL_SetWindowSize(sr_screen, sr_screenWidthInPoints, sr_screenHeightInPoints);
                }
            }

            SDL_SetWindowPosition(sr_screen, defaultX, defaultY);
            {
                // Get/SetWindowPosition don't always agree on what position means.
                // compensate for any constant offset (for window title bar, for example)
                // if that is the case.
                int x, y;
                SDL_GetWindowPosition(sr_screen, &x, &y);
                if(x != defaultX || y != defaultY)
                {
                    SDL_SetWindowPosition(sr_screen, 2*defaultX-x, 2*defaultY-y);
                }
            }

            // we're already setting the relevant flag on creation, but maybe it gets lost in fullscreen mode
            SDL_SetWindowResizable(sr_screen, true);
            SDL_SetWindowRelativeMouseMode(sr_screen, false);

        }
        else
        {
            lastError.Clear();
            lastError << "Couldn't set windowed mode: ";
            lastError << SDL_GetError();
            std::cerr << static_cast<const char*>(lastError) << '\n';
            return false;
        }
    }

    // SDL3: Synchronize window state on async windowing systems (Wayland, etc.)
    // This ensures fullscreen/windowed mode changes are complete before continuing
    SDL_SyncWindow(sr_screen);

#if defined(__APPLE__) && TARGET_OS_IOS
    {
        // On iOS, SDL_GetDesktopDisplayMode may report a reduced "Display Zoom"
        // resolution (e.g. 480x320 pts) rather than the real display size.
        // After the window is created and synced, ask SDL for the actual window
        // size so the viewport covers the full screen.
        int actualW = 0, actualH = 0;
        SDL_GetWindowSize(sr_screen, &actualW, &actualH);
        if (actualW > 0 && actualH > 0) {
            sr_screenWidthInPoints  = actualW;
            sr_screenHeightInPoints = actualH;
        }
    }
#endif

    {
        // Vulkan: complete initialization now that the SDL window exists
        extern void sr_vkRendererLateInit();
        sr_vkRendererLateInit();
    }

    #ifndef DEDICATED
    gl_vendor.Clear();
    gl_renderer.Clear();
    gl_version.Clear();
    gl_extensions.Clear();
    renderer_identification.Clear();

    // sanity check texture modes
    for(int i=rTextureGroups::TEX_GROUPS-1; i>=0; --i)
    {
        int & texmode = rTextureGroups::TextureMode[i];

        // don't do anything for deliberately disabled textures
        if( i == rTextureGroups::TEX_FONT || texmode >= 0 )
        {
            // to default if the modes have been reset for some reason
            if( texmode == 0 )
            {
                texmode = default_texturemode;
            }
            if( texmode < rGLConst::Nearest )
            {
                texmode = rGLConst::Nearest;
            }
            if( texmode > rGLConst::LinearMipmapLinear )
            {
                texmode = rGLConst::LinearMipmapLinear;
            }
        }
    }

    gl_vendor     << RenderGetRendererString(rGLConst::Vendor);
    gl_renderer   << RenderGetRendererString(rGLConst::Renderer);
    gl_version    << RenderGetRendererString(rGLConst::Version);
    gl_extensions << RenderGetRendererString(rGLConst::Extensions);

    // Display lists are not used in GL3 renderer


#ifndef WIN32
    if(!strstr(gl_renderer,"Voodoo3"))
    #endif
    {
        // SDL3: SDL_ShowCursor/SDL_HideCursor take no arguments
        if(currentScreensetting.fullscreen)
            SDL_HideCursor();
        else
            SDL_ShowCursor();
    }

    #ifdef WIN32
    renderer_identification << "WIN32 ";
    #else
    #ifdef MACOSX
    renderer_identification << "MACOSX ";
    #else
    renderer_identification << "LINUX ";
    #endif
    #endif
    renderer_identification << rRenderIdCallback::RenderId() << ' ';
    renderer_identification << "SDL 1.2\n";
    renderer_identification << "CD=" << currentScreensetting.colorDepth  << '\n';
    renderer_identification << "FS=" << currentScreensetting.fullscreen  << '\n';
    renderer_identification << "GL_VENDOR=" << gl_vendor   << '\n';
    renderer_identification << "GL_RENDERER=" << gl_renderer << '\n';
    renderer_identification << "GL_VERSION=" << gl_version  << '\n';
    #endif

    if (// test for Windows software GL (be a little flexible...)
        (
            strstr(gl_vendor,"icrosoft") || strstr(gl_vendor,"SGI")
        )
        && strstr(gl_renderer,"eneric")
    )
        software_renderer=true;

    if ( // test for Mesa software GL
        strstr(gl_vendor,"rian") && strstr(gl_renderer,"X11") &&
        strstr(gl_renderer,"esa")
    )
        software_renderer=true;

    if ( // test for Mesa software GL, new versions
        strstr(gl_vendor,"Mesa") &&
        strstr(gl_renderer,"Software Rasterizer")
        )
        software_renderer=true;

    if ( // test for GLX software GL
        strstr(gl_renderer,"GLX") &&
        strstr(gl_renderer,"ndirect") &&
        strstr(gl_renderer,"esa")
    )
        software_renderer=true;

    // disable storage of non-alpha textures on Savage MX
    if ( strstr( gl_renderer, "SavageMX" ) )
    {
        rISurfaceTexture::storageHack_ = true;
    }

    // fonts look best in bilinear filtering, no mipmaps
    if ( rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] > rGLConst::Linear )
        rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] = rGLConst::Linear;

    // disable trilinear filtering for ATI cards
    if ( strstr( gl_vendor, "ATI" ) )
    {
        default_texturemode = rGLConst::LinearMipmapNearest;
    }

    // wait for activation if we were ALT-Tabbed away:
    while ( !IsWindowActive() )
    {
        SDL_Delay(100);
        SDL_PumpEvents();
    }

    if (software_renderer && !last_software_renderer && !tRecorder::IsPlayingBack())
        sr_LoadDefaultConfig();

    last_software_renderer=software_renderer;


    // wait for activation if we were ALT-Tabbed away:
    while ( !IsWindowActive() )
    {
        SDL_Delay(100);
        SDL_PumpEvents();
    }

    // Update drawable size before resetting render state to ensure correct viewport
    sr_GetDrawableSize();

    sr_ResetRenderState(true);

    rCallbackAfterScreenModeChange::Exec();

    // store last display index
    sr_lastDisplayIndex = currentScreensetting.displayIndex;

    lastSuccess=currentScreensetting;
    failed_attempts = 0;
//    sr_useDirectX = use_directx_back;

    st_SaveConfig();

    return true;
}
#else // #ifdef DEDICATED
static bool lowlevel_sr_InitDisplay()
{
    return true;
}
#endif

bool sr_InitDisplay(){
//    use_directx_back = sr_useDirectX;

    lastSuccessLowZBuffer = lastSuccess;
    lastSuccessLowZBuffer.zDepth = rColorDepth::ArmageTron_ColorDepth_16;
    lastSuccessLowColor = lastSuccess;
    lastSuccessLowColor.colorDepth = rColorDepth::ArmageTron_ColorDepth_16;
    lastSuccessLowColor.zDepth = rColorDepth::ArmageTron_ColorDepth_16;

    while (failed_attempts <= MAXEMERGENCY+1)
    {
        if (failed_attempts)
        {
#ifdef DEBUG
            std::cout << "failed attempts:" << failed_attempts << "\n";
            std::cout.flush();
#endif
            currentScreensetting = *emergency[failed_attempts];

//            sr_useDirectX = false;
        }

        // prepare for crash, note failure and save config
        failed_attempts++;
        st_SaveConfig();

        auto Success = [&]()
        {
            sr_GetDrawableSize();

            sr_UnlockSDL();
            failed_attempts = 0;
            st_SaveConfig();
            return true;
        };

        sr_screenWidth = sr_screenWidthInPoints;
        sr_screenHeight = sr_screenHeightInPoints;

        sr_LockSDL();
        if (lowlevel_sr_InitDisplay())
        {
            return Success();
        }

        st_SaveConfig();

        if (lowlevel_sr_InitDisplay())
        {
            return Success();
        }
        sr_UnlockSDL();


    }

    failed_attempts = 1;
    st_SaveConfig();

    tERR_ERROR("\nSorry, played all my cards trying to "
               "initialize your video system.\n"
               << tOutput("$program_name") << " won't run on your computer. Reason:\n\n"
               << lastError
               << "\n\nI'll try again from the beginning, but the "
               << "chances of success are minimal.\n"
              );

    return false;
}

//! Clean up all GL resources before destroying context
//! CRITICAL: Must be called BEFORE destroying the GL context
//! This ensures proper cleanup order: resources -> context -> window
static void sr_CleanupGLResources()
{
    #ifndef DEDICATED
    // Flush all pending GPU commands to ensure completion
    RenderFlush();
    RenderFinish();

    // ITERATION 11: Complete GPU resource cleanup
    // Cleanup order matters: resources that depend on others must be released first

    // 1. Clean up render queue resources (may reference other resources)
    rRenderQueue::Instance().ReleaseGPU();

    // 2. Unload all textures from GPU memory
    rITexture::UnloadAll();

    // Note: Wall geometry cleaned via RAII when game objects destroyed
    #endif
}

void sr_ExitDisplay(){
    #ifndef DEDICATED
    rCallbackBeforeScreenModeChange::Exec();

    // CRITICAL FIX: Proper OpenGL cleanup order
    // 1. Clean up GL resources
    // 2. Destroy GL context
    // 3. Destroy window
    // This order prevents crashes and resource leaks on some drivers

    if(sr_glcontext && sr_screen)
    {
        sr_LockSDL();

        // Make context current for cleanup operations
        SDL_GL_MakeCurrent(sr_screen, sr_glcontext);

        // Clean up all GL resources BEFORE destroying context
        sr_CleanupGLResources();

        // Now destroy GL context
        SDL_GL_DestroyContext(sr_glcontext);
        sr_glcontext = nullptr;

        sr_UnlockSDL();
    }

    // Finally destroy window
    if (sr_screen)
    {
        sr_LockSDL();
#if !(defined(__APPLE__) && TARGET_OS_IOS)
        // On iOS these calls are no-ops at best and can stall/crash after Vulkan
        // has already torn down the Metal layer — skip them entirely.
        SDL_SetWindowFullscreen(sr_screen, false);
        SDL_SetWindowRelativeMouseMode(sr_screen, false);
#endif
        SDL_DestroyWindow(sr_screen);
        sr_screen = nullptr;
        sr_UnlockSDL();
    }

    #endif
}

void sr_GetDrawableSize()
{
#ifndef DEDICATED
    if(sr_screen)
    {
        // SDL3: SDL_GL_GetDrawableSize → SDL_GetWindowSizeInPixels
        // SDL3: SDL_GL_GetDrawableSize → SDL_GetWindowSizeInPixels
        SDL_GetWindowSizeInPixels(sr_screen, &sr_screenWidth, &sr_screenHeight);
        return;
    }
    sr_screenWidth = sr_screenWidthInPoints;
    sr_screenHeight = sr_screenHeightInPoints;
#endif
}


bool    sr_alphaBlend=true;
bool    sr_glOut=true;
bool    sr_smoothShading=true;


int sr_floorMirror=0;
int sr_floorDetail=rFLOOR_TEXTURE;
bool sr_highRim=true;
bool sr_upperSky=false;
bool sr_lowerSky=false;
bool sr_skyWobble=true;
bool sr_dither=true;
bool sr_infinityPlane=false;
bool sr_laggometer=true;
bool sr_predictObjects=false;
bool sr_texturesTruecolor=true;

bool sr_textOut=false;
bool sr_FPSOut=true;

bool sr_keepWindowActive=true;

tString renderer_identification;

void sr_LoadDefaultConfig(){

    // High detail defaults; no problem for your ordinary 3d-card.
    sr_alphaBlend=true;
    sr_useDisplayLists=rDisplayList_Off;
    sr_textOut=true;
    sr_dither=true;
    sr_smoothShading=true;
    int i;
    #ifndef DEDICATED
    for (i=rTextureGroups::TEX_GROUPS-1;i>=0;i--)
        rTextureGroups::TextureMode[i]=default_texturemode;

    // fonts look best in bilinear filtering, no mipmaps
    rTextureGroups::TextureMode[rTextureGroups::TEX_FONT]=rGLConst::Linear;
    #endif
    sr_floorDetail=rFLOOR_TWOTEXTURE;
    sr_floorMirror=rMIRROR_OFF;
    sr_infinityPlane=false;
    sr_lowerSky=false;
    sr_upperSky=false;
    sr_keepWindowActive=true;

    if (software_renderer){
        // A software renderer! Poor soul. Set low details:
        for (i=rTextureGroups::TEX_GROUPS-1;i>=0;i--)
            rTextureGroups::TextureMode[i]=-1;

    #ifndef DEDICATED
        rTextureGroups::TextureMode[rTextureGroups::TEX_OBJ]=rGLConst::NearestMipmapNearest;
        rTextureGroups::TextureMode[rTextureGroups::TEX_FONT]=rGLConst::NearestMipmapNearest;
    #endif

        sr_highRim=false;
        sr_dither=false;
        sr_alphaBlend=false;
        sr_smoothShading=true; // smooth shading does not slow down the
        // two tested renderers; leave it it.
        sr_floorDetail=rFLOOR_GRID;
        sr_floorMirror=rMIRROR_OFF;
    }
    else if(strstr(gl_vendor,"3Dfx")){
        //workaround for 3dfx renderer: aliasing must be turned on
        //sr_lineAntialias=rFEAT_OFF;
    }
    else if(strstr(gl_vendor,"NVIDIA")){
        // infinity plane works for NVIDIA
        sr_infinityPlane=true;
    }
    else if(strstr(gl_vendor,"Apple")){
        // Vulkan/MoltenVK on Apple Silicon — infinity plane via large finite quad
        sr_infinityPlane=true;
    }
    #ifdef MACOSX
    else if(strstr(gl_vendor,"ATI")){
        // glFlush swapping work for ATI on the mac
        // rSysDep::swapMode_=rSysDep::rSwap_glFlush;
    }
    #endif
    else if(strstr(gl_vendor,"Matrox")){
        sr_floorDetail = rFLOOR_TEXTURE;  // double textured floor does not work
    }
}

void sr_ResetRenderState(bool menu){
    if(!sr_glOut)
        return;
    #ifndef DEDICATED

    // Z-Buffering and perspective correction
    // Use renderer abstraction to ensure gl3Renderer state tracking is updated

    if (menu){
        RenderDisableState(rGLConst::DepthTest);
        RenderHint(0x0C50, rGLConst::Fastest);
        RenderViewport(0, 0, sr_screenWidth, sr_screenHeight);
    }
    else{
        RenderEnableState(rGLConst::DepthTest);
        RenderDepthFunc(rGLConst::LEqual);
    }

    if (sr_dither)
        RenderEnableState(0x0BD0);
    else
        RenderDisableState(0x0BD0);

    RenderDisableState(rGLConst::Lighting);

    // disable texture mapping (selecting textures will reactivate it)
    RenderDisableState(rGLConst::Texture2D);

    // Note: Flat/smooth shading is always smooth in GL3 renderer (vertex color interpolation)
    // The RenderShadeModel calls have been removed as they are no-ops in the modern renderer

    // alpha blending
    if (sr_alphaBlend){
        RenderAlphaFunc(rGLConst::Greater, 0);
        RenderEnableState(rGLConst::Blend);
        RenderBlendFunc(rGLConst::SrcAlpha, rGLConst::OneMinusSrcAlpha);
    }
    else{
        RenderDisableState(rGLConst::AlphaTest);
        RenderDisableState(rGLConst::Blend);
    }

    // reset matrices
    TexMatrix();
    IdentityMatrix();

    ProjMatrix();
    IdentityMatrix();

    ModelMatrix();
    IdentityMatrix();
    #endif
}


/*
static uMenuItemFunction apply
(&sg_screenMenu_mode,"Apply Changes",
"This activates the changes to the resolution and fullscreen/windowed mode "
"made above. This does not work on all systems; exit and reenter Armagetron "
"instead if you experience problems.",
 sr_ReinitDisplay);
*/





//static bool offs=false;

void sr_DepthOffset(bool offset){
    #ifndef DEDICATED
    if (offset){
        RenderPolygonOffset(-2, -5);
        RenderEnableState(rCapability::PolygonOffsetLine);
        RenderEnableState(rCapability::PolygonOffsetPoint);
        RenderEnableState(rCapability::PolygonOffsetFill);
    }
    else{
        RenderPolygonOffset(0, 0);
        RenderDisableState(rCapability::PolygonOffsetPoint);
        RenderDisableState(rCapability::PolygonOffsetLine);
        RenderDisableState(rCapability::PolygonOffsetFill);
    }
    #endif
}

// set activation status
void sr_Activate(bool active)
{
    #ifndef DEDICATED
    if ( !currentScreensetting.fullscreen && !active && sr_keepWindowActive )
    {
        sr_glOut=!active;
    }
    else
    {
        sr_glOut=active;
    }

    // unload textures and stuff if rendering gets disabled
    if (!sr_glOut)
        rCallbackBeforeScreenModeChange::Exec();

    // Jonathans fullscreen bugfix.
    #endif
}

tString & sr_CurrentWindowTitle()
{
    static tString title(tOutput("$window_title_menu"));
    return title;
}

void sr_SetWindowTitle(tOutput o)
{
    tString s;
    s << o;

    sr_SetWindowTitle(s);
}

void sr_SetWindowTitle(tString s)
{
    sr_CurrentWindowTitle() = s;
#ifdef MACOSX
    if(!currentScreensetting.fullscreen)
#endif
    {
#ifndef DEDICATED
        SDL_SetWindowTitle(sr_screen, s);
#endif
    }
}

void sr_SetWindowTitle()
{
    sr_SetWindowTitle(sr_CurrentWindowTitle());
}

//**************************************
//** Screen mode callbacks            **
//**************************************


static tCallback *sr_BeforeAnchor;

rCallbackBeforeScreenModeChange::rCallbackBeforeScreenModeChange(AA_VOIDFUNC *f)
        :tCallback(sr_BeforeAnchor, f){}

void rCallbackBeforeScreenModeChange::Exec()
{
    tCallback::Exec(sr_BeforeAnchor);
}

static tCallback *sr_AfterAnchor;

rCallbackAfterScreenModeChange::rCallbackAfterScreenModeChange(AA_VOIDFUNC *f)
        :tCallback(sr_AfterAnchor, f){}

void rCallbackAfterScreenModeChange::Exec()
{
    tCallback::Exec(sr_AfterAnchor);
}

