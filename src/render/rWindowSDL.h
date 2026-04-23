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

#ifndef ArmageTron_rWindowSDL_H
#define ArmageTron_rWindowSDL_H

#include "rWindow.h"

#ifndef DEDICATED

#include "rSDL.h"
#include <mutex>

//! SDL-based window implementation for graphical client builds
class rWindowSDL : public rWindow
{
public:
    rWindowSDL();
    ~rWindowSDL() override;

    bool Create(const tString& title, int x, int y, int width, int height, rWindowFlags flags) override;
    void Destroy() override;
    bool IsValid() const override;

    void SetTitle(const tString& title) override;
    void SetSize(int width, int height) override;
    void GetSize(int& width, int& height) const override;
    void GetDrawableSize(int& width, int& height) const override;
    void SetPosition(int x, int y) override;
    void GetPosition(int& x, int& y) const override;

    bool SetFullscreen(bool fullscreen, bool desktop = true) override;
    bool IsFullscreen() const override;
    bool SetDisplayMode(const rDisplayMode& mode) override;
    rDisplayMode GetDisplayMode() const override;
    rDisplayMode GetDesktopDisplayMode(int displayIndex = 0) const override;

    int GetNumDisplays() const override;
    int GetDisplayIndex() const override;

    void SwapBuffers() override;
    void Minimize() override;
    void Restore() override;
    void Raise() override;

    void Lock() override;
    void Unlock() override;

    //! Get the underlying SDL window (for internal use)
    SDL_Window* GetSDLWindow() const { return window_; }

private:
    SDL_Window* window_;
    std::mutex mutex_;
};

#endif // DEDICATED

#endif // ArmageTron_rWindowSDL_H
