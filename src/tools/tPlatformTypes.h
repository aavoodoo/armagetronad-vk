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

#ifndef ArmageTron_PlatformTypes_H
#define ArmageTron_PlatformTypes_H

#include <cstdint>

//! Platform-agnostic time structure
//! Represents a point in time or time duration with microsecond precision
struct tPlatformTime
{
    int64_t seconds;      //!< seconds component
    int32_t microseconds; //!< microseconds component (0 to 999999)

    tPlatformTime() : seconds(0), microseconds(0) {}
    tPlatformTime(int64_t sec, int32_t usec) : seconds(sec), microseconds(usec) {}

    //! Normalize so microseconds is in range [0, 999999]
    void Normalize()
    {
        const int32_t NORMALIZER = 1000000;
        if (microseconds >= NORMALIZER || microseconds < 0)
        {
            int64_t overflow = microseconds / NORMALIZER;
            microseconds -= static_cast<int32_t>(overflow * NORMALIZER);
            seconds += overflow;

            while (microseconds < 0)
            {
                microseconds += NORMALIZER;
                seconds--;
            }
        }
    }

    tPlatformTime operator+(const tPlatformTime& other) const
    {
        tPlatformTime ret;
        ret.microseconds = microseconds + other.microseconds;
        ret.seconds = seconds + other.seconds;
        ret.Normalize();
        return ret;
    }

    tPlatformTime operator-(const tPlatformTime& other) const
    {
        tPlatformTime ret;
        ret.microseconds = microseconds - other.microseconds;
        ret.seconds = seconds - other.seconds;
        ret.Normalize();
        return ret;
    }

    //! Convert to floating-point seconds
    double ToDouble() const
    {
        return static_cast<double>(seconds) + static_cast<double>(microseconds) * 1e-6;
    }
};

//! Platform capability flags
struct tPlatformCaps
{
    bool hasHighResTimer;    //!< Timer with better than millisecond accuracy
    bool isTimerMonotonic;   //!< Timer is strictly monotonic (never goes backward)
    bool hasGraphics;        //!< Platform supports graphics (false for dedicated server)

    tPlatformCaps() : hasHighResTimer(false), isTimerMonotonic(false), hasGraphics(false) {}
};

#endif // ArmageTron_PlatformTypes_H
