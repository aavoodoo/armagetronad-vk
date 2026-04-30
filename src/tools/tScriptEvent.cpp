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

#include "tScriptEvent.h"
#include <algorithm>

// Meyer's singleton storage — initialized on first access, avoiding any
// cross-TU static initialization order issues.

std::vector<tScriptEvent::Entry>& tScriptEvent::Listeners()
{
    static std::vector<Entry> s_listeners;
    return s_listeners;
}

int& tScriptEvent::NextId()
{
    static int s_id = 0;
    return s_id;
}

void tScriptEvent::Fire(std::string_view name, Args args)
{
    for (const auto& entry : Listeners())
        entry.fn(name, args);
}

int tScriptEvent::Subscribe(Handler h)
{
    int id = ++NextId();
    Listeners().push_back({id, std::move(h)});
    return id;
}

void tScriptEvent::Unsubscribe(int id)
{
    auto& L = Listeners();
    L.erase(std::remove_if(L.begin(), L.end(),
        [id](const Entry& e){ return e.id == id; }), L.end());
}
