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
#include "tPlatform.h"

// Include both implementations - factory will choose at runtime
#include "tPlatformSDL.h"
#include "tPlatformNull.h"

#include <memory>
#include <stdexcept>

// Global platform instance
static std::unique_ptr<tPlatform> s_platform;
static bool s_platformInitialized = false;

tPlatform& tGetPlatform()
{
    if (!s_platformInitialized || !s_platform)
    {
        // Auto-initialize if not done explicitly
        // Use compile-time detection: DEDICATED macro determines platform type
#ifdef DEDICATED
        tInitPlatform(true);
#else
        tInitPlatform(false);
#endif
    }
    return *s_platform;
}

void tInitPlatform(bool forDedicatedServer)
{
    if (s_platformInitialized)
    {
        return; // Already initialized
    }

#ifdef DEDICATED
    // Dedicated server always uses null platform
    (void)forDedicatedServer; // Suppress unused warning
    s_platform = std::make_unique<tPlatformNull>();
#else
    // Client build: use SDL platform for graphics, null for dedicated mode
    if (forDedicatedServer)
    {
        s_platform = std::make_unique<tPlatformNull>();
    }
    else
    {
        s_platform = std::make_unique<tPlatformSDL>();
    }
#endif

    s_platformInitialized = true;
}

void tShutdownPlatform()
{
    s_platform.reset();
    s_platformInitialized = false;
}
