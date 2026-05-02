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

#include "tLuaState.h"
#include <cctype>
#include <iostream>
#include <string>

// Globals removed from the sandbox environment.
// Effect scripts have no business touching the filesystem, spawning processes,
// loading arbitrary C modules, or manipulating the interpreter itself.
static const char* const s_removedGlobals[] = {
    "io", "os", "require", "dofile", "loadfile", "load", "loadstring",
    "package", "debug", "newproxy", "collectgarbage", "gcinfo",
    nullptr
};

tLuaState::tLuaState()
{
    // Open all standard Lua libraries first (needed for internal bootstrap).
    state_.open_libs();

    // Sandbox: remove globals that allow filesystem access, process spawning,
    // dynamic module loading, and debugger hooks. Effect scripts only need
    // math, string, table, and the safe base functions.
    lua_State* L = state_.raw();
    for (int i = 0; s_removedGlobals[i] != nullptr; ++i)
    {
        lua_pushnil(L);
        lua_setglobal(L, s_removedGlobals[i]);
    }

    alive_ = true;
}

tLuaState& tLuaState::Instance()
{
    static tLuaState s_instance;
    return s_instance;
}

void tLuaState::Shutdown()
{
    if (!alive_) return;
    // luapp::state destructor calls lua_close; move-assign to a fresh empty
    // state to close the current one without destroying the singleton itself.
    state_ = luapp::state{};
    alive_ = false;
}

bool tLuaState::DoString(const char* source, const char* chunkname)
{
    if (!alive_) return false;
    lua_State* L = state_.raw();
    // Use luaL_loadbuffer so we control the chunk name.
    // "=(eval)" — the leading '=' tells Lua to use the rest verbatim as the
    // source name, giving clean error messages like "(eval):1: ..." instead of
    // [string "the full source text here..."].
    int loadResult = luaL_loadbuffer(L, source, strlen(source), chunkname);
    if (loadResult != 0)
    {
        const char* msg = lua_tostring(L, -1);
        std::cerr << "[Lua] Error: " << (msg ? msg : "(no message)") << "\n";
        lua_pop(L, 1);
        return false;
    }
    int callResult = lua_pcall(L, 0, 0, 0);
    if (callResult != 0)
    {
        const char* msg = lua_tostring(L, -1);
        std::cerr << "[Lua] Error: " << (msg ? msg : "(no message)") << "\n";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool tLuaState::DoFile(const char* path)
{
    if (!alive_) return false;

    // Read the file on the C++ side rather than using luaL_dofile.
    // This ensures the sandbox (no 'io' global) also applies here — the script
    // itself cannot re-open files even if it reconstructs the io table.
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        std::cerr << "[Lua] Cannot open '" << path << "': " << strerror(errno) << "\n";
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    std::string src(static_cast<size_t>(sz), '\0');
    fread(src.data(), 1, static_cast<size_t>(sz), f);
    fclose(f);

    // Use load() (luaL_loadbuffer) with a chunkname so error messages show the path.
    lua_State* L = state_.raw();
    std::string chunkname = "@";
    chunkname += path;
    int loadResult = luaL_loadbuffer(L, src.c_str(), src.size(), chunkname.c_str());
    if (loadResult != 0)
    {
        const char* msg = lua_tostring(L, -1);
        std::cerr << "[Lua] Error loading '" << path << "': " << (msg ? msg : "(no message)") << "\n";
        lua_pop(L, 1);
        return false;
    }
    int callResult = lua_pcall(L, 0, 0, 0);
    if (callResult != 0)
    {
        const char* msg = lua_tostring(L, -1);
        std::cerr << "[Lua] Error in '" << path << "': " << (msg ? msg : "(no message)") << "\n";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void tLuaState::DispatchEvent(const char* name, const char* args)
{
    tLuaState& self = tLuaState::Instance();
    if (!self.IsAlive()) return;

    lua_State* L = self.View().raw();
    std::string fn = "on_";
    for (const char* p = name; *p; ++p)
        fn += static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    lua_getglobal(L, fn.c_str());
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, args);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
        std::cerr << "[Lua] " << fn << "(): " << lua_tostring(L, -1) << '\n';
        lua_pop(L, 1);
    }
}

#endif // DEDICATED
