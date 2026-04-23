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
#include "eCoord.h"
#include <cassert>
#include "tConfiguration.h"
#include "tResourceManager.h"
#include "uInput.h"
#include "tInitExit.h"
#include "nConfig.h"

#include <sstream>

#include "rSDL.h"
#include "rScreen.h"
#ifndef DEDICATED
#include "rRender.h"
#ifdef MACOSX
#   include <ApplicationServices/ApplicationServices.h>
#endif
#endif

bool pp_out=1; // or 2d-output?
bool pp_tess_deb=0;

// network setting item for the reesource repository from tResourceManager.cpp
// may be dangerous, so it's disabled for now.
static nSettingItem<tString> conf_res_repo("RESOURCE_REPOSITORY_SERVER", tResourceManager::AccessRepoServer());

static tConfItem<bool> grab("MOUSE_GRAB",su_mouseGrab);

bool sg_moviepackInstalled=false; // do we have the mp on disk?
bool sg_moviepackUse=true;       // do we use it?
static tConfItem<bool> ump("MOVIEPACK",sg_moviepackUse);

// setting item for double bind (available since version 7)
static nSettingItemWatched<REAL> su_doubleBindTimeoutConf( "DOUBLEBIND_TIME", su_doubleBindTimeout, nConfItemVersionWatcher::Group_Cheating, 7  );

bool sg_MoviePack(){
    // Check legacy flags - these are now managed by gMoviepackManager
    return sg_moviepackInstalled && sg_moviepackUse;
}

// Wrap a string in single quotes for shell use, escaping any embedded single quotes.
static std::string sg_ShellQuote( char const * s )
{
    std::string out = "'";
    for ( const char * p = s; *p; ++p )
    {
        if ( *p == '\'' )
            out += "'\\''";
        else
            out += *p;
    }
    out += "'";
    return out;
}

static bool sg_OpenStuff( char const * uri, bool tryBrowser )
{
#ifndef DEDICATED
    if( currentScreensetting.fullscreen )
    {
        // iconify; otherwise, the screen freezes while the browser is started,
        // and on Linux, the game gets window-ified without being noticed about it.
        SDL_MinimizeWindow(sr_screen);
    }
#endif

    // bool success = false;
#ifdef WIN32
    ShellExecute(NULL, "open", uri, NULL, NULL, SW_SHOWNORMAL);
#else
    // general unix
    std::ostringstream s; // composing a command
    std::string quotedURI = sg_ShellQuote( uri );
#if defined(__APPLE__) && TARGET_OS_IOS
    // iOS: system() is not available. Use SDL3's URL opener instead.
    (void)tryBrowser;
    return SDL_OpenURL( uri );
#elif defined(MACOSX)
    if ( tryBrowser )
        return false;
    s << "open " << quotedURI << " || "
      << "safari " << quotedURI << " &";
    // execute command
    return  0 == system( s.str().c_str() );
#else
    if( tryBrowser )
    {
        s << "x-www-browser " << quotedURI << " || ";
    }
    s << "xdg-open " << quotedURI << " || " << "firefox " << quotedURI << " &";
    // execute command
    return  0 == system( s.str().c_str() );
#endif
#endif
    return true;
}

bool sg_OpenURI( char const * uri )
{
#ifdef MACOSX
#ifndef DEDICATED
    CFURLRef URL = CFURLCreateWithBytes( NULL, reinterpret_cast<UInt8 const *>(uri), strlen(uri), kCFStringEncodingUTF8, NULL );
    LSOpenCFURLRef( URL, NULL );
    CFRelease( URL );
#endif
	return true;
#else
    return sg_OpenStuff( uri, true );
#endif
}

bool sg_OpenDirectory( char const * path )
{
    return sg_OpenStuff( path, false );
}

//const eCoord se_zeroCoord(0,0);

