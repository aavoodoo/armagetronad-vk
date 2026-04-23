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

#include "uInputProviderSDL.h"

#ifndef DEDICATED

#include <sstream>

// Helper to write sanitized key name
static void WriteSanitizedKeyname(std::ostream& s, char const* input)
{
    for (char const* c = input; *c; ++c)
    {
        if (isblank(*c))
        {
            s << "_";
        }
        else
        {
            s << char(toupper(*c));
        }
    }
}

uInputProviderSDL::uInputProviderSDL()
    : initialized_(false)
{
}

uInputProviderSDL::~uInputProviderSDL()
{
    Shutdown();
}

bool uInputProviderSDL::Initialize()
{
    if (initialized_)
        return true;

    // SDL initialization is handled elsewhere (rScreen, etc.)
    // We just mark ourselves as ready
    initialized_ = true;
    return true;
}

void uInputProviderSDL::Shutdown()
{
    initialized_ = false;
}

int uInputProviderSDL::GetNumKeys() const
{
    // SDL3: SDL_NUM_SCANCODES → SDL_SCANCODE_COUNT
    return SDL_SCANCODE_COUNT + 1;
}

tString uInputProviderSDL::GetKeyName(int keyIndex) const
{
    tString displayID;

    // SDL3: SDL_GetKeyFromScancode requires 3 parameters
    SDL_Scancode scancode = static_cast<SDL_Scancode>(keyIndex);
    SDL_Keycode key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    const char* name = SDL_GetKeyName(key);
    if (name && name[0])
    {
        displayID = name;
    }

    if (displayID.size() == 0)
    {
        std::ostringstream s;
        s << "UNKNOWN_" << keyIndex;
        displayID = s.str();
    }

    return displayID;
}

tString uInputProviderSDL::GetKeyPersistentID(int keyIndex) const
{
    tString persistentID;

    // SDL3: Use scancode-based naming
    SDL_Scancode scancode = static_cast<SDL_Scancode>(keyIndex);

    switch (scancode)
    {
    default:
    {
        std::ostringstream s;
        const char* name = SDL_GetScancodeName(scancode);
        if (name && name[0] >= ' ')
        {
            s << "SCANCODE_";
            WriteSanitizedKeyname(s, name);
        }
        else
        {
            s << "UNKNOWNSCANCODE_" << keyIndex;
        }
        persistentID = s.str();
    }
    break;
    // Special cases to ensure consistent naming
    case SDL_SCANCODE_RETURN2:
        persistentID = "SCANCODE_RETURN2";
        break;
    case SDL_SCANCODE_BACKSLASH:
        persistentID = "SCANCODE_BACKSLASH";
        break;
    case SDL_SCANCODE_APOSTROPHE:
        persistentID = "SCANCODE_APOSTROPHE";
        break;
    case SDL_SCANCODE_SLASH:
        persistentID = "SCANCODE_SLASH";
        break;
    case SDL_SCANCODE_LEFTBRACKET:
        persistentID = "SCANCODE_LEFTBRACKET";
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        persistentID = "SCANCODE_RIGHTBRACKET";
        break;
    case SDL_SCANCODE_KP_LEFTPAREN:
        persistentID = "SCANCODE_KP_LEFTPAREN";
        break;
    case SDL_SCANCODE_KP_RIGHTPAREN:
        persistentID = "SCANCODE_KP_RIGHTPAREN";
        break;
    case SDL_SCANCODE_KP_LEFTBRACE:
        persistentID = "SCANCODE_KP_LEFTBRACE";
        break;
    case SDL_SCANCODE_KP_RIGHTBRACE:
        persistentID = "SCANCODE_KP_RIGHTBRACE";
        break;
    }

    return persistentID;
}

int uInputProviderSDL::GetNumJoysticks() const
{
    // SDL3: SDL_NumJoysticks() → SDL_GetJoysticks(&count)
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    if (joysticks)
    {
        SDL_free(joysticks);
    }
    return count;
}

bool uInputProviderSDL::GetJoystickInfo(int index, uJoystickInfo& info) const
{
    // SDL3: Get joystick IDs first, then open by ID
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    if (!joysticks || index < 0 || index >= count)
    {
        if (joysticks) SDL_free(joysticks);
        return false;
    }

    SDL_JoystickID joystickID = joysticks[index];
    SDL_free(joysticks);

    // SDL3: SDL_JoystickOpen → SDL_OpenJoystick (takes ID, not index)
    SDL_Joystick* stick = SDL_OpenJoystick(joystickID);
    if (!stick)
        return false;

    info.id = index;

    // SDL3: SDL_JoystickName → SDL_GetJoystickName
    const char* name = SDL_GetJoystickName(stick);
    info.name = name ? name : "Unknown Joystick";

    // SDL3: All joystick query functions renamed with Get prefix
    info.numAxes = SDL_GetNumJoystickAxes(stick);
    info.numButtons = SDL_GetNumJoystickButtons(stick);
    info.numBalls = SDL_GetNumJoystickBalls(stick);
    info.numHats = SDL_GetNumJoystickHats(stick);

    // Note: We don't close the joystick here as it may be in use
    return true;
}

void uInputProviderSDL::SetJoystickEventsEnabled(bool enabled)
{
    // SDL3: SDL_JoystickEventState → SDL_SetJoystickEventsEnabled
    SDL_SetJoystickEventsEnabled(enabled);
}

#endif // DEDICATED
