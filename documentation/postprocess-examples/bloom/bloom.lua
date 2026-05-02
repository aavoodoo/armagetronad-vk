-- Multi-pass Tron glow bloom.
--
-- Sources SCENE_EMISSIVE (populated by the uber shader's emissive hooks for
-- cycles, cycle walls, and zones by default), runs a 2-level Gaussian blur
-- pyramid, and additively composites the result over SCENE_COLOR.
--
-- Pyramid layout:
--   SCENE_EMISSIVE (full)  -extract->  bloom_half          (1/2 res, bright pass + downsample)
--   bloom_half             -blur_h ->  bloom_half_tmp       (horizontal gaussian)
--   bloom_half_tmp         -blur_v ->  bloom_half           (vertical gaussian)
--   bloom_half             -dsample->  bloom_quarter        (1/4 res downsample)
--   bloom_quarter          -blur_h ->  bloom_quarter_tmp
--   bloom_quarter_tmp      -blur_v ->  bloom_quarter
--   SCENE_COLOR + bloom_half + bloom_quarter -composite-> SWAPCHAIN

effect:resource("bloom_half",        "RGBA8", 0.5)
effect:resource("bloom_half_tmp",    "RGBA8", 0.5)
effect:resource("bloom_quarter",     "RGBA8", 0.25)
effect:resource("bloom_quarter_tmp", "RGBA8", 0.25)

-- Level 1: extract into half-resolution
effect:pass("bloom_extract", "bloom_half")
effect:sampler(0, "SCENE_EMISSIVE")

-- Level 1 separable blur (horizontal then vertical)
effect:pass("bloom_blur_h", "bloom_half_tmp")
effect:sampler(0, "bloom_half")

effect:pass("bloom_blur_v", "bloom_half")
effect:sampler(0, "bloom_half_tmp")

-- Level 2: downsample the blurred half to quarter-res
effect:pass("bloom_downsample", "bloom_quarter")
effect:sampler(0, "bloom_half")

-- Level 2 separable blur
effect:pass("bloom_blur_h", "bloom_quarter_tmp")
effect:sampler(0, "bloom_quarter")

effect:pass("bloom_blur_v", "bloom_quarter")
effect:sampler(0, "bloom_quarter_tmp")

-- Final composite: additively combines both pyramid levels over scene color
effect:pass("bloom_composite", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
effect:sampler(1, "bloom_half")
effect:sampler(2, "bloom_quarter")

-- Tunable parameters (exposed as MVP_BLOOM_* config items while loaded)
effect:param_float("intensity",   0, 1.5, 0.0, 5.0,
    "Final composite strength")
effect:param_float("threshold",   1, 0.05, 0.0, 1.0,
    "Emissive alpha cutoff (near-0 removes noise)")
effect:param_float("radius",      2, 1.5, 0.5, 4.0,
    "Blur radius multiplier (wider = softer halo)")
effect:param_float("half_weight", 3, 0.6, 0.0, 1.0,
    "Half-res pyramid level contribution (vs quarter-res)")
effect:param_vec4 ("tint",        4, 1.0, 1.0, 1.0, 1.0,
    "Glow color tint (RGB * intensity)")
