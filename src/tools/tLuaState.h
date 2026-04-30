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

#ifndef TLUASTATE_H
#define TLUASTATE_H

#ifndef DEDICATED

#include "luapp.hpp"
#include <string>

//! Singleton Lua interpreter shared across the entire client.
//!
//! Owns a single luapp::state. All access must occur on the render thread.
//! The state is initialized on first call to Instance() and closed on
//! explicit Shutdown() (called from the renderer's Destroy path).
//!
//! Both effect scripts (post-process .lua) and game scripts (initialize.lua,
//! event hooks, /eval) share this single state and global namespace.
//!
//! Error policy: all script-side errors are caught via pcall and
//! forwarded to tOutput/std::cerr. Script errors never propagate as
//! C++ exceptions.
class tLuaState
{
public:
    //! Get the singleton. Creates the state on first call.
    static tLuaState& Instance();

    //! Get the underlying luapp state_view for direct API use.
    luapp::state_view View() { return luapp::state_view{state_.raw()}; }

    //! Destroy the Lua state. Safe to call multiple times.
    //! Called from the renderer's shutdown path.
    void Shutdown();

    //! Execute a Lua string. Returns true on success.
    //! On error, logs the message and returns false. Never throws.
    //! chunkname controls the source name in error messages; the '=' prefix
    //! tells Lua to use the rest verbatim (e.g. "=(eval)" → "(eval):1: ...").
    bool DoString(const char* source, const char* chunkname = "=(eval)");

    //! Execute a Lua file. Returns true on success.
    //! On error, logs the message and returns false. Never throws.
    bool DoFile(const char* path);

    //! Returns true if the state is live (not shut down).
    bool IsAlive() const { return alive_; }

    //! Called by the eLadderLogWriter hook for each game event.
    //! name = event name (e.g. "DEATH_FRAG"), args = space-separated args string.
    //! Dispatches to the Lua function on_<lowercase_name>(args) if defined.
    //! Safe to use as a plain function pointer (no captures).
    static void DispatchEvent(const char* name, const char* args);

    // Non-copyable, non-movable.
    tLuaState(const tLuaState&) = delete;
    tLuaState& operator=(const tLuaState&) = delete;

private:
    tLuaState();
    ~tLuaState() = default;

    luapp::state state_;
    bool alive_ = false;
};

#endif // DEDICATED
#endif // TLUASTATE_H
