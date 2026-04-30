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

#include "aa_config.h"

#ifndef DEDICATED

#include "gLuaBindings.h"
#include "gCycle.h"
#include "ePlayer.h"
#include "eTeam.h"
#include "tConfiguration.h"
#include "tLuaState.h"
#include "luapp.hpp"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// tLuaConfItem — ephemeral config item backed by Lua
// ---------------------------------------------------------------------------
// Created by aa_setting(name, default). Registered with the console system
// so it can be changed with "NAME value" from the console or config commands.
// Never written to user.cfg (Save() returns false).
// On change, the matching Lua global is updated so scripts can read it directly.
// ---------------------------------------------------------------------------

class tLuaConfItem : public tConfItemBase
{
    std::string value_;   //!< current value as string

public:
    tLuaConfItem(const char* name, const char* defaultValue)
        : tConfItemBase(name), value_(defaultValue) {}

    // Never save to config files — this is a per-session runtime value.
    virtual bool Save() override { return false; }

    virtual void WriteVal(std::ostream& s) override
    {
        s << value_;
    }

    virtual void ReadVal(std::istream& s) override
    {
        // Eat leading blanks (but not newlines).
        int c = EatWhitespace(s);

        if (c != '\n' && s.good() && !s.eof())
        {
            // Read the rest of the line as the new value, so multi-word
            // strings like "Hello World" are captured whole.
            std::string newVal;
            std::getline(s, newVal);
            // Trim trailing CR and spaces.
            while (!newVal.empty() && (newVal.back() == '\r' || newVal.back() == ' '))
                newVal.pop_back();
            if (!newVal.empty())
            {
                value_ = newVal;
                changed = true;
                ExecuteCallback();
                SyncToLua();
            }
        }
        // getline already consumed the newline; nothing more to skip.
    }

    //! Push the current value to the Lua global with the item's name.
    void SyncToLua()
    {
        tLuaState& ls = tLuaState::Instance();
        if (!ls.IsAlive()) return;
        lua_State* L = ls.View().raw();
        // Try to coerce to number so Lua sees a number not a string.
        char* end = nullptr;
        double num = strtod(value_.c_str(), &end);
        if (end && *end == '\0' && !value_.empty())
            lua_pushnumber(L, num);
        else
            lua_pushstring(L, value_.c_str());
        lua_setglobal(L, static_cast<const char*>(title));
    }
};

// Owns all tLuaConfItems created during this process lifetime.
// tConfItemBase registers/deregisters itself, so these must outlive the config map.
static std::vector<std::unique_ptr<tLuaConfItem>> s_luaConfItems;

// ---------------------------------------------------------------------------
// aa_setting(name, default_value)
// ---------------------------------------------------------------------------
// Declares a new ephemeral config item. Safe to call multiple times with the
// same name (idempotent — subsequent calls are ignored so scripts can be
// re-evaluated without re-registering already-live settings).
// The default value is pushed as the Lua global on first declaration.
// ---------------------------------------------------------------------------

static int l_setting(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    if (!lua_isnumber(L, 2) && !lua_isstring(L, 2))
        return luaL_argerror(L, 2, "expected number or string");

    // Produce the string representation for the C++ backing store.
    const char* defStr = lua_tostring(L, 2); // Lua coerces number→string here

    tString tname(name);

    // Idempotent: if already registered (e.g. from a previous call), skip.
    if (tConfItemBase::FindConfigItem(tname))
        return 0;

    // Create and register the new item.
    auto item = std::make_unique<tLuaConfItem>(name, defStr);
    s_luaConfItems.push_back(std::move(item));

    // Set the Lua global to the default value, preserving the original Lua type.
    lua_pushvalue(L, 2);    // push the original default (number or string)
    lua_setglobal(L, name); // MY_SETTING = default
    return 0;
}

// ---------------------------------------------------------------------------
// Config access helpers (used internally by the config proxy)
// ---------------------------------------------------------------------------

// l_config_get(name) -> string | nil
static int l_config_get(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    tString tname(name);
    tConfItemBase* item = tConfItemBase::FindConfigItem(tname);
    if (!item) { lua_pushnil(L); return 1; }
    std::ostringstream oss;
    item->WriteVal(oss);
    lua_pushstring(L, oss.str().c_str());
    return 1;
}

