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

#include "uInputInterface.h"

#ifndef DEDICATED
#include "uInputProviderSDL.h"
#endif
#include "uInputProviderNull.h"

#include <memory>
#include <stdexcept>

// Global input provider instance
static std::unique_ptr<uInputProvider> s_inputProvider;

uInputProvider& uGetInputProvider()
{
    if (!s_inputProvider)
    {
        // Auto-initialize based on build type
#ifdef DEDICATED
        uInitInputProvider(true);
#else
        uInitInputProvider(false);
#endif
    }
    return *s_inputProvider;
}

void uInitInputProvider(bool forDedicatedServer)
{
    if (s_inputProvider)
    {
        s_inputProvider->Shutdown();
        s_inputProvider.reset();
    }

    if (forDedicatedServer)
    {
        s_inputProvider = std::make_unique<uInputProviderNull>();
    }
    else
    {
#ifndef DEDICATED
        s_inputProvider = std::make_unique<uInputProviderSDL>();
#else
        s_inputProvider = std::make_unique<uInputProviderNull>();
#endif
    }

    s_inputProvider->Initialize();
}

void uShutdownInputProvider()
{
    if (s_inputProvider)
    {
        s_inputProvider->Shutdown();
        s_inputProvider.reset();
    }
}
