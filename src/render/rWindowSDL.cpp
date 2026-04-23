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

#include "rWindowSDL.h"

#ifndef DEDICATED

#include <cstring>

// ============================================================================
// rWindowSDL implementation
// ============================================================================

rWindowSDL::rWindowSDL()
    : window_(nullptr)
{
}

rWindowSDL::~rWindowSDL()
{
    Destroy();
}

bool rWindowSDL::Create(const tString& title, int x, int y, int width, int height, rWindowFlags flags)
{
    if (window_)
    {
        Destroy();
    }

    Uint32 sdlFlags = 0;

    if (flags & rWindowFlags::OpenGL)
        sdlFlags |= SDL_WINDOW_OPENGL;
    if (flags & rWindowFlags::Fullscreen)
        sdlFlags |= SDL_WINDOW_FULLSCREEN;
    // SDL3: SDL_WINDOW_FULLSCREEN_DESKTOP is removed, use SDL_WINDOW_FULLSCREEN with NULL mode
    if (flags & rWindowFlags::FullscreenDesktop)
        sdlFlags |= SDL_WINDOW_FULLSCREEN;
    if (flags & rWindowFlags::Resizable)
        sdlFlags |= SDL_WINDOW_RESIZABLE;
    if (flags & rWindowFlags::Borderless)
        sdlFlags |= SDL_WINDOW_BORDERLESS;
    if (flags & rWindowFlags::Hidden)
        sdlFlags |= SDL_WINDOW_HIDDEN;
    if (flags & rWindowFlags::HighDPI)
        sdlFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    // SDL3: Window position is set separately after creation
    window_ = SDL_CreateWindow(static_cast<const char*>(title), width, height, sdlFlags);
    if (window_ && x != PositionUndefined && y != PositionUndefined)
    {
        SDL_SetWindowPosition(window_, x, y);
    }

    return window_ != nullptr;
}

void rWindowSDL::Destroy()
{
    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool rWindowSDL::IsValid() const
{
    return window_ != nullptr;
}

void rWindowSDL::SetTitle(const tString& title)
{
    if (window_)
    {
        SDL_SetWindowTitle(window_, static_cast<const char*>(title));
    }
}

void rWindowSDL::SetSize(int width, int height)
{
    if (window_)
    {
        SDL_SetWindowSize(window_, width, height);
    }
}

void rWindowSDL::GetSize(int& width, int& height) const
{
    if (window_)
    {
        SDL_GetWindowSize(window_, &width, &height);
    }
    else
    {
        width = height = 0;
    }
}

void rWindowSDL::GetDrawableSize(int& width, int& height) const
{
    if (window_)
    {
        // SDL3: Use SDL_GetWindowSizeInPixels for high-DPI aware size
        SDL_GetWindowSizeInPixels(window_, &width, &height);
    }
    else
    {
        width = height = 0;
    }
}

void rWindowSDL::SetPosition(int x, int y)
{
    if (window_)
    {
        SDL_SetWindowPosition(window_, x, y);
    }
}

void rWindowSDL::GetPosition(int& x, int& y) const
{
    if (window_)
    {
        SDL_GetWindowPosition(window_, &x, &y);
    }
    else
    {
        x = y = 0;
    }
}

bool rWindowSDL::SetFullscreen(bool fullscreen, bool desktop)
{
    if (!window_)
        return false;

    // SDL3: Use SDL_SetWindowFullscreen with bool
    // For desktop fullscreen, set fullscreen mode to NULL before enabling
    if (fullscreen && desktop)
    {
        SDL_SetWindowFullscreenMode(window_, nullptr);
    }
    return SDL_SetWindowFullscreen(window_, fullscreen);
}

bool rWindowSDL::IsFullscreen() const
{
    if (!window_)
        return false;

    Uint32 flags = SDL_GetWindowFlags(window_);
    return (flags & SDL_WINDOW_FULLSCREEN) != 0;
}

bool rWindowSDL::SetDisplayMode(const rDisplayMode& mode)
{
    if (!window_)
        return false;

    // SDL3: Find closest matching display mode (returns bool, takes output param)
    SDL_DisplayID displayID = SDL_GetDisplayForWindow(window_);
    SDL_DisplayMode closestMode;
    if (SDL_GetClosestFullscreenDisplayMode(
        displayID, mode.width, mode.height, static_cast<float>(mode.refreshRate), true, &closestMode))
    {
        return SDL_SetWindowFullscreenMode(window_, &closestMode);
    }
    return false;
}

rDisplayMode rWindowSDL::GetDisplayMode() const
{
    rDisplayMode mode = {0, 0, 0, 0};

    if (window_)
    {
        const SDL_DisplayMode* sdlMode = SDL_GetWindowFullscreenMode(window_);
        if (sdlMode)
        {
            mode.width = sdlMode->w;
            mode.height = sdlMode->h;
            mode.refreshRate = static_cast<int>(sdlMode->refresh_rate);
            mode.format = 0;
        }
    }

    return mode;
}

rDisplayMode rWindowSDL::GetDesktopDisplayMode(int displayIndex) const
{
    rDisplayMode mode = {0, 0, 0, 0};

    SDL_DisplayID displayID = AA_GetDisplayID(displayIndex);
    const SDL_DisplayMode* sdlMode = SDL_GetDesktopDisplayMode(displayID);
    if (sdlMode)
    {
        mode.width = sdlMode->w;
        mode.height = sdlMode->h;
        mode.refreshRate = static_cast<int>(sdlMode->refresh_rate);
        mode.format = 0;
    }

    return mode;
}

int rWindowSDL::GetNumDisplays() const
{
    return AA_GetNumVideoDisplays();
}

int rWindowSDL::GetDisplayIndex() const
{
    if (window_)
    {
        return AA_GetWindowDisplayIndex(window_);
    }
    return 0;
}

void rWindowSDL::SwapBuffers()
{
    if (window_)
    {
        SDL_GL_SwapWindow(window_);
    }
}

void rWindowSDL::Minimize()
{
    if (window_)
    {
        SDL_MinimizeWindow(window_);
    }
}

void rWindowSDL::Restore()
{
    if (window_)
    {
        SDL_RestoreWindow(window_);
    }
}

void rWindowSDL::Raise()
{
    if (window_)
    {
        SDL_RaiseWindow(window_);
    }
}

void rWindowSDL::Lock()
{
    mutex_.lock();
}

void rWindowSDL::Unlock()
{
    mutex_.unlock();
}

#endif // DEDICATED
