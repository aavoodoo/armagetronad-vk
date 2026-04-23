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

#ifndef ArmageTron_uInputProviderSDL_H
#define ArmageTron_uInputProviderSDL_H

#include "uInputInterface.h"

#ifndef DEDICATED

#include "rSDL.h"

//! SDL-based input provider for graphical client builds
class uInputProviderSDL : public uInputProvider
{
public:
    uInputProviderSDL();
    ~uInputProviderSDL() override;

    bool Initialize() override;
    void Shutdown() override;

    int GetNumKeys() const override;
    tString GetKeyName(int keyIndex) const override;
    tString GetKeyPersistentID(int keyIndex) const override;

    int GetNumJoysticks() const override;
    bool GetJoystickInfo(int index, uJoystickInfo& info) const override;
    void SetJoystickEventsEnabled(bool enabled) override;

    bool SupportsGraphics() const override { return true; }

private:
    bool initialized_;
};

#endif // DEDICATED

#endif // ArmageTron_uInputProviderSDL_H
