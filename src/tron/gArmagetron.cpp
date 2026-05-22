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

#include "gStuff.h"
#include "gMoviepack.h"
#include "tSysTime.h"
#include "tDirectories.h"
#include "tLocale.h"
#include "rViewport.h"
#include "rConsole.h"
#include "gGame.h"
#include "gLogo.h"
#include "gCommandLineJumpStart.h"

#include "eSoundMixer.h"

#include "rScreen.h"
#include "rSysdep.h"
#ifndef DEDICATED
#include "rFrameLifecycle.h"
#include "tLuaState.h"
#include "gLuaBindings.h"
#endif
#include "uInputQueue.h"
#include "uInput.h"
//#include "eTess.h"
#include "rTexture.h"
#include "tConfiguration.h"
#include "tRandom.h"
#include "tRecorder.h"
#include "tCommandLine.h"
#include "tToDo.h"
#include "eAdvWall.h"
#include "eGameObject.h"
#include "uMenu.h"
#include "ePlayer.h"
#include "gLanguageMenu.h"
#include "gAICharacter.h"
#include "gCycle.h"
//#include <unistd>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <bitset>
#include "tCrypto.h"

#include "nServerInfo.h"
#include "nSocket.h"
#include "eLadderLog.h"
#ifndef DEDICATED
#include "rRender.h"
#include "rSDL.h"
// SDL_syswm.h - only needed for SDL2, SDL3 includes it via SDL.h
#ifndef HAVE_SDL3
#include <SDL_syswm.h>
#endif
// On Android and iOS, SDL_main.h redefines main() as SDL_main() so SDL's app delegate can call it
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if (defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)) && defined(HAVE_SDL3)
#include <SDL3/SDL_main.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <unistd.h>
#endif
// On-screen touch buttons are cockpit widgets now: each player's cockpit
// loads an optional touch-overlay XML (default
// AATeam/touch/touch-buttons-0.1.aacockpit.xml) on top of its primary
// cockpit, and the buttons render + route input through the standard
// cCockpit/cWidget::TouchButton pipeline. No platform-specific overlay
// code is needed any more.

static gCommandLineJumpStartAnalyzer sg_jumpStartAnalyzer;
#endif

#ifndef DEDICATED
#ifdef MACOSX_XCODE
#include "gOSXURLHandler.h"
#endif
#endif

// data structure for command line parsing
class gMainCommandLineAnalyzer: public tCommandLineAnalyzer
{
public:
    bool     fullscreen_;
    bool     windowed_;
    bool     use_directx_;
    bool     dont_use_directx_;
#ifndef DEDICATED
    int      screenshotFrame_;       //!< capture PNG at this frame; -1 = disabled
    tString  screenshotOut_;         //!< output path for --screenshot-out
    int      exitAfterFrame_;        //!< exit after N frames; -1 = disabled
    tString  compileMoviepackPath_;  //!< non-empty → compile-moviepack mode
#endif

    gMainCommandLineAnalyzer()
    {
        windowed_ = false;
        fullscreen_ = false;
        use_directx_ = false;
        dont_use_directx_ = false;
#ifndef DEDICATED
        screenshotFrame_ = -1;
        exitAfterFrame_  = -1;
#endif
    }


private:
    bool DoAnalyze( tCommandLineParser & parser, int pass ) override
    {
        if(pass > 0)
            return false;

        if ( parser.GetSwitch( "-fullscreen", "-f" ) )
        {
            fullscreen_=true;
        }
        else if ( parser.GetSwitch( "-window", "-w" ) ||  parser.GetSwitch( "-windowed") )
        {
            windowed_=true;
        }
#ifdef WIN32
        else if ( parser.GetSwitch( "+directx") )
        {
            use_directx_=true;
        }
        else if ( parser.GetSwitch( "-directx") )
        {
            dont_use_directx_=true;
        }
#endif
#ifndef DEDICATED
        else if ( tString val; parser.GetOption( val, "--screenshot-frame" ) )
        {
            screenshotFrame_ = atoi( val.c_str() );
        }
        else if ( tString val; parser.GetOption( val, "--screenshot-out" ) )
        {
            screenshotOut_ = val;
        }
        else if ( tString val; parser.GetOption( val, "--exit-after-frame" ) )
        {
            exitAfterFrame_ = atoi( val.c_str() );
        }
#ifdef HAVE_SHADERC_SHADERC_HPP
        else if ( tString val; parser.GetOption( val, "--compile-moviepack" ) )
        {
            compileMoviepackPath_ = val;
        }
#endif
#endif
        else
        {
            return false;
        }

        return true;
    }

