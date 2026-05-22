# Post-Processing for Moviepack Authors

This guide describes how to write post-processing (PP) effects for the Vulkan renderer. You'll learn the shader contract, the Lua render graph API, the parameter system, the uber-shader hook system that feeds the scene + emissive attachments, and how to ship all of this inside a moviepack.

For step-by-step examples (passthrough → depthviz → bloom), see `documentation/postprocess-tutorial.md`.

---

## 1. How post-processing fits into the frame

When post-processing is **enabled**, a frame flows through three stages:

```
  [ scene draw ]  ─▶  [ PP pipeline ]  ─▶  SWAPCHAIN  ─▶  present
       │                    │
       │                    └─▶ reads SCENE_COLOR, SCENE_DEPTH, SCENE_EMISSIVE
       │                        runs N fullscreen passes
       │
       └─▶ writes to an offscreen render pass with TWO color attachments:
           - SCENE_COLOR    (location 0 of uber.frag)
           - SCENE_EMISSIVE (location 1 of uber.frag, populated by hooks)
           and a shared SCENE_DEPTH attachment.
```

When PP is **disabled** the scene draws directly into the swapchain image and none of the PP machinery runs — zero overhead.

PP is activated implicitly by moviepack selection. A pack provides PP iff it ships `shaders/postprocess/<packName>/<packName>.lua` (the pack's name and the effect script's directory name must match). Activating the pack turns PP on; selecting None or another pack without PP turns it off. macOS keeps PP forced on with the built-in `passthrough` effect for MoltenVK depth preservation.

---

## 2. Effect directory layout

An effect lives in its own directory under `shaders/postprocess/`:

```
shaders/postprocess/<effect_name>/
    <effect_name>.lua        — render graph declaration (required)
    <some_shader>.frag       — one per pass declared in the .lua
    <more_shader>.frag
    shared_helpers.glsl      — anything you want #included; freely named
```

**Discovery paths, searched in order** (first match wins):

1. `moviepack/shaders/postprocess/<effect_name>/` — current moviepack
2. `shaders/postprocess/<effect_name>/` — system shaders dir
3. `~/.armagetronad/shaders/postprocess/<effect_name>/` — user overrides

This means you can ship an effect inside your moviepack and the engine picks it up automatically when the moviepack is activated.

**Ship source, not SPIR-V.** The engine compiles `.frag` files at effect-load time via libshaderc. No `.spv` files exist on disk and you should not create any.

---

## 3. The fragment shader contract

Every PP fragment shader conforms to a fixed interface. Copy the layout from `shaders/postprocess/passthrough/passthrough.frag`:

```glsl
#version 450

// --- Samplers (set 0) ---
// The engine declares 4 bindings regardless; unused ones sample zero.
// Always declare at least the bindings you actually use.
layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 2) uniform sampler2D uUnused2;
layout(set = 0, binding = 3) uniform sampler2D uUnused3;

// --- Parameter UBO (set 1) ---
// Declares `pp.fparams[8]`, `pp.iparams[4]`, and the
// FP(slot)/IP(slot)/FP4(slot) macros. Always #include this.
#include "postprocess_params.glsl"

// --- Push constants ---
layout(push_constant) uniform PushConstants {
    vec2  uResolution;  // target attachment resolution (pixels)
    float uTime;        // seconds since engine start
    float _pad;
    vec4  uArenaBBox;   // (minX, minY, maxX, maxY) of current arena
} pc;

// --- I/O ---
layout(location = 0) in  vec2 vTexCoord;  // from fullscreen.vert
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(uSceneColor, vTexCoord);
}
```

**Vertex shader is fixed.** The engine always uses `shaders/postprocess/fullscreen.vert` — a 3-vertex oversized-triangle trick with no vertex buffer. You don't write your own vertex shader.

### Built-in sampler sources

| Name | What | Format |
|---|---|---|
| `SCENE_COLOR` | Scene color attachment (what the player sees) | `rgba8` |
| `SCENE_EMISSIVE` | Per-pixel glow contribution from the uber shader's emissive hooks | `rgba8`, alpha = max RGB channel |
| `SCENE_DEPTH` | Scene depth buffer | depth |

