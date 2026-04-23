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

#ifndef ArmageTron_uInputInterface_H
#define ArmageTron_uInputInterface_H

#include "tString.h"
#include <memory>
#include <vector>

//! Forward declaration
class uInput;

//! Information about a joystick device
struct uJoystickInfo
{
    int id;              //!< Device index
    tString name;        //!< Human-readable name
    int numAxes;         //!< Number of axes
    int numButtons;      //!< Number of buttons
    int numBalls;        //!< Number of trackballs
    int numHats;         //!< Number of hats (d-pads)
};

//! Abstract interface for input providers
//! Platform-specific implementations (SDL, null) implement this interface.
class uInputProvider
{
public:
    virtual ~uInputProvider() = default;

    //! Initialize the input system
    //! @return true on success
    virtual bool Initialize() = 0;

    //! Shutdown the input system
    virtual void Shutdown() = 0;

    //! Get the number of available key codes
    virtual int GetNumKeys() const = 0;

    //! Get the display name for a key by its index
    //! @param keyIndex The key index (scancode for SDL2)
    //! @return Human-readable key name
    virtual tString GetKeyName(int keyIndex) const = 0;

    //! Get the persistent ID for a key (used for config storage)
    //! @param keyIndex The key index
    //! @return Persistent string identifier
    virtual tString GetKeyPersistentID(int keyIndex) const = 0;

    //! Get number of available joysticks
    virtual int GetNumJoysticks() const = 0;

    //! Get information about a joystick
    //! @param index Joystick index (0 to GetNumJoysticks()-1)
    //! @param info Output structure filled with joystick info
    //! @return true if successful
    virtual bool GetJoystickInfo(int index, uJoystickInfo& info) const = 0;

    //! Enable/disable joystick event processing
    virtual void SetJoystickEventsEnabled(bool enabled) = 0;

    //! Check if the provider supports graphics (false for dedicated server)
    virtual bool SupportsGraphics() const = 0;
};

//! Get the current input provider instance
//! @return Reference to the active input provider
uInputProvider& uGetInputProvider();

//! Initialize the input provider
//! @param forDedicatedServer If true, creates null provider for dedicated server
void uInitInputProvider(bool forDedicatedServer = false);

//! Shutdown the input provider
void uShutdownInputProvider();

#endif // ArmageTron_uInputInterface_H
