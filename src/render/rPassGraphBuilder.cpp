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

#include "rPassGraphBuilder.h"
#include <string>
#include <iostream>

// ---------------------------------------------------------------------------
// EffectBuilder — render graph methods
// ---------------------------------------------------------------------------

void EffectBuilder::Resource(std::string_view name, std::string_view format, float scale)
{
    rPostProcessResourceDecl r;
    r.name  = std::string(name);
    r.scale = scale;

    if      (format == "RGBA16F") r.format = rPostProcessFormat::RGBA16F;
    else if (format == "R8")      r.format = rPostProcessFormat::R8;
    else                          r.format = rPostProcessFormat::RGBA8;

    desc_.resources.push_back(std::move(r));
}

void EffectBuilder::Pass(std::string_view shader, std::string_view target)
{
    rPostProcessPassDecl p;
    p.shader = std::string(shader);
    p.target = std::string(target);
    desc_.passes.push_back(std::move(p));
}

void EffectBuilder::Sampler(int binding, std::string_view source)
{
    if (desc_.passes.empty())
    {
        std::cerr << "[EffectBuilder] effect:sampler() called before effect:pass() — ignored\n";
        return;
    }
    rPostProcessSamplerDecl s;
    s.binding = binding;
    s.source  = std::string(source);
    desc_.passes.back().samplers.push_back(std::move(s));
}

// ---------------------------------------------------------------------------
// EffectBuilder — parameter methods
// ---------------------------------------------------------------------------

void EffectBuilder::ParamFloat(std::string_view name, int slot,
                                float def, float minv, float maxv,
                                std::string_view desc)
{
    rPostProcessParam p;
    p.name        = std::string(name);
    p.description = std::string(desc);
    p.type        = rPostProcessParam::Float;
    p.slot        = slot;
    p.defaults[0] = def;
    p.minVal      = minv;
    p.maxVal      = maxv;
    params_.push_back(std::move(p));
}

void EffectBuilder::ParamInt(std::string_view name, int slot,
                              int def, int minv, int maxv,
                              std::string_view desc)
{
    rPostProcessParam p;
    p.name        = std::string(name);
    p.description = std::string(desc);
    p.type        = rPostProcessParam::Int;
    p.slot        = slot;
    p.defaults[0] = static_cast<float>(def);
    p.minVal      = static_cast<float>(minv);
    p.maxVal      = static_cast<float>(maxv);
    params_.push_back(std::move(p));
}

void EffectBuilder::ParamVec4(std::string_view name, int slot,
                               float r, float g, float b, float a,
                               std::string_view desc)
{
    rPostProcessParam p;
    p.name        = std::string(name);
    p.description = std::string(desc);
    p.type        = rPostProcessParam::Vec4;
    p.slot        = slot;
    p.defaults[0] = r;
    p.defaults[1] = g;
    p.defaults[2] = b;
    p.defaults[3] = a;
    params_.push_back(std::move(p));
}

// ---------------------------------------------------------------------------
// Registration + injection helpers
// ---------------------------------------------------------------------------

void rRegisterEffectBuilder(luapp::state_view L)
{
    luapp::register_class<EffectBuilder>(L)
        .destructor()
        .method<&EffectBuilder::Resource>  ("resource")
        .method<&EffectBuilder::Pass>      ("pass")
        .method<&EffectBuilder::Sampler>   ("sampler")
        .method<&EffectBuilder::ParamFloat>("param_float")
        .method<&EffectBuilder::ParamInt>  ("param_int")
        .method<&EffectBuilder::ParamVec4> ("param_vec4")
        .finalize("EffectBuilder");
}

void rInjectEffectBuilder(luapp::state_view L, EffectBuilder& eb)
{
    lua_State* raw = L.raw();
    void* mem = lua_newuserdata(raw, sizeof(EffectBuilder));
    new (mem) EffectBuilder(eb);    // copy-construct
    luaL_getmetatable(raw, luapp::userdata_traits<EffectBuilder>::name);
    lua_setmetatable(raw, -2);
    lua_setglobal(raw, "effect");
}

void rCleanEffectGlobal(luapp::state_view L)
{
    lua_State* raw = L.raw();
    lua_pushnil(raw);
    lua_setglobal(raw, "effect");
}

// Keep old names working during transition
void rRegisterPassGraphBuilders(luapp::state_view L) { rRegisterEffectBuilder(L); }
void rInjectBuilders(luapp::state_view L, EffectBuilder& pgb, EffectBuilder& emb)
{
    // pgb and emb are the same type now; just inject pgb as "effect"
    // (emb is ignored — callers should be updated to use the new API)
    rInjectEffectBuilder(L, pgb);
    (void)emb;
}
void rCleanBuilderGlobals(luapp::state_view L) { rCleanEffectGlobal(L); }

#endif // DEDICATED