// l_config_set(name, value) -> bool
static int l_config_set(lua_State* L)
{
    const char* name  = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    tString tname(name);
    tConfItemBase* item = tConfItemBase::FindConfigItem(tname);
    if (!item) { lua_pushboolean(L, 0); return 1; }
    tCurrentAccessLevel accessGuard(tAccessLevel_Owner, true);
    std::istringstream iss(value);
    item->ReadVal(iss);
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// Player query
// ---------------------------------------------------------------------------

// aa_players() -> array of tables
// Each table: name, score, ping, team, spectating, chatting, color_r/g/b,
//             alive — and when alive: x, y, dir_x, dir_y, speed, rubber
static int l_players(lua_State* L)
{
    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < se_PlayerNetIDs.Len(); i++)
    {
        ePlayerNetID* p = se_PlayerNetIDs(i);
        if (!p || !p->IsActive()) continue;

        lua_newtable(L);

        lua_pushstring(L, static_cast<const char*>(p->GetName()));
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, p->Score());
        lua_setfield(L, -2, "score");
        lua_pushnumber(L, static_cast<double>(p->ping));
        lua_setfield(L, -2, "ping");
        lua_pushboolean(L, p->IsSpectating() ? 1 : 0);
        lua_setfield(L, -2, "spectating");
        lua_pushboolean(L, p->IsChatting() ? 1 : 0);
        lua_setfield(L, -2, "chatting");

        eTeam* team = p->CurrentTeam();
        lua_pushstring(L, team ? static_cast<const char*>(team->Name()) : "");
        lua_setfield(L, -2, "team");

        lua_pushinteger(L, p->color.r_);
        lua_setfield(L, -2, "color_r");
        lua_pushinteger(L, p->color.g_);
        lua_setfield(L, -2, "color_g");
        lua_pushinteger(L, p->color.b_);
        lua_setfield(L, -2, "color_b");

        gCycle* cycle = dynamic_cast<gCycle*>(p->Object());
        if (cycle && cycle->Alive())
        {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "alive");

            eCoord pos = cycle->Position();
            lua_pushnumber(L, static_cast<double>(pos.x));
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, static_cast<double>(pos.y));
            lua_setfield(L, -2, "y");

            eCoord dir = cycle->Direction();
            lua_pushnumber(L, static_cast<double>(dir.x));
            lua_setfield(L, -2, "dir_x");
            lua_pushnumber(L, static_cast<double>(dir.y));
            lua_setfield(L, -2, "dir_y");

            lua_pushnumber(L, static_cast<double>(cycle->Speed()));
            lua_setfield(L, -2, "speed");
            lua_pushnumber(L, static_cast<double>(cycle->GetRubber()));
            lua_setfield(L, -2, "rubber");
        }
        else
        {
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "alive");
        }

        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Team query
// ---------------------------------------------------------------------------

// aa_teams() -> array of tables
// Each table: name, score, num_players, num_humans, num_ais, alive_players,
//             color_r/g/b, players (array of name strings)
static int l_teams(lua_State* L)
{
    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < eTeam::teams.Len(); i++)
    {
        eTeam* t = eTeam::teams(i);
        if (!t) continue;

        lua_newtable(L);

        lua_pushstring(L, static_cast<const char*>(t->Name()));
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, t->Score());
        lua_setfield(L, -2, "score");
        lua_pushinteger(L, t->NumPlayers());
        lua_setfield(L, -2, "num_players");
        lua_pushinteger(L, t->NumHumanPlayers());
        lua_setfield(L, -2, "num_humans");
        lua_pushinteger(L, t->NumAIPlayers());
        lua_setfield(L, -2, "num_ais");
        lua_pushinteger(L, t->AlivePlayers());
        lua_setfield(L, -2, "alive_players");
        lua_pushinteger(L, static_cast<int>(t->R()));
        lua_setfield(L, -2, "color_r");
        lua_pushinteger(L, static_cast<int>(t->G()));
        lua_setfield(L, -2, "color_g");
        lua_pushinteger(L, static_cast<int>(t->B()));
        lua_setfield(L, -2, "color_b");

        lua_newtable(L);
        for (int j = 0; j < t->NumPlayers(); j++)
        {
            ePlayerNetID* p = t->Player(j);
            if (p)
            {
                lua_pushstring(L, static_cast<const char*>(p->GetName()));
                lua_rawseti(L, -2, j + 1);
            }
        }
        lua_setfield(L, -2, "players");

        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void gRegisterLuaBindings(lua_State* L)
{
    // Raw helpers (available but usually accessed through the config proxy)
    lua_register(L, "aa_config_get", l_config_get);
    lua_register(L, "aa_config_set", l_config_set);

    // Game object queries
    lua_register(L, "aa_players",    l_players);
    lua_register(L, "aa_teams",      l_teams);

    // Ephemeral setting declaration
    lua_register(L, "aa_setting",    l_setting);

    // Bootstrap the `config` proxy table.
    //
    // config.walls_length          → aa_config_get("WALLS_LENGTH"), coerced to number when possible
    // config.walls_length = 150    → aa_config_set("WALLS_LENGTH", "150")
    //
    // Works for both built-in C++ config items and aa_setting() items.
    // Keys are uppercased automatically so both `config.walls_length` and
    // `config.WALLS_LENGTH` work.
    tLuaState::Instance().DoString(
        "config = setmetatable({}, {\n"
        "  __index = function(_, key)\n"
        "    local v = aa_config_get(key:upper())\n"
        "    if v == nil then return nil end\n"
        "    return tonumber(v) or v\n"
        "  end,\n"
        "  __newindex = function(_, key, val)\n"
        "    aa_config_set(key:upper(), tostring(val))\n"
        "  end\n"
        "})\n",
        "=(config proxy)"
    );
}

#endif // DEDICATED
