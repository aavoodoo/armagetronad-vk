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

#include "cockpit/cTouchButton.h"

#ifndef DEDICATED

#include "rRenderQueue.h"
#include "rVertex.h"
#include "rFont.h"
#include "uInput.h"
#include "cockpit/cCockpit.h"
#include "ePlayer.h"
#include "rViewport.h"   // MAX_VIEWPORTS, sr_viewportBelongsToPlayer
#include "rScreen.h"     // sr_screenWidth, sr_screenHeight
#include "tConfiguration.h"

#include <algorithm>
#include <sstream>
#include <cstdio>

// Defined in uInput.cpp — flips the global ENABLE_TOUCH from C++.
// We can't include uInput.h here without pulling a lot of UI machinery;
// the symbol has C linkage (per uInput.cpp:62) so this forward is enough.
extern "C" void su_SetEnableTouch(int mode);

// State queries used by Render and IsVisible. Forward-declared so the
// translation unit doesn't have to pull uInput.h / ePlayer engine internals.
extern bool su_IsGyroActive();    // uInput.cpp
extern bool su_HasGyroSensor();   // uInput.cpp
extern bool se_IsChatEnabled();   // ePlayer.cpp

namespace cWidget {

TouchButton::TouchButton()
    : WithCaption()
{
}

bool TouchButton::Process(tXmlParser::node cur)
{
    // Handle root <TouchButton action="" player="" touchMode="..."> attributes.
    if (cur.IsOfType("TouchButton")) {
        actionName_ = cur.GetProp("action");
        cur.GetProp("player", player_);

        // touchMode is either "all" (active in every ENABLE_TOUCH mode 1..3)
        // or a single mode number ("1", "2", or "3"). Anything else falls
        // back to mode 3 (the default cockpit-buttons mode). Multi-mode
        // lists are not supported — pick "all" or one specific mode.
        if (cur.HasProp("touchMode")) {
            tString mode = cur.GetProp("touchMode");
            if      (mode == "all") touchModeMask_ = 0x0E; // bits 1|2|3
            else if (mode == "1")   touchModeMask_ = 0x02;
            else if (mode == "2")   touchModeMask_ = 0x04;
            else if (mode == "3")   touchModeMask_ = 0x08;
            else                    touchModeMask_ = 0x08; // unknown → mode 3
        }

        // actionMode controls whether the release transition dispatches a
        // 0.0 follow-up. Default "tap" suits the majority of touch
        // actions (CHAT, INGAME_MENU, the turn actions whose engine
        // handler ignores the release event). Only buttons whose handler
        // genuinely consumes x=0.0 (today: CYCLE_BRAKE) need
        // actionMode="hold".
        if (cur.HasProp("actionMode")) {
            tString m = cur.GetProp("actionMode");
            tapMode_ = (m != "hold");
        }

        // Toggled visual: render alpha follows a queried game-state flag
        // (see Render). Optionally inverted so "on" maps to !state — used
        // for the spec button, which should look active (gaming-pad icon
        // lit) when the player is NOT spectating.
        if (cur.HasProp("toggleVisual")) {
            tString t = cur.GetProp("toggleVisual");
            toggle_ = (t == "true" || t == "1");
        }
        if (cur.HasProp("invert")) {
            tString t = cur.GetProp("invert");
            invert_ = (t == "true" || t == "1");
        }

        // Picker name (hardcoded: "instant_chat" or "touch_mode" today).
        // When set, this button shows a dropdown slot list on drag-from-
        // button-beyond-threshold. See OnDrag / OpenPicker / BuildSlots.
        if (cur.HasProp("picker")) pickerName_ = cur.GetProp("picker");

        // tintActive="r,g,b" — colour applied to the background SDF when
        // the toggle-driven state is ON. Three 0-1 floats, comma-
        // separated. Member defaults (r=0, g=1, b=0.4 → green) apply if
        // the attribute is absent or unparseable.
        if (cur.HasProp("tintActive")) {
            tString s = cur.GetProp("tintActive");
            float r = tintR_, g = tintG_, b = tintB_;
            if (std::sscanf(static_cast<const char*>(s), "%f,%f,%f", &r, &g, &b) == 3) {
                tintR_ = r; tintG_ = g; tintB_ = b;
            }
        }

        // Visibility predicate. Hidden buttons skip Render and OnPress
        // entirely. Recognised values resolved in IsVisible().
        if (cur.HasProp("visibleIf")) visibleIf_ = cur.GetProp("visibleIf");
        return true;
    }
    if (WithCoordinates::Process(cur) || WithCaption::Process(cur) || WithBackground::Process(cur))
        return true;
    DisplayError(cur);
    return false;
}

bool TouchButton::IsActiveInCurrentMode() const
{
    int mode = su_GetEnableTouch();
    if (mode < 1 || mode > 3) return false;
    return (touchModeMask_ & (1 << mode)) != 0;
}

bool TouchButton::IsVisible() const
{
    // Explicit visibleIf= attribute takes precedence over any
    // auto-hide heuristic.
    if (visibleIf_.Len() > 1) {
        if (visibleIf_ == "chatEnabled")   return se_IsChatEnabled();
        if (visibleIf_ == "gyroAvailable") {
            // Hide in split-screen: a single device's gyro can't be
            // shared across multiple players sitting around the same
            // screen. The gyro listener would react to ONE physical
            // tilt and feed it to whichever viewport is bound to gyro
            // input — nondeterministic from the players' POV.
            rViewportConfiguration* vp = rViewportConfiguration::CurrentViewportConfiguration();
            if (vp && vp->num_viewports > 1) return false;
            return su_HasGyroSensor();
        }
        if (visibleIf_ == "always")        return true;
        // Unknown predicate — fail-safe: stay visible so the cockpit
        // author doesn't lose a button to a typo. (Misspellings show up
        // at design time when the button doesn't disappear when expected.)
        return true;
    }

    // Auto-hide for COCKPIT_KEY_<N>: if no widget in this button's cockpit
    // has registered an event handler for this key, pressing the button
    // would be a no-op. Hide it instead of leaving a "dead" button.
    // Cockpits opt out by setting an explicit visibleIf="always" (or any
    // other predicate that returns true).
    if (actionName_.substr(0, 12) == "COCKPIT_KEY_" && actionName_.size() == 13) {
        int keyNum = actionName_[12] - '0';
        if (keyNum >= 1 && keyNum <= 5 && m_Cockpit) {
            return m_Cockpit->m_EventHandlers.count(keyNum) > 0;
        }
    }

    return true;
}

void TouchButton::ResolveAction()
{
    if (!action_ && !actionName_.empty())
        action_ = uAction::Find(actionName_.c_str());
}

bool TouchButton::HitTest(float hx, float hy) const
{
    return hx >= m_position.x - m_size.x && hx <= m_position.x + m_size.x &&
           hy >= m_position.y - m_size.y && hy <= m_position.y + m_size.y;
}

// Dispatch the button's bound `actionName_` with the given value, using the
// same type-based routing the gesture handlers expect. COCKPIT_KEY_* and
// SCORE have special paths; everything else goes through uAction. The `on`
// flag matters only for hold-mode player actions (turn/brake) — globals
// and the special branches ignore the release transition by design.
void TouchButton::DispatchBoundAction(bool on, int vpIdx)
{
    // COCKPIT_KEY_* → cockpit-internal widget event. Fires only on press.
    if (actionName_.substr(0, 12) == "COCKPIT_KEY_" && actionName_.size() == 13)
    {
        int keyNum = actionName_[12] - '0';
        if (keyNum >= 1 && keyNum <= 5 && on)
        {
            FOREACH_COCKPIT(j) { (*j)->HandleEvent(keyNum, true); }
        }
        return;
    }

    // SCORE → per-viewport HUD slot toggle (mirrors legacy gTouchOverlay).
    if (on && vpIdx >= 0 && actionName_ == "SCORE")
    {
        ePlayerNetID::SetShowScoresViewport(
            vpIdx, !ePlayerNetID::GetShowScoresViewport(vpIdx));
        return;
    }

    ResolveAction();
    if (!action_) return;

    // Globals (INGAME_MENU, CONSOLE_INPUT, SCORE-without-vpIdx fallback, …)
    // dispatch via GlobalAct. Release is always a no-op for these.
    if (dynamic_cast<uActionGlobal*>(action_))
    {
        if (on) uActionGlobalFunc::GlobalAct(action_, 1.0f);
        return;
    }

    // Player/camera action. Hold-style (CYCLE_BRAKE): press fires 1.0,
    // release fires 0.0. Tap-style: press fires 1.0, release no-op.
    uPlayerPrototype* pc = uPlayerPrototype::PlayerConfig(player_ - 1);
    if (!pc) return;
    if (on) pc->Act(action_, 1.0f);
    else if (!tapMode_) pc->Act(action_, 0.0f);
}

// Build the picker's slot list. Hardcoded by `pickerName_`. Reads live
// state (current touch mode, per-player instant-chat strings) at open
// time so the labels and selection are always current.
void TouchButton::BuildSlots(int vpIdx)
{
    slots_.clear();

    if (pickerName_ == "touch_mode")
    {
        const int current = su_GetEnableTouch();
        // Slot 0..2 map to ENABLE_TOUCH = 1..3. Action field is empty —
        // touch_mode dispatches via su_SetEnableTouch directly in
        // FireSlot, not through a uAction.
        slots_.push_back({tString(""), tString("1: zones"),    current == 1});
        slots_.push_back({tString(""), tString("2: gestures"), current == 2});
        slots_.push_back({tString(""), tString("3: buttons"),  current == 3});
        return;
    }

    if (pickerName_ == "instant_chat")
    {
        // Resolve which player this picker belongs to. In split-screen
        // it's the viewport's player; outside that context fall back to
        // the XML-declared player_.
        int playerID = player_ - 1;
        if (vpIdx >= 0 && vpIdx < MAX_VIEWPORTS) {
            int pid = sr_viewportBelongsToPlayer[vpIdx];
            if (pid >= 0) playerID = pid;
        }

        for (int i = 0; i < 8; i++)
        {
            tString cfgKey;
            cfgKey << "INSTANT_CHAT_STRING_" << (playerID + 1) << "_" << (i + 1);
            tString label;
            if (tConfItemBase* item = tConfItemBase::FindConfigItem(cfgKey))
            {
                std::ostringstream val;
                item->WriteVal(val);
                label = val.str().c_str();
            }
            if (label.Len() <= 1) {
                label = "";
                label << "slot " << (i + 1);
            }
            tString actName;
            actName << "INSTANT_CHAT_" << (i + 1);
            slots_.push_back({actName, label, false});
        }
        return;
    }
}

// Compute slot rect centers in HUD space. Slots stack VERTICALLY directly
// below the originating button, flush with the same screen edge the
// button sits against (left-anchored button → slot column hugs the left
// border; right-anchored → hugs the right). Slots overlap the rest of
// the side column (score / gyro / camera / spec) for the duration the
// picker is open — that's intentional, the picker is modal-by-touch and
// returns to the normal layout the moment the finger releases.
void TouchButton::OpenPicker(int vpIdx)
{
    BuildSlots(vpIdx);
    if (slots_.empty()) return;

    const float gap = 0.005f;
    // Larger touch targets than the originating button. Wide enough for
    // multi-word labels ("1: zones", "INSTANT_CHAT_STRING_…"); tall
    // enough to be hit comfortably with a fingertip in a quick drag.
    const float slotHW = m_size.x * 5.2f;   // ~5.2× button half-width
    const float slotHH = m_size.y * 1.3f;   // ~1.3× button half-height
    slotHalfExt_ = tCoord(slotHW, slotHH);

    // Column flush with the screen edge the button is anchored to.
    // m_position.x < 0 ⇒ left-anchored; slot LEFT edge at NDC x=-1.
    // Otherwise right-anchored; slot RIGHT edge at NDC x=+1.
    const bool atLeft = m_position.x < 0;
    const float slotCx = atLeft
        ?  (-1.0f + slotHW)   // left edge flush with screen left
        :  ( 1.0f - slotHW);  // right edge flush with screen right

    // Vertical stack: slot[0] immediately below the button, descending.
    // HUD y grows UP so each subsequent slot has a smaller y.
    const float step           = 2.0f * (slotHH + gap);
    const float firstSlotCy    = m_position.y - m_size.y - gap - slotHH;
    slotCenters_.clear();
    for (size_t i = 0; i < slots_.size(); i++)
    {
        const float cy = firstSlotCy - static_cast<float>(i) * step;
        slotCenters_.push_back(tCoord(slotCx, cy));
    }
    dropdownState_ = DropdownState::Open;
    hoveredSlot_   = -1;
}

void TouchButton::ClosePicker()
{
    dropdownState_ = DropdownState::Closed;
    slots_.clear();
    slotCenters_.clear();
    slotHalfExt_ = tCoord(0, 0);
    hoveredSlot_ = -1;
}

bool TouchButton::SlotHitTest(float hx, float hy, int& outIdx) const
{
    outIdx = -1;
    for (size_t i = 0; i < slotCenters_.size(); i++)
    {
        const tCoord & c = slotCenters_[i];
        if (hx >= c.x - slotHalfExt_.x && hx <= c.x + slotHalfExt_.x &&
            hy >= c.y - slotHalfExt_.y && hy <= c.y + slotHalfExt_.y)
        {
            outIdx = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool TouchButton::IsExpandedHit(float hx, float hy) const
{
    if (HitTest(hx, hy)) return true;
    int dummy;
    return SlotHitTest(hx, hy, dummy);
}

void TouchButton::FireSlot(int idx, int vpIdx)
{
    if (idx < 0 || idx >= static_cast<int>(slots_.size())) return;
    SlotSpec const & slot = slots_[idx];

    if (pickerName_ == "touch_mode")
    {
        // Slot 0..2 → ENABLE_TOUCH 1..3. Use the C bridge declared at
        // the top of this file.
        su_SetEnableTouch(idx + 1);
        return;
    }

    if (slot.action.Len() <= 1) return;

    // Slot action — same dispatch surface as the bound action.
    uAction * act = uAction::Find(slot.action.c_str());
    if (!act) return;
    if (dynamic_cast<uActionGlobal*>(act))
    {
        uActionGlobalFunc::GlobalAct(act, 1.0f);
        return;
    }
    // Resolve player from the viewport when we have one, else from XML.
    int playerN = player_;
    if (vpIdx >= 0 && vpIdx < MAX_VIEWPORTS) {
        int pid = sr_viewportBelongsToPlayer[vpIdx];
        if (pid >= 0) playerN = pid + 1;
    }
    if (uPlayerPrototype* pc = uPlayerPrototype::PlayerConfig(playerN - 1))
        pc->Act(act, 1.0f);
}

void TouchButton::OnPress(int vpIdx, float hx, float hy)
{
    pressed_       = true;
    pressStartHx_  = hx;
    pressStartHy_  = hy;
    dropdownState_ = DropdownState::Closed;
    hoveredSlot_   = -1;

    // Fire rising-edge action immediately for hold-mode buttons (engine
    // consumes the press transition) and for plain tap-mode buttons
    // without a picker (matches the legacy "fire on press" UX).
    // Tap-mode buttons WITH a picker defer their default action to
    // OnRelease so we can tell a plain tap apart from a drag that
    // opened the picker.
    if (!tapMode_ || pickerName_.Len() <= 1)
        DispatchBoundAction(true, vpIdx);
}

bool TouchButton::OnDrag(int vpIdx, float hx, float hy)
{
    if (!pressed_) return false;

    // Buttons without a picker: existing drag-out cancel. Hold-mode
    // also fires the release-0.0 follow-up so the action (brake) ends.
    if (pickerName_.Len() <= 1)
    {
        if (!HitTest(hx, hy))
        {
            if (!tapMode_) DispatchBoundAction(false, vpIdx);
            pressed_ = false;
            return false;
        }
        return true;
    }

    // Picker buttons. Open on drag-beyond-threshold.
    if (dropdownState_ == DropdownState::Closed)
    {
        const float dx = hx - pressStartHx_;
        const float dy = hy - pressStartHy_;
        // Threshold: half the button's longest half-extent — small enough
        // that a deliberate slide opens immediately, large enough that
        // hand jitter on a tap doesn't.
        const float thresh = 0.5f * std::max(m_size.x, m_size.y);
        if (dx * dx + dy * dy > thresh * thresh)
            OpenPicker(vpIdx);
    }

    // Once open, track the slot under the finger.
    if (dropdownState_ == DropdownState::Open)
        SlotHitTest(hx, hy, hoveredSlot_);

    return true;
}

void TouchButton::OnRelease(int vpIdx)
{
    if (!pressed_) return;

    if (dropdownState_ == DropdownState::Open)
    {
        // Release inside a slot → fire that slot. Release anywhere else
        // (button itself, or empty space) → cancel: no default action,
        // no slot action.
        if (hoveredSlot_ >= 0) FireSlot(hoveredSlot_, vpIdx);
        ClosePicker();
        pressed_ = false;
        return;
    }

    // No picker opened. Fire the deferred default for tap-mode picker
    // buttons (drag never crossed threshold), or fire the release-0.0
    // follow-up for hold-mode buttons. Tap-mode non-picker buttons
    // already fired on press — nothing to do here.
    if (pickerName_.Len() > 1)
        DispatchBoundAction(true, vpIdx);
    else if (!tapMode_)
        DispatchBoundAction(false, vpIdx);

    pressed_ = false;
}

void TouchButton::Render()
{
    if (!IsActiveInCurrentMode()) return;
    if (!IsVisible()) return;

    const float x0 = m_position.x - m_size.x;
    const float x1 = m_position.x + m_size.x;
    const float y0 = m_position.y - m_size.y;
    const float y1 = m_position.y + m_size.y;

    // Visual state. Default: tracks the pressed_ flag. When toggle_ is
    // set in XML, the visual mirrors a queried game-state flag instead.
    // State sources by action name:
    //   TOGGLE_SPECTATOR → ePlayer::spectate (the INTENT — flipped by
    //     the press transition, not the round-synced IsSpectating).
    //   SCORE            → ePlayerNetID::GetShowScoresViewport(myVp).
    //   COCKPIT_KEY_3    → su_IsGyroActive() (our default gyro slot).
    // invert_ flips the resolved state (so the spec button looks lit
    // when the player is NOT spectating).
    bool visualOn = pressed_;
    if (toggle_) {
        bool state = false;
        if (actionName_ == "TOGGLE_SPECTATOR") {
            // Use ePlayer::spectate, not netPlayer->IsSpectating(): the
            // intent updates immediately on press; IsSpectating only
            // syncs between rounds (ePlayer.cpp:4694-4701).
            if (ePlayer* p = ePlayer::PlayerConfig(player_ - 1)) {
                state = p->spectate;
            }
        }
        else if (actionName_ == "SCORE") {
            // Derive our viewport idx from the cockpit's bound player.
            int vpIdx = 0;
            if (m_Cockpit) {
                ePlayer* p = m_Cockpit->GetPlayer();
                if (p) {
                    for (int i = 0; i < MAX_VIEWPORTS; i++) {
                        if (ePlayer::PlayerConfig(sr_viewportBelongsToPlayer[i]) == p) {
                            vpIdx = i; break;
                        }
                    }
                }
            }
            state = ePlayerNetID::GetShowScoresViewport(vpIdx);
        }
        else if (actionName_ == "COCKPIT_KEY_3") {
            // Default-cockpit gyro slot.
            state = su_IsGyroActive();
        }
        if (invert_) state = !state;
        visualOn = state;
    }
    const uint8_t alpha = visualOn ? 200 : 128;

    // Active tint — only applied to toggle-driven buttons in the ON state.
    // Modulates the SDF's per-vertex RGB toward the configured colour
    // (default green). Plain non-toggle buttons render at full white.
    const bool   applyTint = toggle_ && visualOn;
    const uint8_t tintR8 = applyTint ? static_cast<uint8_t>(std::min(1.0f, std::max(0.0f, tintR_)) * 255.0f) : 255;
    const uint8_t tintG8 = applyTint ? static_cast<uint8_t>(std::min(1.0f, std::max(0.0f, tintG_)) * 255.0f) : 255;
    const uint8_t tintB8 = applyTint ? static_cast<uint8_t>(std::min(1.0f, std::max(0.0f, tintB_)) * 255.0f) : 255;

    if (m_background.HasContent())
    {
        // Use Background gradient/texture from XML (supports SDF via Graphic sdf= attribute)
        m_background.SetGradientEdges(tCoord(x0, y0), tCoord(x1, y1));
        auto verts = m_background.GenerateRectVertices(tCoord(x0, y0), tCoord(x1, y1));
        // Apply press alpha + tint modulation. Tint is multiplicative
        // against the (typically white) per-vertex RGB the background
        // writes for SDF rendering. Result: when ON, the icon takes the
        // configured tint (default green); when OFF, original colour.
        for (auto& v : verts)
        {
            uint8_t va = v.color[3];
            v.color[3] = static_cast<uint8_t>(va * alpha / 255);
            if (applyTint) {
                v.color[0] = static_cast<uint8_t>(v.color[0] * tintR8 / 255);
                v.color[1] = static_cast<uint8_t>(v.color[1] * tintG8 / 255);
                v.color[2] = static_cast<uint8_t>(v.color[2] * tintB8 / 255);
            }
        }
        rRenderStateKey state = m_background.GetRenderStateKey(rBlendMode::Alpha);
        rRenderQueue::Instance().Submit(rRenderPhase::HUD, state, verts.data(), verts.size());
    }
    else
    {
        // Default: semi-transparent blue quad
        const uint8_t r = 80, g = 100, b = 220;
        rVertex20 v0(x0, y1, 0, r, g, b, alpha, 0, 0);
        rVertex20 v1(x1, y1, 0, r, g, b, alpha, 0, 0);
        rVertex20 v2(x1, y0, 0, r, g, b, alpha, 0, 0);
        rVertex20 v3(x0, y0, 0, r, g, b, alpha, 0, 0);
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);
    }

    // Render caption text centered in button
    if (!m_caption.empty()) {
        const float textH = m_size.y * 0.7f;
        const float textW = rTextField::GetTextLength(m_caption, textH, true);
        rTextField tf(m_position.x - textW * 0.5f, m_position.y + textH * 0.5f,
                      textH, sr_fontCockpit);
        tf << m_caption;
    }

    // Picker slots — only drawn while the dropdown is open. Each slot is a
    // dark semi-transparent rect with the label centered; the hovered
    // slot brightens, the cfg-selected slot (current touch mode) gets a
    // distinct tint so it's recognisable at a glance.
    if (dropdownState_ != DropdownState::Open) return;
    for (size_t i = 0; i < slots_.size(); i++)
    {
        const tCoord & c   = slotCenters_[i];
        const float    sx0 = c.x - slotHalfExt_.x;
        const float    sx1 = c.x + slotHalfExt_.x;
        const float    sy0 = c.y - slotHalfExt_.y;
        const float    sy1 = c.y + slotHalfExt_.y;
        const bool     hot = (static_cast<int>(i) == hoveredSlot_);
        const bool     sel = slots_[i].selected;
        // Background colour: cooler for plain, warmer-on-hover, brighter
        // also for the currently-selected entry so it stands out.
        uint8_t r, g, b, a;
        if (hot)      { r = 240; g = 200; b =  80; a = 220; }
        else if (sel) { r =  80; g = 160; b = 240; a = 200; }
        else          { r =  30; g =  35; b =  45; a = 200; }
        rVertex20 v0(sx0, sy1, 0, r, g, b, a, 0, 0);
        rVertex20 v1(sx1, sy1, 0, r, g, b, a, 0, 0);
        rVertex20 v2(sx1, sy0, 0, r, g, b, a, 0, 0);
        rVertex20 v3(sx0, sy0, 0, r, g, b, a, 0, 0);
        rRenderStateKey state = rRenderStateKey::HUD(0, rBlendMode::Alpha);
        rRenderQueue::Instance().SubmitQuad(rRenderPhase::HUD, state, v0, v1, v2, v3);

        // Slot label.
        if (slots_[i].label.Len() > 1) {
            const float textH = slotHalfExt_.y * 0.9f;
            const float textW = rTextField::GetTextLength(slots_[i].label, textH, true);
            rTextField tf(c.x - textW * 0.5f, c.y + textH * 0.5f,
                          textH, sr_fontCockpit);
            tf << slots_[i].label;
        }
    }
}

} // namespace cWidget

#endif // !DEDICATED