Any other name refers to an **intermediate render target** declared with `effect:resource()` in the `.lua` script.

### Push constant usage

- `uResolution` is the **target attachment's** pixel size, not the swapchain's. For a half-res pass it's half the window width/height. Use it to compute texel offsets.
- `uTime` is frame-synchronous and useful for animated effects.
- `uArenaBBox` lets you express effects in world-space rather than screen-space.

---

## 4. The Lua render graph API

Each effect has a `.lua` script that runs when the effect loads. The renderer injects a global `effect` (an `EffectBuilder` object) before the script runs.

### Declaring passes

```lua
-- Declare an intermediate render target
effect:resource(name, format, scale)
-- name   — identifier used in later sampler/pass calls
-- format — "RGBA8" | "RGBA16F" | "R8"
-- scale  — resolution as a fraction of swapchain (0.5 = half res)

-- Declare a fullscreen pass
effect:pass(shader, target)
-- shader — basename of the .frag file, without extension
-- target — resource name or "SWAPCHAIN"

-- Bind a texture to the most recently declared pass
effect:sampler(binding, source)
-- binding — layout(binding = N) in the fragment shader (0..3)
-- source  — built-in name or resource name
```

Passes execute in declaration order. The last pass must target `"SWAPCHAIN"`.

### Declaring parameters

```lua
effect:param_float(name, slot, default, min, max, description)
effect:param_int  (name, slot, default, min, max, description)
effect:param_vec4 (name, slot, r, g, b, a, description)
```

- `slot` is the index used in the shader with `FP(slot)` / `IP(slot)` / `FP4(slot)`.
- `PARAM_VEC4` occupies 4 consecutive float slots starting at `slot` — that slot must be vec4-aligned (0, 4, 8, 12, ...).
- Float and int slots are independent — `param_float 0` and `param_int 0` don't collide.

### Reading parameters in the shader

```glsl
#include "postprocess_params.glsl"

// Give slots readable names
#define P_INTENSITY  FP(0)
#define P_THRESHOLD  FP(1)
#define P_BANDS      IP(0)
#define P_TINT       FP4(4)   // vec4 at slot 4 (must be aligned to 4)

void main()
{
    vec3 glow = texture(...).rgb * P_INTENSITY;
    ...
}
```

The parameter UBO has space for **32 float slots** (read via `FP()` / `FP4()`) and **16 int slots** (read via `IP()`).

### Minimal single-pass example

```lua
-- passthrough.lua
effect:pass("passthrough", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
```

### Multi-pass example

```lua
-- bloom.lua
effect:resource("bloom_half",        "RGBA8", 0.5)
effect:resource("bloom_half_tmp",    "RGBA8", 0.5)
effect:resource("bloom_quarter",     "RGBA8", 0.25)
effect:resource("bloom_quarter_tmp", "RGBA8", 0.25)

effect:pass("bloom_extract",   "bloom_half")       ; effect:sampler(0, "SCENE_EMISSIVE")
effect:pass("bloom_blur_h",    "bloom_half_tmp")   ; effect:sampler(0, "bloom_half")
effect:pass("bloom_blur_v",    "bloom_half")       ; effect:sampler(0, "bloom_half_tmp")
effect:pass("bloom_downsample","bloom_quarter")    ; effect:sampler(0, "bloom_half")
effect:pass("bloom_blur_h",    "bloom_quarter_tmp"); effect:sampler(0, "bloom_quarter")
effect:pass("bloom_blur_v",    "bloom_quarter")    ; effect:sampler(0, "bloom_quarter_tmp")
effect:pass("bloom_composite", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
effect:sampler(1, "bloom_half")
effect:sampler(2, "bloom_quarter")

effect:param_float("intensity",   0, 1.5, 0.0, 5.0, "Final composite strength")
effect:param_float("threshold",   1, 0.05, 0.0, 1.0, "Emissive alpha cutoff")
effect:param_float("radius",      2, 1.5, 0.5, 4.0, "Blur radius multiplier")
effect:param_float("half_weight", 3, 0.6, 0.0, 1.0, "Half-res pyramid contribution")
effect:param_vec4 ("tint",        4, 1.0, 1.0, 1.0, 1.0, "Glow color tint")
```

