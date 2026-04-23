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

#ifndef ArmageTron_uInputProviderNull_H
#define ArmageTron_uInputProviderNull_H

#include "uInputInterface.h"
#include <sstream>

//! Null input provider for dedicated server builds
//! Provides stub implementations that do nothing but allow code to compile.
class uInputProviderNull : public uInputProvider
{
public:
    uInputProviderNull() = default;
    ~uInputProviderNull() override = default;

    bool Initialize() override { return true; }
    void Shutdown() override {}

    int GetNumKeys() const override
    {
        // Return a reasonable number for config compatibility
        return 512;
    }

    tString GetKeyName(int keyIndex) const override
    {
        std::ostringstream s;
        s << "KEY_" << keyIndex;
        return tString(s.str());
    }

    tString GetKeyPersistentID(int keyIndex) const override
    {
        std::ostringstream s;
        s << "KEY_" << keyIndex;
        return tString(s.str());
    }

    int GetNumJoysticks() const override { return 0; }

    bool GetJoystickInfo(int, uJoystickInfo&) const override { return false; }

    void SetJoystickEventsEnabled(bool) override {}

    bool SupportsGraphics() const override { return false; }
};

#endif // ArmageTron_uInputProviderNull_H
