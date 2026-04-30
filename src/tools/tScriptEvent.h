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

#ifndef TSCRIPTEVENT_H
#define TSCRIPTEVENT_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

//! Lightweight named event bus for game → scripting decoupling.
//!
//! Game code fires named events with optional string arguments. Scripting
//! layers (Lua, future Python, etc.) subscribe once and dispatch to their
//! own handlers. Neither side knows about the other's types.
//!
//! Thread safety: not thread-safe. All Fire/Subscribe/Unsubscribe calls
//! must occur on the main game thread.
//!
//! Usage — game code (no render/Lua dependency):
//!   #include "tScriptEvent.h"
//!   tScriptEvent::Fire("death_frag", {"killer_name", "victim_name"});
//!
//! Usage — scripting layer (subscribes once at init):
//!   tScriptEvent::Subscribe([](std::string_view name, const tScriptEvent::Args& args) {
//!       // dispatch to Lua, Python, etc.
//!   });
class tScriptEvent
{
public:
    using Args    = std::vector<std::string>;
    using Handler = std::function<void(std::string_view name, const Args& args)>;

    //! Fire a named event. All registered handlers are called synchronously
    //! in subscription order. Safe to call with no subscribers (no-op).
    static void Fire(std::string_view name, Args args = {});

    //! Register a handler for all events. Returns an ID for Unsubscribe().
    static int Subscribe(Handler h);

    //! Deregister a previously registered handler by ID. No-op for unknown IDs.
    static void Unsubscribe(int id);

private:
    struct Entry { int id; Handler fn; };
    static std::vector<Entry>& Listeners();
    static int& NextId();
};

#endif // TSCRIPTEVENT_H
