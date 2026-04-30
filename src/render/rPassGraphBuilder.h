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

#ifndef RPASSGRAPHBUILDER_H
#define RPASSGRAPHBUILDER_H

#ifndef DEDICATED

#include "vulkan/rVulkanPostProcess.h"
#include "luapp.hpp"
#include <string_view>
#include <vector>

//! Lua-facing builder for a post-process effect.
//!
//! Injected into the Lua state as the global `effect` before executing an
//! effect's .lua script. The script calls methods on it to declare both the
//! render pass graph and the tunable parameters. C++ reads back the results
//! with BuildDesc() / BuildParams() after the script returns.
//!
//! -------------------------------------------------------------------------
//! Render graph API
//! -------------------------------------------------------------------------
//!
//!   effect:resource(name, format, scale)
//!     Declare an intermediate render target used as input or output between passes.
//!     name   — identifier referenced by pass() targets and sampler() sources
//!     format — "RGBA8" | "RGBA16F" | "R8"
//!     scale  — fraction of swapchain extent (e.g. 0.5 = half-resolution)
//!
//!   effect:pass(shader, target)
//!     Add a full-screen pass. Always call effect:sampler() immediately after.
//!     shader — fragment shader basename (shaders/postprocess/<effect>/<shader>.frag.spv)
//!     target — resource name declared above, or one of the built-in outputs:
//!              "SWAPCHAIN" — write directly to the swapchain (final pass)
//!
//!   effect:sampler(binding, source)
//!     Add a texture sampler to the most recently declared pass.
//!     binding — descriptor set binding index (0-based, must be unique per pass)
//!     source  — resource name declared above, or one of the built-in scene inputs:
//!              "SCENE_COLOR"    — the rendered scene (opaque + transparent geometry)
//!              "SCENE_EMISSIVE" — emissive channel (cycle walls, zones — used by bloom)
//!              "SCENE_DEPTH"    — depth buffer (0=near, 1=far in NDC)
//!
//! -------------------------------------------------------------------------
//! Parameter API  (tunable at runtime via CONFIG items / in-game menu)
//! -------------------------------------------------------------------------
//!
//!   effect:param_float(name, slot, default, min, max, description)
//!     Expose a float scalar. Maps to fparams[slot] in the shader's push-constant UBO.
//!     Values are clamped to [min, max]. Config item: MVP_<EFFECT>_<NAME>.
//!
//!   effect:param_int(name, slot, default, min, max, description)
//!     Expose an integer scalar. Maps to iparams[slot]. Stored as float internally.
//!
//!   effect:param_vec4(name, slot, r, g, b, a, description)
//!     Expose a vec4 color/vector. Maps to fparams[slot] (occupies one slot as a vec4).
//!     default values are the four RGBA components.
class EffectBuilder
{
public:
    EffectBuilder() = default;

    // --- render graph ---

    //! Add an intermediate render target. Called from Lua: effect:resource(...)
    void Resource(std::string_view name, std::string_view format, float scale);

    //! Start a new pass. Called from Lua: effect:pass(...)
    void Pass(std::string_view shader, std::string_view target);

    //! Add a sampler to the most recently declared pass. Called from Lua: effect:sampler(...)
    void Sampler(int binding, std::string_view source);

    // --- parameters ---

    //! Declare a float parameter. Called from Lua: effect:param_float(...)
    void ParamFloat(std::string_view name, int slot, float def, float minv, float maxv, std::string_view desc);

    //! Declare an integer parameter. Called from Lua: effect:param_int(...)
    void ParamInt(std::string_view name, int slot, int def, int minv, int maxv, std::string_view desc);

    //! Declare a vec4 parameter. Called from Lua: effect:param_vec4(...)
    void ParamVec4(std::string_view name, int slot, float r, float g, float b, float a, std::string_view desc);

    // --- extraction (called from C++ after the script returns) ---

    [[nodiscard]] rPostProcessPipelineDesc      BuildDesc()   const { return desc_; }
    [[nodiscard]] std::vector<rPostProcessParam> BuildParams() const { return params_; }

private:
    rPostProcessPipelineDesc       desc_;
    std::vector<rPostProcessParam> params_;
};

// ---- luapp userdata_traits specialization --------------------------------

namespace luapp {

template <>
struct userdata_traits<EffectBuilder> {
    static constexpr const char* name = "EffectBuilder";
};

} // namespace luapp

// ---- Registration + injection helpers ------------------------------------

//! Register the EffectBuilder class in the Lua state.
//! Call once after rLuaState is initialized (e.g. from rVulkanPostProcess::Init).
void rRegisterEffectBuilder(luapp::state_view L);

//! Inject a pre-constructed EffectBuilder as global "effect" in the Lua state.
//! Call before running an effect script; call rCleanEffectGlobal() after extracting results.
void rInjectEffectBuilder(luapp::state_view L, EffectBuilder& eb);

//! Remove the "effect" global (set to nil).
//! Call after extracting results via eb.BuildDesc() / eb.BuildParams().
void rCleanEffectGlobal(luapp::state_view L);

// ---- Backward-compatibility aliases (deprecated — use the above) ----------

using PassGraphBuilder  = EffectBuilder;   ///< @deprecated use EffectBuilder
using EffectMetaBuilder = EffectBuilder;   ///< @deprecated use EffectBuilder

#endif // DEDICATED
#endif // RPASSGRAPHBUILDER_H
