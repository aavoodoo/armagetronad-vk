-- Passthrough — single pass copying the scene color to the swapchain.
-- This is the simplest possible effect script and serves as a working
-- reference for effect authors.
--
-- The global `effect` (EffectBuilder) is injected by the C++ renderer
-- before this script runs. Use it to declare the render graph and any
-- tunable parameters.
--
-- Render graph API:
--   effect:resource(name, format, scale)   -- declare intermediate render target
--   effect:pass(shader, target)            -- add a full-screen pass
--   effect:sampler(binding, source)        -- bind a texture to the last pass
--
--   Built-in sources: "SCENE_COLOR", "SCENE_EMISSIVE", "SCENE_DEPTH"
--   Built-in target:  "SWAPCHAIN" (final output)
--   Formats:          "RGBA8", "RGBA16F", "R8"
--
-- Parameter API:
--   effect:param_float(name, slot, default, min, max, description)
--   effect:param_int(name, slot, default, min, max, description)
--   effect:param_vec4(name, slot, r, g, b, a, description)

effect:pass("passthrough", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