    void DoHelp( std::ostream & s ) override
    {                                      //
#ifndef DEDICATED
        s << "-f, --fullscreen             : start in fullscreen mode\n";
        s << "-w, --window, --windowed     : start in windowed mode\n\n";
#ifndef DEDICATED
        s << "--screenshot-frame N         : capture PNG screenshot at rendered frame N\n";
        s << "--screenshot-out path        : output path for --screenshot-frame PNG\n";
        s << "--exit-after-frame N         : exit after N rendered frames\n\n";
#ifdef HAVE_SHADERC_SHADERC_HPP
        s << "--compile-moviepack path     : compile all GLSL shaders in a moviepack ZIP\n";
        s << "                               and add pre-compiled SPIR-V to the ZIP\n\n";
#endif
#endif
#ifdef WIN32
        s << "+directx, -directx           : enable/disable usage of DirectX for screen\n"
        << "                               initialisation under MS Windows\n\n";
        s << "\n\nYes, I know this looks ugly. Sorry about that.\n";
#endif
#endif
    }
};

static gMainCommandLineAnalyzer commandLineAnalyzer;

// flag indicating whether directX is supposed to be used for input (defaults to false, crashes on my Win7)
static bool sr_useDirectX = false;
/*
extern bool sr_useDirectX; // rScreen.cpp
#ifdef WIN32
static tConfItem<bool> udx("USE_DIRECTX","makes use of the DirectX input "
                           "fuctions; causes some graphic cards to fail to work (VooDoo 3,...)",
                           sr_useDirectX);
#endif
*/

extern void exit_game_objects(eGrid *grid);

enum gConnection
{
    gLeave,
    gDialup,
    gISDN,
    gDSL
    // gT1
};

// initial setup menu
void sg_StartupPlayerMenu()
{
    uMenu firstSetup("$first_setup", false);
    firstSetup.SetBot(-.2);

    uMenuItemExit e2(&firstSetup, "$menuitem_accept", "$menuitem_accept_help");

    ePlayer * player = ePlayer::PlayerConfig(0);
    tASSERT( player );

    gConnection connection = gDSL;

    uMenuItemSelection<gConnection> net(&firstSetup, "$first_setup_net", "$first_setup_net_help", connection );
    if ( !st_FirstUse )
    {
        net.NewChoice( "$first_setup_leave", "$first_setup_leave_help", gLeave );
        connection = gLeave;
    }
    net.NewChoice( "$first_setup_net_dialup", "$first_setup_net_dialup_help", gDialup );
    net.NewChoice( "$first_setup_net_isdn", "$first_setup_net_isdn_help", gISDN );
    net.NewChoice( "$first_setup_net_dsl", "$first_setup_net_dsl_help", gDSL );

    tString keyboardTemplate("keys_cursor.cfg");

    uMenuItemSelection<tString> k(&firstSetup, "$first_setup_keys", "$first_setup_keys_help", keyboardTemplate );
    if ( !st_FirstUse )
    {
        k.NewChoice( "$first_setup_leave", "$first_setup_leave_help", tString("") );
        keyboardTemplate="";
    }

    k.NewChoice( "$first_setup_keys_cursor", "$first_setup_keys_cursor_help", tString("keys_cursor.cfg") );
    k.NewChoice( "$first_setup_keys_wasd", "$first_setup_keys_wasd_help", tString("keys_wasd.cfg") );
    // SDL3: keys_zqsd.cfg was SDL1-only, removed
    k.NewChoice( "$first_setup_keys_cursor_single", "$first_setup_keys_cursor_single_help", tString("keys_cursor_single.cfg") );
    k.NewChoice( "$first_setup_keys_x", "$first_setup_keys_x_help", tString("keys_x.cfg") );

#ifdef DEBUG
    k.NewChoice( "none", "none", tString("") );
#endif

    tColor leave(0,0,0,0);
    tColor color(1,0,0);
    uMenuItemSelection<tColor> c(&firstSetup,
                                 "$first_setup_color",
                                 "$first_setup_color_help",
                                 color);

    if ( !st_FirstUse )
    {
        color = leave;
        c.NewChoice( "$first_setup_leave", "$first_setup_leave_help", leave );
    }

    c.NewChoice( "$first_setup_color_red", "", tColor(1,0,0) );
    c.NewChoice( "$first_setup_color_blue", "", tColor(0,0,1) );
    c.NewChoice( "$first_setup_color_green", "", tColor(0,1,0) );
    c.NewChoice( "$first_setup_color_yellow", "", tColor(1,1,0) );
    c.NewChoice( "$first_setup_color_orange", "", tColor(1,.5,0) );
    c.NewChoice( "$first_setup_color_purple", "", tColor(.5,0,1) );
    c.NewChoice( "$first_setup_color_magenta", "", tColor(1,0,1) );
    c.NewChoice( "$first_setup_color_cyan", "", tColor(0,1,1) );
    c.NewChoice( "$first_setup_color_white", "", tColor(1,1,1) );
    c.NewChoice( "$first_setup_color_dark", "", tColor(0,0,0) );

    if ( st_FirstUse )
    {
        for(int i=tRandomizer::GetInstance().Get(4); i>=0; --i)
        {
            c.LeftRight(1);
        }
    }

    uMenuItemString n(&firstSetup,
                      "$player_name_text",
                      "$player_name_help",
                      player->name, ePlayerNetID::MAX_NAME_LENGTH);

    uMenuItemExit e(&firstSetup, "$menuitem_accept", "$menuitem_accept_help");

    firstSetup.Enter();

    // apply network rates
    switch(connection)
    {
    case gDialup:
        sn_maxRateIn  = 6;
        sn_maxRateOut = 4;
        break;
    case gISDN:
        sn_maxRateIn  = 8;
        sn_maxRateOut = 8;
        break;
    case gDSL:
        sn_maxRateIn  = 64;
        sn_maxRateOut = 16;
        break;
    case gLeave:
        break;
    }

    // store color
    if( ! (color == leave) )
    {
        player->rgb[0] = int(color.r_*15);
        player->rgb[1] = int(color.g_*15);
        player->rgb[2] = int(color.b_*15);
    }

    // load keyboard layout
    if( keyboardTemplate.Len() > 1 )
    {
        std::ostringstream fullName;
        // SDL3: Use sdl2 folder for keyboard configs (compatible with SDL3)
        fullName << "sdl2/";
        fullName << keyboardTemplate;

        std::ifstream s;
        if( tConfItemBase::OpenFile( s, fullName.str() ) )
        {
            tCurrentAccessLevel level( tAccessLevel_Owner, true );
            tConfItemBase::ReadFile( s );
        }
    }
}

