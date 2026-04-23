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

#ifndef ArmageTron_uEventSDL_H
#define ArmageTron_uEventSDL_H

#include "uEvent.h"

#ifndef DEDICATED

// Forward declaration - actual SDL types come from rSDL.h
union SDL_Event;

//! SDL to uEvent mapper
//! Converts platform-specific SDL events to platform-agnostic uEvent structures.
//! This class is only available in non-dedicated (graphical client) builds.
class uEventSDL
{
public:
    //! Convert an SDL_Event to a uEvent
    //! @param sdlEvent The SDL event to convert
    //! @param outEvent The resulting platform-agnostic event
    //! @return true if the event was successfully converted, false if unsupported
    static bool ToUEvent(const SDL_Event& sdlEvent, uEvent& outEvent);

    //! Convert a uEvent back to an SDL_Event
    //! @param event The platform-agnostic event
    //! @param outSDLEvent The resulting SDL event
    //! @return true if the event was successfully converted
    //! @note This is primarily for testing/verification purposes
    static bool ToSDLEvent(const uEvent& event, SDL_Event& outSDLEvent);

    //! Convert SDL key modifiers to uKeyMod flags
    static uint16_t ConvertKeyMod(uint16_t sdlMod);

    //! Convert uKeyMod flags back to SDL modifiers
    static uint16_t ConvertKeyModToSDL(uint16_t uMod);
};

#endif // DEDICATED

#endif // ArmageTron_uEventSDL_H
