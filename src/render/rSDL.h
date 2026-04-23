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

SDL3 Header - provides SDL3 types and compatibility helpers

*/

#ifndef AT_SDL_H
#define AT_SDL_H

#include "aa_config.h"

// SDL3-only build
#ifndef AA_SDL3
#define AA_SDL3 1
#endif

#ifndef DEDICATED

#include <SDL3/SDL.h>

// =============================================================================
// SDL3 Helper Functions
// =============================================================================

// Display enumeration helpers
inline int AA_GetNumVideoDisplays()
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays)
    {
        SDL_free(displays);
    }
    return count;
}

inline SDL_DisplayID AA_GetDisplayID(int displayIndex)
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID result = SDL_GetPrimaryDisplay();
    if (displays)
    {
        if (displayIndex >= 0 && displayIndex < count)
        {
            result = displays[displayIndex];
        }
        SDL_free(displays);
    }
    return result;
}

inline int AA_GetWindowDisplayIndex(SDL_Window* window)
{
    SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays)
    {
        for (int i = 0; i < count; i++)
        {
            if (displays[i] == displayID)
            {
                SDL_free(displays);
                return i;
            }
        }
        SDL_free(displays);
    }
    return 0;
}

// Surface format helpers (SDL3 changed format from pointer to enum)
inline const SDL_PixelFormatDetails* AA_GetSurfaceFormatDetails(SDL_Surface* surface)
{
    return SDL_GetPixelFormatDetails(surface->format);
}

inline int AA_GetSurfaceBytesPerPixel(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->bytes_per_pixel : 0;
}

inline int AA_GetSurfaceBitsPerPixel(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->bits_per_pixel : 0;
}

inline Uint32 AA_GetSurfaceRmask(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->Rmask : 0;
}

inline Uint32 AA_GetSurfaceGmask(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->Gmask : 0;
}

inline Uint32 AA_GetSurfaceBmask(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->Bmask : 0;
}

inline Uint32 AA_GetSurfaceAmask(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = AA_GetSurfaceFormatDetails(surface);
    return details ? details->Amask : 0;
}

inline SDL_PixelFormat AA_GetSurfaceFormat(SDL_Surface* surface)
{
    return surface->format;
}

// Key event access macros
#define AA_KEY_SYM(event) ((event).key.key)
#define AA_KEY_SCANCODE(event) ((event).key.scancode)
#define AA_KEY_MOD(event) ((event).key.mod)
#define AA_KEY_DOWN(event) ((event).key.down)

// SDL3 uses nanosecond timestamps - convert to milliseconds
inline Uint32 AA_GetEventTimestampMs(const SDL_Event& event)
{
    return static_cast<Uint32>(event.common.timestamp / 1000000);
}

#else // DEDICATED server stubs

#define SDLK_LAST 300
#define SDL_NUM_SCANCODES 300

typedef int SDL_Event;
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef signed short Sint16;
typedef unsigned int Uint32;
typedef signed int Sint32;
typedef unsigned long long Uint64;
typedef int SDL_AudioSpec;
typedef int SDL_Window;
typedef int SDL_Surface;
typedef int SDL_PixelFormat;

// Stub functions for dedicated server
inline int AA_GetNumVideoDisplays() { return 0; }
inline int AA_GetSurfaceBytesPerPixel(SDL_Surface*) { return 0; }
inline int AA_GetSurfaceBitsPerPixel(SDL_Surface*) { return 0; }
inline Uint32 AA_GetSurfaceRmask(SDL_Surface*) { return 0; }
inline Uint32 AA_GetSurfaceGmask(SDL_Surface*) { return 0; }
inline Uint32 AA_GetSurfaceBmask(SDL_Surface*) { return 0; }
inline Uint32 AA_GetSurfaceAmask(SDL_Surface*) { return 0; }

#define AA_KEY_SYM(event) 0
#define AA_KEY_SCANCODE(event) 0
#define AA_KEY_MOD(event) 0
#define AA_KEY_DOWN(event) false

#endif // DEDICATED

#endif // AT_SDL_H
