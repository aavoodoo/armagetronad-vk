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

#include "aa_config.h"
#include "tPlatformSDL.h"

#ifndef DEDICATED
#ifdef HAVE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#endif

#ifdef WIN32
#include <windows.h>
#include <sys/timeb.h>
#else
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif

// Use clock_gettime if available on Linux
#if !defined(WIN32) && !defined(MACOSX)
#if HAVE_LIBRT && HAVE_TIME_H
#ifdef CLOCK_MONOTONIC
#define USE_CLOCK_GETTIME
#include <time.h>
#endif
#endif
#include <sys/time.h>
#endif

#ifdef MACOSX
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

tPlatformSDL::tPlatformSDL()
    : startTime_()
    , startTimeInitialized_(false)
    , hpcReliable_(false)
    , timerIsMonotonic_(false)
{
#ifdef WIN32
    // Check if high-resolution performance counter is available
    LARGE_INTEGER frq;
    hpcReliable_ = (QueryPerformanceFrequency(&frq) != 0);
    timerIsMonotonic_ = true; // Windows timers are monotonic
#elif defined(MACOSX)
    timerIsMonotonic_ = true;
#else
    // Linux: will be set based on which clock we use
    timerIsMonotonic_ = false;
#endif
}

tPlatformSDL::~tPlatformSDL() = default;

tPlatformCaps tPlatformSDL::GetCapabilities() const
{
    tPlatformCaps caps;
    caps.hasHighResTimer = IsTimerAccurate();
    caps.isTimerMonotonic = timerIsMonotonic_;
    caps.hasGraphics = true;
    return caps;
}

void tPlatformSDL::GetTimeInternal(tPlatformTime& time) const
{
#ifdef WIN32
    if (hpcReliable_)
    {
        LARGE_INTEGER mtime, frq;
        QueryPerformanceFrequency(&frq);
        QueryPerformanceCounter(&mtime);
        time.seconds = mtime.QuadPart / frq.QuadPart;
        time.microseconds = static_cast<int32_t>(
            ((mtime.QuadPart - time.seconds * frq.QuadPart) * 1000000) / frq.QuadPart);
    }
    else
    {
        // Fallback to GetTickCount64 (Vista+) or GetTickCount
#if _WIN32_WINNT >= 0x0600
        ULONGLONG tick64 = GetTickCount64();
#else
        static DWORD lastTickCount = GetTickCount();
        DWORD tickCount = GetTickCount();
        static uint64_t tick64 = 0;
        unsigned int tickUpdate = tickCount - lastTickCount;
        lastTickCount = tickCount;
        tick64 += tickUpdate;
#endif
        int milliseconds = tick64 % 1000;
        time.microseconds = milliseconds * 1000;
        time.seconds = (tick64 - milliseconds) / 1000;
    }

#elif defined(MACOSX)
    uint64_t machTime = mach_absolute_time();
    static uint64_t start = machTime;
    machTime -= start;

    static double machTimeToMicroseconds = 0.0;
    if (machTimeToMicroseconds == 0.0)
    {
        mach_timebase_info_data_t timebaseInfo;
        mach_timebase_info(&timebaseInfo);
        machTimeToMicroseconds = timebaseInfo.numer * 1E-3 / timebaseInfo.denom;
    }

    uint64_t microseconds = static_cast<uint64_t>(machTime * machTimeToMicroseconds);
    time.microseconds = microseconds % 1000000;
    time.seconds = (microseconds - time.microseconds) / 1000000;

#else // Linux/Unix
#ifdef USE_CLOCK_GETTIME
    struct timespec res;
    bool success = false;

#ifdef CLOCK_MONOTONIC_RAW
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &res) == 0)
    {
        success = true;
        timerIsMonotonic_ = true;
    }
    else
#endif
    {
        if (clock_gettime(CLOCK_MONOTONIC, &res) == 0)
        {
            success = true;
        }
    }

    if (success)
    {
        time.microseconds = res.tv_nsec / 1000;
        time.seconds = res.tv_sec;
        return;
    }
#endif

    // Fallback to gettimeofday
    struct timeval tp;
    struct timezone tzp;
    gettimeofday(&tp, &tzp);
    time.microseconds = tp.tv_usec;
    time.seconds = tp.tv_sec;
#endif

    time.Normalize();
}

void tPlatformSDL::GetTime(tPlatformTime& time) const
{
    tPlatformTime current;
    GetTimeInternal(current);

    if (!startTimeInitialized_)
    {
        startTime_ = current;
        startTimeInitialized_ = true;
    }

    time = current - startTime_;

#ifdef WIN32
    // Detect HPC reliability issues on Windows
    if (hpcReliable_)
    {
        static tPlatformTime lastTime;
        if ((current - lastTime).seconds < 0)
        {
            // Timer went backward, HPC is unreliable
            hpcReliable_ = false;
            GetTimeInternal(current);
            startTime_ = startTime_ + current - lastTime;
            time = current - startTime_;
        }
        lastTime = current;
    }
#endif
}

void tPlatformSDL::Sleep(int microseconds) const
{
    if (microseconds <= 0)
        return;

    static unsigned int sleepRest = 0;
    sleepRest += microseconds;
    unsigned int milliseconds = sleepRest / 1000;

#ifndef DEDICATED
    SDL_Delay(milliseconds);
#else
    // Fallback for dedicated builds (shouldn't happen with tPlatformSDL)
#ifdef WIN32
    SleepEx(milliseconds, false);
#else
    usleep(milliseconds * 1000);
#endif
#endif

    sleepRest -= milliseconds * 1000;
}

bool tPlatformSDL::IsTimerAccurate() const
{
#ifdef WIN32
    return hpcReliable_;
#else
    return true; // Always accurate on Unix/macOS
#endif
}

bool tPlatformSDL::IsTimerMonotonic() const
{
    return timerIsMonotonic_;
}
