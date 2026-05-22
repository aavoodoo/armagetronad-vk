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
#include "tCoord.h"
#include <cstdint>
#include <vector>

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

    //! Hit-test against the button rect (HUD space [-1,1]).
    bool HitTest(float hx, float hy) const;
    //! Hit-test against the button + any open picker slot. Used by the
    //! dispatcher to decide when a drag has gone "out of the active
    //! interactive area" vs is still selecting a slot.
    bool IsExpandedHit(float hx, float hy) const;

    //! Press handler. Records the press start (for drag-threshold
    //! detection) and fires the rising-edge action for hold-mode buttons
    //! and tap-mode buttons without a picker. Tap-mode buttons WITH a
    //! picker defer their default action to OnRelease so we can tell
    //! tap (no drag) from slide (picker opens → slot selected).
    //! @param vpIdx  Viewport index the touch landed in.
    //! @param hx,hy  Press position in HUD space.
    void OnPress(int vpIdx, float hx, float hy);
    //! Drag handler. Returns true while the button is still the active
    //! recipient of finger events; false when this drag took the finger
    //! out of the interactive region and the dispatcher should drop the
    //! per-finger binding.
    bool OnDrag(int vpIdx, float hx, float hy);
    //! Release handler. Fires the picker's slot action (when open and
    //! hovered) or the default action (when the picker never opened),
    //! and for hold-mode buttons fires the 0.0 follow-up.
    void OnRelease(int vpIdx);

    //! Read-only access to the currently-open picker for the renderer.
    bool DropdownOpen() const { return dropdownState_ == DropdownState::Open; }

    tString  actionName_;        //!< Action name from XML (e.g. "CYCLE_TURN_LEFT")
    int      player_       = 1;  //!< 1-based player index
    uAction* action_       = nullptr; //!< Resolved action pointer (lazy)
    int64_t  activeFinger_ = -1; //!< Finger currently holding this button (-1 = none)
    bool     pressed_      = false;
    uint8_t  touchModeMask_ = 0x08; //!< Bitmask of touch modes where this button is active.
                                    //!< Default 0x08 = mode 3 only.
                                    //!< Parsed from touchMode="all" (0x0E) or a single
                                    //!< mode number ("1"→0x02, "2"→0x04, "3"→0x08).

    //! Press/release dispatch mode. Default "tap" — Activate(true) →
    //! action(1.0), Activate(false) → no-op. Suits UI and rising-edge
    //! gameplay actions (CHAT, INGAME_MENU, CYCLE_TURN_LEFT/RIGHT — the
    //! engine handler for the turns gates on x > 0.5 so the release is
    //! ignored anyway). Set actionMode="hold" for actions whose handler
    //! actually consumes the release event — today that's only
    //! CYCLE_BRAKE, which uses `braking = (x > 0)`. Ignored for
    //! uActionGlobal (always tap) and COCKPIT_KEY_* (always tap).
    bool     tapMode_ = true;

    //! Toggled visual mode. When true, the button's rendered alpha
    //! mirrors a queried game-state flag instead of the per-press
    //! pressed_ field. Useful for buttons that reflect a persistent
    //! state (spectator on/off, gyro on/off) rather than a one-shot
    //! action. Parsed from XML toggle="true".
    bool     toggle_ = false;

    //! Invert the toggled visual. When true, the rendered alpha is
    //! `!state` instead of `state`. Used so the spec button looks
    //! "active" (gaming-pad icon lit) when the player IS playing —
    //! i.e. NOT spectating. Parsed from XML invert="true".
    bool     invert_ = false;

    //! Per-button "active tint" colour for toggle-driven buttons. When
    //! toggleVisual="true" and the queried state resolves to ON, the
    //! background's SDF is modulated by this colour (multiplied into the
    //! per-vertex RGB). Default is green; cockpit can override via
    //! tintActive="r,g,b" (three 0-1 floats, comma-separated).
    float    tintR_ = 0.0f;
    float    tintG_ = 1.0f;
    float    tintB_ = 0.4f;

    //! Visibility predicate name. Empty = always visible. Recognised
    //! values today: "chatEnabled" (visible iff ENABLE_CHAT is true),
    //! "gyroAvailable" (visible iff the host exposes a gyroscope
    //! sensor). Parsed from XML visibleIf="…". Hidden buttons skip
    //! Render and don't participate in OnPress hit-tests.
    tString  visibleIf_;

    //! Picker name. Empty when the button has no dropdown. Today the
    //! only recognised values are "instant_chat" (8 slots from the
    //! per-player INSTANT_CHAT_STRING_<p>_<i> cfg, fires INSTANT_CHAT_<i>
    //! as a uActionPlayer) and "touch_mode" (3 slots that flip the
    //! global ENABLE_TOUCH via su_SetEnableTouch). Parsed from XML
    //! picker="instant_chat" / picker="touch_mode".
    tString  pickerName_;

    //! Picker state. Closed by default; flipped to Open when OnDrag
    //! detects the finger has moved beyond the open threshold from the
    //! press start.
    enum class DropdownState : uint8_t { Closed, Open };
    DropdownState dropdownState_ = DropdownState::Closed;

    //! Press start in HUD space — used to measure drag distance.
    float    pressStartHx_ = 0;
    float    pressStartHy_ = 0;
    //! Currently-hovered slot index (or -1) while the picker is open.
    int      hoveredSlot_  = -1;

    //! Per-slot definition built when the picker opens. action is a
    //! uAction name to dispatch on slot-release (empty for the
    //! touch_mode picker which uses a built-in su_SetEnableTouch call).
    struct SlotSpec {
        tString action;
        tString label;
        bool    selected;  // visual mark (e.g. current touch mode)
    };
    std::vector<SlotSpec> slots_;
    //! Slot rect centers in HUD space, parallel to slots_.
    std::vector<tCoord>   slotCenters_;
    //! Per-slot half-extent in HUD space (same for every slot).
    tCoord                slotHalfExt_ {0, 0};

    //! Check if this button is active in the current touch mode
    bool IsActiveInCurrentMode() const;

    //! Evaluate the visibleIf predicate. Buttons whose predicate fails
    //! are skipped by Render and OnPress (no draw, no hit-test). The
    //! check runs every Render — cheap state reads, so we don't bother
    //! caching.
    bool IsVisible() const;

private:
    void ResolveAction();
    void DispatchBoundAction(bool on, int vpIdx);
    void BuildSlots(int vpIdx);
    void OpenPicker(int vpIdx);
    void ClosePicker();
    bool SlotHitTest(float hx, float hy, int& outIdx) const;
    void FireSlot(int idx, int vpIdx);
};

} // namespace cWidget

#endif // !DEDICATED
#endif // ARMAGETRON_COCKPIT_TOUCH_BUTTON_H
