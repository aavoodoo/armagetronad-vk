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

#ifndef ArmageTron_PlatformSDL_H
#define ArmageTron_PlatformSDL_H

#include "tPlatform.h"

//! SDL-based platform implementation for graphical client builds.
//! Uses native OS APIs for timing and SDL for sleep.
class tPlatformSDL : public tPlatform
{
public:
    tPlatformSDL();
    ~tPlatformSDL() override;

    tPlatformCaps GetCapabilities() const override;
    void GetTime(tPlatformTime& time) const override;
    void Sleep(int microseconds) const override;
    bool IsTimerAccurate() const override;
    bool IsTimerMonotonic() const override;

private:
    mutable tPlatformTime startTime_;
    mutable bool startTimeInitialized_;
    mutable bool hpcReliable_;          // Windows: high-performance counter reliable
    mutable bool timerIsMonotonic_;

    void GetTimeInternal(tPlatformTime& time) const;
};

#endif // ArmageTron_PlatformSDL_H