#ifndef DEDICATED
static void welcome(){
    bool textOutBack = sr_textOut;
    sr_textOut = false;

#ifdef DEBUG_XXXX
    {
        for (int i = 20; i>=0; i--)
        {
            rRenderFrame([&]() {
                rTextField c(-.8,.6, .1, .1);
                tString s;
                s << ColorString(1,1,1);
                s << "Test";
                s << ColorString(1,0,0);
                s << "bla bla blubb blaa blaa blubbb blaaa blaaa blubbbb blaaaa blaaaa blubbbbb blaaaaa blaaaaa blubbbbbb blaaaaaa\n";
                c << s;
            });
        }
    }
#endif

    REAL timeout = tSysTimeFloat() + .2;
    SDL_Event tEvent;

    if (st_FirstUse)
    {
        sr_LoadDefaultConfig();
        textOutBack = sr_textOut;
        sr_textOut = false;
        gLogo::SetBig(false);
        gLogo::SetSpinning(true);
    }
    else
    {
        bool showSplash = true;
#ifdef DEBUG
        showSplash = false;
#endif

        // Start the music up
        auto& mixer = eSoundMixer::GetMixer();
        mixer.SetMode(TITLE_TRACK);
        mixer.Update();

        // disable splash screen when recording (it's annoying)
        static const char * splashSection = "SPLASH";
        if ( tRecorder::IsRunning() )
        {
            showSplash = false;

            // but keep it for old recordings where the splash screen was always active
            if ( !tRecorder::Playback( splashSection, showSplash ) )
                showSplash = tRecorder::IsPlayingBack();
        }

#ifndef DEDICATED
        if ( sg_jumpStartAnalyzer.ShouldConnect() )
        {
            showSplash = false;
            gLogo::SetDisplayed(false);
            sg_jumpStartAnalyzer.Connect();
        }
#endif

#ifdef MACOSX_XCODE
        sg_StartAAURLHandler( showSplash );
#endif
        tRecorder::Record( splashSection, showSplash );

        if ( showSplash )
        {
            timeout = tSysTimeFloat() + 6;

            uInputProcessGuard inputProcessGuard;
            while((!su_GetSDLInput(tEvent)
                   || (tEvent.type != SDL_EVENT_KEY_DOWN
                       && tEvent.type != SDL_EVENT_MOUSE_BUTTON_DOWN
                       && tEvent.type != SDL_EVENT_FINGER_DOWN)) &&
                    tSysTimeFloat() < timeout)
            {
                if ( sr_glOut )
                {
                    rRenderFrame([&]() {
                        sr_ResetRenderState(true);
                        rViewport::s_viewportFullscreen.Select();
                        uMenu::GenericBackground();
                    });
                }

                tAdvanceFrame();
            }
        }

        // catch some keyboard input
        {
            uInputProcessGuard inputProcessGuard;
            while (su_GetSDLInput(tEvent)) ;
        }

        sr_textOut = textOutBack;
        return;
    }

    if ( sr_glOut )
    {
        rRenderFrame([&]() {
            //        rFont::s_defaultFont.Select();
            //        rFont::s_defaultFontSmall.Select();
            gLogo::Display();
            // Note: Original code had a second ClearGL() here before SwapGL
            // This unusual pattern might have been for double-buffering setup
            // Now handled by rRenderFrame lifecycle
        });
    }
    else
    {
        rSysDep::SwapGL();
    }

    sr_textOut = textOutBack;
    sg_StartupLanguageMenu();

    sr_textOut = textOutBack;
    sg_StartupPlayerMenu();

    st_FirstUse=false;


    sr_textOut = textOutBack;
}
#endif