Note that `bloom_blur_h` and `bloom_blur_v` are each used twice (at half and quarter res). The engine compiles each shader once and binds the appropriate target per pass — you don't need one `.frag` per resolution.

### Limits

| Limit | Value |
|---|---|
| Max passes per effect | 16 |
| Max samplers per pass | 4 |
| Float parameter slots | 32 |
| Int parameter slots | 16 |

---

## 5. Parameter live tuning

Once an effect is active, every declared parameter is a runtime config item named `MVP_<EFFECTNAME>_<PARAMNAME>` (uppercase). From the in-game console (owner level) or from a Lua script:

```lua
aa_config_set("MVP_BLOOM_INTENSITY", "2.5")
aa_config_set("MVP_BLOOM_TINT", "1.0 0.7 1.0 1.0")
aa_config_set("MVP_GRAYSCALE_AMOUNT", "0.7")
```

Changes are applied by the next rendered frame.

---

## 6. Parameter persistence

Tuned values survive across sessions when a moviepack is active:

- **Where**: `moviepack_<name>.cfg`, saved alongside `user.cfg` in the user data dir.
- **When saved**: on moviepack deactivation (explicit switch, or game exit while active).
- **When loaded**: on moviepack activation, after the effect has been loaded and its `MVP_*` settings registered.
- **Never in `user.cfg`**: the engine uses `tSettingItem` for these, which forbids writes to `user.cfg`. Switching moviepacks cleanly removes the `MVP_*` entries from the settings registry.

---

## 7. The uber shader hook system

This section is only relevant if your effect needs to control which parts of the scene glow (or otherwise modify per-component rendering). Skip it if your effect just reads `SCENE_COLOR` / `SCENE_DEPTH`.

### Concept

The system uber fragment shader (`shaders/uber.frag`) calls small per-component **hook functions** defined in `uber_hooks.glsl`. Moviepacks can override `uber_hooks.glsl` to change those behaviours without touching `uber.frag` itself. This keeps moviepacks forward-compatible with engine updates that change the main shader body.

**Compilation flow:**

1. Moviepack activates.
2. The engine looks for `moviepack/shaders/uber_hooks.glsl`. If present, it's used; otherwise, the system version.
3. `uber.frag` is compiled at load time with the resolved include path.

### Two kinds of hooks

#### Color hooks — modify displayed pixels

```glsl
vec4 hookSky      (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookFloor    (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookRimWall  (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookCycleWall(vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookCycle    (vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal);
vec4 hookZone     (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookEffects  (vec4 color, vec2 texCoord, vec3 modelPos);
```

Each hook receives the shaded color and returns a potentially modified color. The default implementations are passthrough.

#### Emissive hooks — control what glows

```glsl
vec3 hookCycleEmissive    (vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal);
vec3 hookCycleWallEmissive(vec4 color, vec2 texCoord, vec3 modelPos);
vec3 hookZoneEmissive     (vec4 color, vec2 texCoord, vec3 modelPos);
```

Return the per-pixel RGB glow contribution. Returning `vec3(0.0)` means "this pixel doesn't glow". The returned color is written (along with computed alpha = max channel) to `SCENE_EMISSIVE`, which bloom reads as input.

Default implementations give cycles full-intensity glow, cycle walls 80%, and zones 60%.

### Globals available inside hooks

```glsl
uTime              // float, seconds (same as push constant)
uRenderContext     // int, rRenderContext enum value (for debug/switches)
uArenaBBox         // vec4, (minX, minY, maxX, maxY) world coords
```

Render context IDs:

| ID | Component |
|---|---|
| 4 | Sky |
| 5 | Floor |
| 6 | RimWalls |
| 7 | PlayerWalls (cycle trails) |
| 8 | Cycles (bodies) |
| 9 | Zones |
| 10 | Effects (sparks, explosions) |

### Hooks + post-process parameters

Both hook and post-process fragment shaders see the **same** `MVP_*` UBO. This means you can:

- Declare a `param_float("glow_boost", ...)` in your `.lua` script.
- Read it via `FP(slot)` from both the bloom fragment shader AND your custom `uber_hooks.glsl` (include `postprocess_params.glsl` in the hook file too).
- Tuning `MVP_<EFFECT>_GLOW_BOOST` affects both scene shading and the bloom pass simultaneously.

