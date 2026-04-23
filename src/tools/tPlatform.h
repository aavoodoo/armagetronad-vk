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

#ifndef ArmageTron_Platform_H
#define ArmageTron_Platform_H

#include "tPlatformTypes.h"
#include <memory>

//! Abstract platform interface
//! Provides platform-agnostic access to system services like timing and sleep.
//! Implementations exist for graphical client and null (dedicated server).
class tPlatform
{
public:
    virtual ~tPlatform() = default;

    //! Get platform capabilities
    virtual tPlatformCaps GetCapabilities() const = 0;

    //! Get current monotonic time
    //! @param[out] time The current time relative to an arbitrary start point
    virtual void GetTime(tPlatformTime& time) const = 0;

    //! Sleep for the specified duration
    //! @param microseconds Number of microseconds to sleep
    virtual void Sleep(int microseconds) const = 0;

    //! Check if timer has high resolution (better than millisecond)
    virtual bool IsTimerAccurate() const = 0;

    //! Check if timer is strictly monotonic
    virtual bool IsTimerMonotonic() const = 0;

    // Future expansion points (not implemented in Sprint 2):
    // virtual void PollEvents() = 0;
    // virtual void* CreateWindow(...) = 0;
    // etc.

protected:
    tPlatform() = default;
    tPlatform(const tPlatform&) = delete;
    tPlatform& operator=(const tPlatform&) = delete;
};

//! Get the global platform instance
//! @return Reference to the platform singleton
tPlatform& tGetPlatform();

//! Initialize the platform layer
//! Must be called before tGetPlatform() is used.
//! @param forDedicatedServer Set to true for dedicated server (no graphics)
void tInitPlatform(bool forDedicatedServer = false);

//! Shutdown the platform layer
void tShutdownPlatform();

#endif // ArmageTron_Platform_H
