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

#ifndef ARMAGETRON_COCKPIT_TOUCH_BUTTON_H
#define ARMAGETRON_COCKPIT_TOUCH_BUTTON_H

#include "cockpit/cWidgetBase.h"

#ifndef DEDICATED

#include "tString.h"
#include <cstdint>

class uAction;

namespace cWidget {

//! On-screen touch button widget for ENABLE_TOUCH=3 mode.
//! Renders a semi-transparent quad with an action label.
//! When tapped, activates the bound player action.
//!
//! XML example:
//!   <TouchButton action="CYCLE_TURN_LEFT" player="1" camera="in">
//!     <Position x="-0.90" y="-0.75"/>
//!     <Size width="0.20" height="0.20"/>
//!     <Caption>◀</Caption>
//!   </TouchButton>
class TouchButton : public WithCoordinates, public WithCaption, public WithBackground {
public:
    TouchButton();
    ~TouchButton() override = default;

    void Render() override;
    bool Process(tXmlParser::node cur) override;

    //! Hit-test in [-1,1] HUD space. Convert from touch [0,1] with: hx=x*2-1, hy=1-y*2
    bool HitTest(float hx, float hy) const;

    //! Activate or deactivate the bound action
    void Activate(bool on);

    tString  actionName_;        //!< Action name from XML (e.g. "CYCLE_TURN_LEFT")
    int      player_       = 1;  //!< 1-based player index
    uAction* action_       = nullptr; //!< Resolved action pointer (lazy)
    int64_t  activeFinger_ = -1; //!< Finger currently holding this button (-1 = none)
    bool     pressed_      = false;
    uint8_t  touchModeMask_ = 0x08; //!< Bitmask of touch modes where this button is active.
                                    //!< Default 0x08 = mode 3 only. Bit 1=mode1, 2=mode2, 4=mode3.
                                    //!< Parsed from touchMode="1,3" or touchMode="all".

    //! Check if this button is active in the current touch mode
    bool IsActiveInCurrentMode() const;

private:
    void ResolveAction();
};

} // namespace cWidget

#endif // !DEDICATED
#endif // ARMAGETRON_COCKPIT_TOUCH_BUTTON_H
