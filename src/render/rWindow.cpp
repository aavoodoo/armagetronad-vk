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

#include "rWindow.h"

#ifndef DEDICATED
#include "rWindowSDL.h"
#endif
#include "rWindowNull.h"

#include <memory>

// Global window instance
static std::unique_ptr<rWindow> s_window;

rWindow& rGetWindow()
{
    if (!s_window)
    {
        // Auto-initialize based on build type
#ifdef DEDICATED
        rInitWindow(true);
#else
        rInitWindow(false);
#endif
    }
    return *s_window;
}

void rInitWindow(bool forDedicatedServer)
{
    if (s_window)
    {
        s_window.reset();
    }

    if (forDedicatedServer)
    {
        s_window = std::make_unique<rWindowNull>();
    }
    else
    {
#ifndef DEDICATED
        s_window = std::make_unique<rWindowSDL>();
#else
        s_window = std::make_unique<rWindowNull>();
#endif
    }
}

void rShutdownWindow()
{
    if (s_window)
    {
        s_window->Destroy();
        s_window.reset();
    }
}