### Minimum hooks-only moviepack

A moviepack can ship only a custom `uber_hooks.glsl` with no post-processing at all:

```
moviepack.aamvp/
    shaders/
        uber_hooks.glsl   # your custom hooks
    settings.cfg          # MVP_* tunables (optional)
```

---

## 8. Packaging as a moviepack

A `.aamvp.zip` moviepack with post-processing looks like this:

The effect directory name must match the moviepack's name. For a pack named `customBloom`:

```
customBloom.aamvp.zip
├── settings.cfg                 # (optional) MVP_* tunables
├── textures/                    # (standard moviepack textures, optional)
│   └── ...
├── models/                      # (standard moviepack models, optional)
│   └── ...
└── shaders/
    ├── uber_hooks.glsl          # (optional — override scene hooks)
    └── postprocess/
        └── customBloom/         # ← directory name matches pack name
            ├── customBloom.lua
            ├── customBloom.frag
            └── ...
```

The PP effect is auto-discovered on activation — no cfg key wires it up. `settings.cfg` is for MVP_* tunables only:

```
# customBloom.aamvp — Tron glow
MVP_CUSTOMBLOOM_INTENSITY 2.0
MVP_CUSTOMBLOOM_TINT 0.8 0.9 1.0 1.0
```

All config items in `settings.cfg` are applied with owner-level elevation when the moviepack activates.

---

## 9. Tutorial

See `documentation/postprocess-tutorial.md` for step-by-step examples building three effects from scratch:

1. **Passthrough** — single pass, no parameters
2. **Depthviz** — single pass, reads `SCENE_DEPTH`, tunable parameters
3. **Bloom** — multi-pass, intermediate resources, `SCENE_EMISSIVE` input

Complete working files are in `documentation/postprocess-examples/`.

---

## 10. Troubleshooting

**Nothing happens when I activate my moviepack.**
The PP effect is discovered by file convention: `shaders/postprocess/<packName>/<packName>.lua` must exist inside the pack, where `<packName>` matches the moviepack's name exactly (case-sensitive).

**"Effect script not found"**
The engine couldn't find `<effect_name>.lua` in the search path. The effect name must match the directory name.

**"Cannot find shaders/postprocess/fullscreen.vert"**
The system shaders directory isn't on the search path. Make sure you didn't delete or rename the system `shaders/postprocess/` directory.

**Shader compile errors on load.**
libshaderc prints them to stderr with `path:line:col: message`. Common causes:
- Missing `#version 450` at the top.
- Forgetting to `#include "postprocess_params.glsl"` when you use `FP(...)` / `IP(...)`.
- Declaring fewer sampler bindings than the engine expects — always declare bindings 0..3.

**`MVP_*` console commands say "unknown variable" after switching moviepacks.**
Expected — those settings exist only while the effect is loaded. Re-activate the moviepack to get them back.

**My vec4 parameter values are wrong / packed into the wrong channels.**
`param_vec4` slot must be aligned to 4 (0, 4, 8, 12, ...). Reading via `FP4(slot)` with an unaligned slot gives garbage.

**The emissive attachment looks empty (bloom has nothing to glow).**
Your moviepack's `uber_hooks.glsl` is probably overriding the emissive hooks to `return vec3(0.0)`. Either omit emissive overrides (inherit the defaults) or emit non-zero for the components you want to glow.

---

## 11. Reference files

Start by reading these in order:

1. `shaders/postprocess/passthrough/passthrough.lua` + `passthrough.frag` — minimum viable effect.
2. `documentation/postprocess-examples/depthviz/` — single-pass effect with parameters and `SCENE_DEPTH`.
3. `documentation/postprocess-examples/bloom/` — multi-pass framegraph with intermediate resources.
4. `shaders/uber_hooks.glsl` — default hook implementations, documented contract.
5. `shaders/postprocess_params.glsl` — the UBO layout and `FP`/`IP` macros.
6. `moviepacks/customBloom.aamvp.zip` and `moviepacks/customCelShading.aamvp.zip` — complete moviepack examples you can unzip and modify.
