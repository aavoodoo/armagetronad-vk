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

#ifndef ArmageTron_rWindow_H
#define ArmageTron_rWindow_H

#include "tString.h"
#include <memory>

//! Window creation flags
enum class rWindowFlags : uint32_t
{
    None = 0,
    Fullscreen = 1 << 0,
    FullscreenDesktop = 1 << 1,
    OpenGL = 1 << 2,
    Resizable = 1 << 3,
    Borderless = 1 << 4,
    Hidden = 1 << 5,
    HighDPI = 1 << 6
};

//! Combine window flags
inline rWindowFlags operator|(rWindowFlags a, rWindowFlags b)
{
    return static_cast<rWindowFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(rWindowFlags a, rWindowFlags b)
{
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

//! Display mode information
struct rDisplayMode
{
    int width;
    int height;
    int refreshRate;
    uint32_t format;
};

//! Abstract window interface
//! Platform-specific implementations provide window management functionality.
class rWindow
{
public:
    virtual ~rWindow() = default;

    //! Create and show the window
    //! @param title Window title
    //! @param x X position (or centered)
    //! @param y Y position (or centered)
    //! @param width Window width
    //! @param height Window height
    //! @param flags Window creation flags
    //! @return true on success
    virtual bool Create(const tString& title, int x, int y, int width, int height, rWindowFlags flags) = 0;

    //! Destroy the window
    virtual void Destroy() = 0;

    //! Check if window is valid/created
    virtual bool IsValid() const = 0;

    //! Set window title
    virtual void SetTitle(const tString& title) = 0;

    //! Set window size
    virtual void SetSize(int width, int height) = 0;

    //! Get window size
    virtual void GetSize(int& width, int& height) const = 0;

    //! Get drawable size (may differ from window size on high-DPI displays)
    virtual void GetDrawableSize(int& width, int& height) const = 0;

    //! Set window position
    virtual void SetPosition(int x, int y) = 0;

    //! Get window position
    virtual void GetPosition(int& x, int& y) const = 0;

    //! Set fullscreen mode
    //! @param fullscreen true for fullscreen, false for windowed
    //! @param desktop true for desktop fullscreen (no mode change)
    //! @return true on success
    virtual bool SetFullscreen(bool fullscreen, bool desktop = true) = 0;

    //! Check if window is fullscreen
    virtual bool IsFullscreen() const = 0;

    //! Set display mode (resolution, refresh rate)
    virtual bool SetDisplayMode(const rDisplayMode& mode) = 0;

    //! Get current display mode
    virtual rDisplayMode GetDisplayMode() const = 0;

    //! Get desktop display mode
    virtual rDisplayMode GetDesktopDisplayMode(int displayIndex = 0) const = 0;

    //! Get the number of displays
    virtual int GetNumDisplays() const = 0;

    //! Get the display index this window is on
    virtual int GetDisplayIndex() const = 0;

    //! Swap buffers (present frame)
    virtual void SwapBuffers() = 0;

    //! Minimize the window
    virtual void Minimize() = 0;

    //! Restore the window
    virtual void Restore() = 0;

    //! Raise the window to front
    virtual void Raise() = 0;

    //! Lock window operations (thread safety)
    virtual void Lock() = 0;

    //! Unlock window operations
    virtual void Unlock() = 0;

    //! Special position values
    static constexpr int PositionCentered = -1;
    static constexpr int PositionUndefined = -2;
};

//! Get the current window instance
//! @return Reference to the active window
rWindow& rGetWindow();

//! Initialize the window system
//! @param forDedicatedServer If true, creates null window for dedicated server
void rInitWindow(bool forDedicatedServer = false);

//! Shutdown the window system
void rShutdownWindow();

#endif // ArmageTron_rWindow_H