void cleanup(eGrid *grid){
    static bool reentry=true;
    if (reentry){
        reentry=false;
        su_contInput=false;

        exit_game_objects(grid);
        /*
          for(int i=MAX_PLAYERS-1;i>=0;i--){
          if (playerConfig[i])
          destroy(playerConfig[i]->cam);
          }


          gNetPlayerWall::Clear();

          eFace::Clear();
          eEdge::Clear();
          ePoint::Clear();

          eFace::Clear();
          eEdge::Clear();
          ePoint::Clear();

          eGameObject::DeleteAll();


        */

#ifdef POWERPAK_DEB
        if (pp_out){
            PD_Quit();
            PP_Quit();
        }
#endif
        nNetObject::ClearAll();

#ifndef DEDICATED
        gMoviepackManager::Destroy();
#endif

        if (sr_glOut){
            rITexture::UnloadAll();
        }

        sr_glOut=false;
        sr_ExitDisplay();

#ifndef DEDICATED
        sr_RendererCleanup();
#endif

    }
}

#ifndef DEDICATED
static bool sg_active = true;
static void sg_DelayedActivation()
{
    Activate( sg_active );
}

// SDL3: Event filter callback returns bool
bool filter(void*, SDL_Event *tEvent){
    // recursion avoidance
    static bool recursion = false;
    if ( !recursion )
    {
        class RecursionGuard
        {
        public:
            RecursionGuard( bool& recursion )
                    :recursion_( recursion )
            {
                recursion = true;
            }

            ~RecursionGuard()
            {
                recursion_ = false;
            }

        private:
            bool& recursion_;
        };

        RecursionGuard guard( recursion );

        // boss key or OS X quit command
        if ((tEvent->type==SDL_EVENT_KEY_DOWN && tEvent->key.key==SDLK_ESCAPE &&
                tEvent->key.mod & SDL_KMOD_SHIFT) ||
                (tEvent->type==SDL_EVENT_KEY_DOWN && tEvent->key.key==SDLK_Q &&
                 tEvent->key.mod & SDL_KMOD_GUI) ||
                (tEvent->type==SDL_EVENT_QUIT)){
            // sn_SetNetState(nSTANDALONE);
            // sn_Receive();

            // register end of recording
            tRecorder::Record("END");

            st_SaveConfig();
            uMenu::quickexit=uMenu::QuickExit_Total;
            return false;
        }

#if defined(__APPLE__) && TARGET_OS_IOS
        // iOS background / foreground lifecycle
        if (tEvent->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
        {
            // Save config — OS may kill the app while in background without notice.
            st_SaveConfig();
            // Pause Vulkan rendering: vkAcquireNextImageKHR and vkWaitForFences
            // with UINT64_MAX would block indefinitely when no Metal drawable is
            // available, causing the OS to terminate the blocked main thread and
            // display a black screen during the home-screen transition animation.
            sr_vkSetAppInBackground(true);
        }
        if (tEvent->type == SDL_EVENT_DID_ENTER_FOREGROUND)
        {
            // Resume rendering and force a swapchain recreation: the Metal surface
            // dimensions may have changed (e.g. rotation) while in the background.
            sr_vkSetAppInBackground(false);
            sr_vkRequestSwapchainRecreation();
        }
#endif

        if(tEvent->type==SDL_EVENT_MOUSE_MOTION)
            if(static_cast<int>(tEvent->motion.x)==sr_screenWidth/2 && static_cast<int>(tEvent->motion.y)==sr_screenHeight/2)
                return false;
        if (su_mouseGrab &&
                tEvent->type!=SDL_EVENT_MOUSE_BUTTON_DOWN &&
                tEvent->type!=SDL_EVENT_MOUSE_BUTTON_UP &&
                ((static_cast<int>(tEvent->motion.x)>=sr_screenWidth-10  || static_cast<int>(tEvent->motion.x)<=10) ||
                 (static_cast<int>(tEvent->motion.y)>=sr_screenHeight-10 || static_cast<int>(tEvent->motion.y)<=10)))
            SDL_WarpMouseInWindow(sr_screen, static_cast<float>(sr_screenWidth/2), static_cast<float>(sr_screenHeight/2));

        // fetch alt-tab (SDL3 has separate window event types)

        if (tEvent->type==SDL_EVENT_WINDOW_FOCUS_GAINED || tEvent->type==SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            // Jonathans fullscreen bugfix.
#ifdef MACOSX
            if(currentScreensetting.fullscreen ^ lastSuccess.fullscreen) return false;
#endif
            // reload GL stuff if application gets reactivated
            if ( tEvent->type == SDL_EVENT_WINDOW_FOCUS_GAINED )
            {
                // just treat it like a screen mode change, gets the job done
                st_ToDo(rCallbackBeforeScreenModeChange::Exec);
                st_ToDo(rCallbackAfterScreenModeChange::Exec);
            }
            return false;
        }

        if (su_prefetchInput){
            // SDL3: TEXT_INPUT and TEXT_EDITING events have a dynamically-allocated
            // text.text pointer that is linked to the SDL event queue entry. If we
            // copy the event struct into our own tEvents[] queue, the pointer becomes
            // dangling when SDL frees its temporary memory on the next SDL_PollEvent
            // call (SDL_FreeTemporaryMemory is called at the start of SDL_PumpEvents).
            // Skip these events so they stay in SDL's queue and are returned directly
            // by SDL_PollEvent, where the pointer is guaranteed to remain valid for
            // the duration of the call.
            if (tEvent->type == SDL_EVENT_TEXT_INPUT ||
                tEvent->type == SDL_EVENT_TEXT_EDITING ||
                tEvent->type == SDL_EVENT_TEXT_EDITING_CANDIDATES)
                return true;
            return su_StoreSDLEvent(*tEvent) != 0;
        }

    }

    return true;
}
#endif

//from game.C
void Update_netPlayer();

void sg_SetIcon()
{
#ifndef DEDICATED
#ifndef MACOSX
#ifdef  WIN32_X
    SDL_SysWMinfo	info;
    HICON			icon;
    // get the HWND handle
    SDL_VERSION( &info.version );
    if( SDL_GetWMInfo( &info ) )
    {
        icon = LoadIcon( GetModuleHandle( NULL ), MAKEINTRESOURCE( 1 ) );
        SetClassLong( info.window, GCL_HICON, (LONG) icon );
    }
#else
    rSurface tex( "textures/icon.png" );

    if (tex.GetSurface())
        SDL_SetWindowIcon(sr_screen, tex.GetSurface());
#endif
#endif
#endif
}

class gAutoStringArray
{
public:
    ~gAutoStringArray()
    {
#ifdef HAVE_CLEARENV
        // Optional. Systems that don't have this function better make copies of putenv() arguments.
        clearenv();
#endif

        for ( std::vector< char * >::iterator i = strings.begin(); i != strings.end(); ++i )
        {
            free( *i );
        }
    }

    char * Store( char const * s )
    {
        char * ret = strdup( s );
        strings.push_back( ret );
        return ret;
    }
private:
    std::vector< char * > strings; // the stored raw C strings
};

// wrapper for putenv, taking care of the peculiarity that the argument
// is kept in use for the rest of the program's lifetime
void sg_PutEnv( char const * s )
{
    static gAutoStringArray store;
    putenv( store.Store( s ) );
}

namespace
{
tString sn_configurationSavedInVersion{"0.2.8"};
tConfItem<tString> sn_configurationSavedInVersionConf("SAVED_IN_VERSION",sn_configurationSavedInVersion);

#ifndef DEDICATED
struct SDLCleanup
{
    // no init, that requires parameters and gives a return
    ~SDLCleanup(){SDL_Quit();}
};
struct SDLSoundCleanup
{
    ~SDLSoundCleanup(){eSoundMixer::ShutDown();}
};
#endif
}

int main(int argc,char **argv){
    //std::cout << "enter\n";
    //  net_test();

    bool dedicatedServer = false;

    //  std::cout << "Running " << argv[0] << "...\n";

    // tERR_MESSAGE( "Start!" );

    try
    {
        // Create this command line analyzer here instead of statically
        // so it will be the first to display in --help.
        tDefaultCommandLineAnalyzer defaultCommandLineAnalyzer;
        tCommandLineData commandLine( st_programVersion );

        // analyse command line
        // tERR_MESSAGE( "Analyzing command line." );
        if ( !commandLine.Analyse(argc, argv) )
        {
            return 0;
        }

        // Early-exit for --compile-moviepack: run as standalone tool, skip all game init.
        // tDirectories::Data() is ready after commandLine.Analyse (DoInitialize runs there).
#if !defined(DEDICATED) && defined(HAVE_SHADERC_SHADERC_HPP)
        if ( commandLineAnalyzer.compileMoviepackPath_.Len() > 1 )
        {
            bool ok = sr_CompileMoviepack( commandLineAnalyzer.compileMoviepackPath_.c_str() );
            return ok ? 0 : 1;
        }
#endif

        {
            // embed version in recording
            const char * versionSection = "VERSION";
            tString version( st_programVersion );
            tRecorder::Playback( versionSection, version );
            tRecorder::Record( versionSection, version );
#ifndef DEDICATED
            if(version != st_programVersion)
            {
#ifdef DEBUG
                tERR_WARN( "Recording from a different version, consider at high risk of desync." );
#endif
                tRecorder::ActivateProbablyDesyncedPlayback();
            }
#endif
        }

        {
            // read/write server/client mode from/to recording
            const char * dedicatedSection = "DEDICATED";
            if ( !tRecorder::PlaybackStrict( dedicatedSection, dedicatedServer ) )
            {
#ifdef DEDICATED
                dedicatedServer = true;
#endif
            }
            tRecorder::Record( dedicatedSection, dedicatedServer );
        }


        // while DGA mouse is buggy in XFree 4.0:
#ifdef linux
        // Sam 5/23 - Don't ever use DGA, we don't need it for this game.
        // no longer needed, the bug this compensated was fixed a long time
        // ago.
        /*
        if ( ! getenv("SDL_VIDEO_X11_DGAMOUSE") ) {
            sg_PutEnv("SDL_VIDEO_X11_DGAMOUSE=0");
        }
        */
#endif

        // atexit(ANET_Shutdown);

#ifndef WIN32
#ifdef DEBUG
#define NOSOUND
#endif
#endif

#ifndef DEDICATED
        // SDL3: SDL_Init returns true on success (opposite of SDL2)
        // SDL3: SDL_INIT_NOPARACHUTE was removed (always enabled in debug builds)
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
        // Force landscape orientation via SDL3 hint (must be set before SDL_Init).
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
        // Map touch finger events to mouse events so all existing click/button handlers work.
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            tERR_ERROR("Couldn't initialize SDL: " << SDL_GetError());
        }
        atexit(SDL_Quit);

#ifdef __ANDROID__
        // Set up writable user data directory now that SDL is initialized.
        tDirectories::InitAndroid();
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
        // Set up iOS bundle + documents paths now that SDL is initialized.
        tDirectories::InitiOS();
#endif

        // su_KeyInit();
        su_KeyInit();

#ifndef NOJOYSTICK
        // SDL3: SDL_InitSubSystem returns true on success (opposite of SDL2)
        if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
            std::cout << "Error initializing joystick subsystem\n";
        else
        {
#ifdef DEBUG
            // std::cout << "Joystick(s) initialized\n";
#endif // DEBUG
            su_JoystickInit();
        }
#endif // NOJOYSTICK
#endif // DEDICATED

        // tERR_MESSAGE( "Initializing player data." );
        ePlayer::Init();


        // tERR_MESSAGE( "Loading configuration." );
        tLocale::Load("languages.txt");

        eLadderLogInitializer ladderlog;
        st_LoadConfig();
        su_EnableTouchDefault();  // set ENABLE_TOUCH default on mobile after config load

        // migrate user configuration from previous versions
        if(sn_configurationSavedInVersion != st_programVersion)
        {
            if(st_FirstUse)
            {
                sn_configurationSavedInVersion = "0.0";
            }

            tConfigMigration::Migrate(sn_configurationSavedInVersion);
        }
        if(tConfigMigration::SavedBefore(sn_configurationSavedInVersion, st_programVersion))
            sn_configurationSavedInVersion = st_programVersion;

        // record and play back the recording debug level
        tRecorderSyncBase::GetDebugLevelPlayback();

        if ( commandLineAnalyzer.fullscreen_ )
            currentScreensetting.fullscreen   = true;
        if ( commandLineAnalyzer.windowed_ )
            currentScreensetting.fullscreen   = false;
        if ( commandLineAnalyzer.use_directx_ )
            sr_useDirectX                       = true;
        if ( commandLineAnalyzer.dont_use_directx_ )
            sr_useDirectX                       = false;
#ifndef DEDICATED
        if ( commandLineAnalyzer.screenshotFrame_ >= 0 )
            sr_SetGoldenScreenshot( commandLineAnalyzer.screenshotFrame_,
                                    commandLineAnalyzer.screenshotOut_.c_str() );
        if ( commandLineAnalyzer.exitAfterFrame_ >= 0 )
            sr_SetExitAfterFrame( commandLineAnalyzer.exitAfterFrame_ );
#endif

        //gAICharacter::LoadAll(tString( "aiplayers.cfg" ) );
        gAICharacter::LoadAll( aiPlayersConfig );

        sg_LanguageInit();
        atexit(tLocale::Clear);

        if ( commandLine.Execute() )
        {
                gCycle::PrivateSettings();

            {
                std::ifstream t;

                        if ( !tDirectories::Config().Open( t, "settings.cfg" ) )
                {
                    //		#ifdef WIN32
                    //                    tERR_ERROR( "Data files not found. You have to run Armagetron from its own directory." );
                    //		#else
                    tERR_ERROR( "Configuration files not found. Check your installation." );
                    //		#endif
                }
                    }

            {
                std::ofstream s;
                        if (! tDirectories::Var().Open( s, "scorelog.txt", std::ios::app ) )
                {
                    char const * error = "var directory not writable or does not exist. It should reside inside your user data directory and should have been created automatically on first start, but something must have gone wrong."
                    #ifdef WIN32
                                         " You can access your user data directory over one of the start menu entries we installed."
                    #else
                                         " Your user data directory is subdirectory named .armagetronad in your home directory."
                    #endif
                                         ;

                    tERR_ERROR( error );
                }
                    }

            {
                std::ifstream t;

                if ( tDirectories::Data().Open( t, "moviepack/settings.cfg" ) )
                {
                    sg_moviepackInstalled=true;
                }
            }

            // Scan for available moviepacks (legacy folder + zip files)
            gMoviepackManager::Get().ScanMoviepacks();

#ifndef DEDICATED
            sr_glOut=1;
            //std::cout << "checked mp\n";

            SDLCleanup sdlCleanup; // call SDL_Quit later

                sr_vkRendererInit();

            // ScanMoviepacks() ran before sr_glOut was set, so the renderer-
            // dependent part of ActivateMoviepack() (PP activation, effect
            // search path) was skipped. Re-apply it now that the renderer is up.
            gMoviepackManager::Get().NotifyRendererReady();

            SDL_SetEventFilter(&filter, 0);
            //std::cout << "set filter\n";

            tConsole::RegisterMessageCallback(&uMenu::Message);
            tConsole::RegisterIdleCallback(&uMenu::IdleInput);

#ifndef NOSOUND
                SDLSoundCleanup soundInitAndCleanup; // se_SoundInit() now, se_SoundExit() later
    #endif

                if (sr_InitDisplay()){

                sg_SetIcon();

                try
                {
                    con << "Status: Using Vulkan renderer\n";

                    //std::cout << "init disp\n";

                    //std::cout << "init sound\n";

                    welcome();

                    //std::cout << "atexit\n";

                    sr_con.autoDisplayAtSwap=false;

                    //std::cout << "sound started\n";

                    gLogo::SetBig(false);
                    gLogo::SetSpinning(true);

                    sn_bigBrotherString = renderer_identification + "VER=" + st_programVersion + "\n\n";

                    // Route all eLadderLog game events to Lua on_<event>(args) functions.
                    // Registered once here because gArmagetron.cpp has access to both
                    // the engine (eLadderLog) and render (tLuaState) layers.
#ifndef DEDICATED
                    eLadderLogWriter::SetScriptHook(&tLuaState::DispatchEvent);
                    gRegisterLuaBindings(tLuaState::Instance().View().raw());
#endif

                    // Load startup Lua script if present (game-scripting equivalent of old Ruby initialize.rb)
                    if (tLuaState::Instance().IsAlive()) {
                        tString luaInit = tDirectories::Data().GetReadPath("scripts/initialize.lua");
                        if (luaInit.Len() > 1)
                            tLuaState::Instance().DoFile(luaInit.c_str());
                    }

                    MainMenu();

                    // remove all players
                    for ( int i = se_PlayerNetIDs.Len()-1; i>=0; --i )
                        se_PlayerNetIDs(i)->RemoveFromGame();

                    nNetObject::ClearAll();

                    rITexture::UnloadAll();
                }
                catch (tException const & e)
                {
                    gLogo::SetDisplayed(true);
                    uMenu::SetIdle(NULL);
                    sr_con.autoDisplayAtSwap=false;

                    // inform user of generic errors
                    tConsole::Message( e.GetName(), e.GetDescription(), 20 );
                }

                // Save config before tearing down the display so we don't stall
                // with a black screen while writing files.
                st_SaveConfig();

                // Free moviepack singleton state while the renderer is still alive
                // so cached preview/title texture destructors can call RenderDeleteTexture.
                gMoviepackManager::Destroy();

                // Destroy the Vulkan renderer BEFORE destroying the SDL window.
                // On iOS/MoltenVK the surface is backed by a CAMetalLayer owned by
                // the SDL UIWindow; destroying the window first can cause
                // vkDeviceWaitIdle (called inside the renderer destructor) to stall
                // on a surface whose Metal layer is no longer active, which leaves a
                // black frame visible during the iOS exit transition animation.
                sr_RendererCleanup();
                sr_ExitDisplay();

#if defined(__APPLE__) && TARGET_OS_IOS
                // On iOS, terminate the process immediately after the window is gone.
                // _exit() skips C++ static destructors so detached boost::thread
                // font-glyph workers cannot race against std::mutex teardown
                // (which causes "mutex lock failed: Invalid argument").
                // The OS reclaims all process resources, so this is safe.
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                _exit(0);
#endif

                //std::cout << "saved\n";

                //    cleanup(grid);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }


#else // DEDICATED
            sr_glOut=0;

            //  nServerInfo::TellMasterAboutMe();

            while (!uMenu::quickexit)
                sg_HostGame();
#endif // DEDICATED
            nNetObject::ClearAll();
            nServerInfo::DeleteAll();
        }

        ePlayer::Exit();


        //	tLocale::Clear();
    }
    catch ( tCleanQuit const & e )
    {
        return 0;
    }
    catch ( tException const & e )
    {
        try
        {
            st_PresentError( e.GetName(), e.GetDescription() );
        }
        catch(...)
        {
        }

        return 1;
    }
    catch ( std::exception & e )
    {
        try
        {
            st_PresentError("", e.what());
        }
        catch(...)
        {
        }
    }
#ifdef _MSC_VER
#pragma warning ( disable : 4286 )
    // GRR. Visual C++ dones not handle generic exceptions with the above general statement.
    // A specialized version is needed. The best part: it warns about the code below being redundant.
    catch( tGenericException const & e )
    {
        try
        {
            st_PresentError( e.GetName(), e.GetDescription() );
        }
        catch(...)
        {
        }

        return 1;
    }
#endif
    catch(...)
    {
        return 1;
    }

    return 0;
}

#ifdef DEDICATED
// settings missing in the dedicated server
static void st_Dummy(std::istream &s){tString rest; rest.ReadLine(s);}
static tConfItemFunc st_Dummy10("MASTER_QUERY_INTERVAL", &st_Dummy);
static tConfItemFunc st_Dummy11("MASTER_SAVE_INTERVAL", &st_Dummy);
static tConfItemFunc st_Dummy12("MASTER_IDLE", &st_Dummy);
static tConfItemFunc st_Dummy13("MASTER_PORT", &st_Dummy);
#endif



