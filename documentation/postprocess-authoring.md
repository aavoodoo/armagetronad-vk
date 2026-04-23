# Post-Processing for Moviepack Authors

This guide describes how to write post-processing (PP) effects for the Vulkan renderer. You'll learn the shader contract, the `.pipeline` framegraph DSL, the `.meta` parameter file, the uber-shader hook system that feeds the scene + emissive attachments, and how to ship all of this inside a moviepack.

If you just want to try the reference effects, copy `moviepacks/customBloom.aamvp.zip` or `moviepacks/customCelShading.aamvp.zip`, unzip it, and tweak from there.

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

The toggle lives in two config items:

| Config key | Type | Default | Purpose |
|---|---|---|---|
| `POST_PROCESS_ENABLED` | bool | `0` | Master on/off |
| `POST_PROCESS_EFFECT` | string | `""` | Directory name of the active effect |


---

## 2. Effect directory layout

An effect lives in its own directory under `shaders/postprocess/`:

```
shaders/postprocess/<effect_name>/
    <effect_name>.pipeline   (optional — single-pass effects can omit)
    <effect_name>.meta       (optional — declares tunable parameters)
    <some_shader>.frag       (one per PASS in the .pipeline file)
    <more_shader>.frag
    shared_helpers.glsl      (anything you want #included; freely named)
```

**Discovery paths, searched in order** (first match wins):

1. `moviepack/shaders/postprocess/<effect_name>/` — current moviepack
2. `shaders/postprocess/<effect_name>/` — system shaders dir
3. `~/.armagetronad/shaders/postprocess/<effect_name>/` — user overrides

This means you can ship an effect inside your moviepack and the engine picks it up automatically when the moviepack is activated.

**Ship source, not SPIR-V.** The engine compiles `.frag` files at effect-load time via libshaderc. No `.spv` files exist on disk and you should not create any — the build system will ignore them.

---

## 3. The fragment shader contract

Every PP fragment shader conforms to a fixed interface. Copy the layout from `shaders/postprocess/passthrough/passthrough.frag`:

```glsl
#version 450

// --- Samplers (set 0) ---
// One binding per SAMPLER line in the .pipeline file. The engine's layout
// declares 4 bindings regardless; unused ones can be any sampler2D and
// will read zero. Always declare exactly 4 (0..3).
layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 2) uniform sampler2D uUnused2;
layout(set = 0, binding = 3) uniform sampler2D uUnused3;

// --- Parameter UBO (set 1) ---
// Shared helper — declares `pp.fparams[8]`, `pp.iparams[4]`, and the
// FP(slot)/IP(slot)/FP4(slot) macros. Always #include this if you have
// any tunable parameters; harmless otherwise.
#include "postprocess_params.glsl"

// --- Push constants ---
// Populated by the engine, identical for every pass.
layout(push_constant) uniform PushConstants {
    vec2  uResolution;  // target attachment resolution (pixels)
    float uTime;        // seconds since engine start
    float _pad;         // keep the struct 16-byte aligned
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

In the `.pipeline` file you bind samplers to named sources. The built-in ones are:

| Name | What | Format |
|---|---|---|
| `SCENE_COLOR` | Scene color attachment (what the player sees) | `rgba8` |
| `SCENE_EMISSIVE` | Per-pixel glow contribution from the uber shader's emissive hooks | `rgba8`, alpha encodes "how much this pixel glows" |
| `SCENE_DEPTH` | Scene depth buffer (reversed-Z: 1 = near, 0 = far) | depth |

Any other name refers to an **intermediate render target** declared with a `RESOURCE` line in the same `.pipeline` file.

### Push constant usage

- `uResolution` is the **target attachment's** pixel size, not the swapchain's. For a half-res pass it's half the window width/height. Use it to compute texel offsets.
- `uTime` is frame-synchronous and useful for animated effects. Don't rely on it for physics — it's not stable across save-states.
- `uArenaBBox` lets you express effects in world-space rather than screen-space. For example: `length(worldPos.xy - arenaCenter)` as a glow falloff.

---

## 4. The `.pipeline` framegraph DSL

If your effect is a single pass reading only `SCENE_COLOR` + `SCENE_DEPTH`, you can skip the `.pipeline` file entirely. The engine will synthesize a default one-pass descriptor that calls `<effect_name>.frag` with `SCENE_COLOR` bound to binding 0 and `SCENE_DEPTH` to binding 1, writing to `SWAPCHAIN`.

For anything more complex, write a `.pipeline` file.

### Syntax

```
# comment (line-oriented, # or // both work; whitespace between tokens free)

RESOURCE <name> <format> <scale>
PASS <shader> <target>
  SAMPLER <binding> <source>
  SAMPLER <binding> <source>
  ...
PASS <shader> <target>
  ...
```

Parsing is line-oriented and order-sensitive: every `SAMPLER` line attaches to the most recently declared `PASS`.

### `RESOURCE` — declare an intermediate render target

```
RESOURCE bloom_half rgba8 0.5
```

- `<name>` — the identifier used in later `SAMPLER` lines and `PASS` targets. Must be unique within the effect. Avoid the reserved names `SCENE_COLOR`, `SCENE_EMISSIVE`, `SCENE_DEPTH`, and `SWAPCHAIN`.
- `<format>` — one of `rgba8`, `rgba16f`, `r8`.
- `<scale>` — resolution as a fraction of the swapchain (e.g. `1.0`, `0.5`, `0.25`). Automatically recomputed on window resize.

Resources live for the duration of the effect — not per-frame. The engine allocates them once, keeps them resident, and recreates them on swapchain resize.

### `PASS` — declare a fullscreen triangle pass

```
PASS bloom_extract bloom_half
  SAMPLER 0 SCENE_EMISSIVE
```

- `<shader>` — the basename of the fragment shader file, **without** extension. The engine loads `<shader>.frag` from the effect directory.
- `<target>` — either a `RESOURCE` name declared earlier in the same file, or the built-in `SWAPCHAIN`.

Passes execute in file order. **Exactly one** pass must target `SWAPCHAIN` and it must be the last pass — its output is what gets presented.

### `SAMPLER` — attach a sampler binding to a pass

```
SAMPLER 0 SCENE_COLOR
SAMPLER 1 bloom_half
```

- `<binding>` — the `layout(binding = N)` in the fragment shader (0..3).
- `<source>` — built-in name (`SCENE_COLOR`, `SCENE_EMISSIVE`, `SCENE_DEPTH`) or a `RESOURCE` declared in the same file.

A pass that doesn't declare bindings 0..3 still has all four sampler descriptors written (the engine fills the missing ones with a dummy). This means you can always declare four `sampler2D` uniforms in the shader without `.pipeline` having to match — unused ones just sample black.

### Limits

| Limit | Value |
|---|---|
| `PP_MAX_PASSES_PER_EFFECT` | 16 |
| `PP_MAX_SAMPLERS_PER_PASS` | 4 |

### Complete multi-pass example — `bloom.pipeline`

From `shaders/postprocess/bloom/bloom.pipeline`:

```
# Multi-pass Tron glow bloom.

RESOURCE bloom_half        rgba8  0.5
RESOURCE bloom_half_tmp    rgba8  0.5
RESOURCE bloom_quarter     rgba8  0.25
RESOURCE bloom_quarter_tmp rgba8  0.25

# --- Level 1: extract into half-resolution ---
PASS bloom_extract bloom_half
  SAMPLER 0 SCENE_EMISSIVE

# --- Level 1 separable blur (horizontal then vertical) ---
PASS bloom_blur_h bloom_half_tmp
  SAMPLER 0 bloom_half

PASS bloom_blur_v bloom_half
  SAMPLER 0 bloom_half_tmp

# --- Level 2: downsample the blurred half to quarter-res ---
PASS bloom_downsample bloom_quarter
  SAMPLER 0 bloom_half

# --- Level 2 separable blur ---
PASS bloom_blur_h bloom_quarter_tmp
  SAMPLER 0 bloom_quarter

PASS bloom_blur_v bloom_quarter
  SAMPLER 0 bloom_quarter_tmp

# --- Final composite ---
PASS bloom_composite SWAPCHAIN
  SAMPLER 0 SCENE_COLOR
  SAMPLER 1 bloom_half
  SAMPLER 2 bloom_quarter
```

Note that `bloom_blur_h` and `bloom_blur_v` are each used twice (at half and quarter res). The engine compiles each shader once and binds the appropriate target attachment per pass — you don't need one `.frag` per resolution.

---

## 5. The `.meta` parameter file

The `.meta` file declares which parameters players can tune. The engine:

1. Reads the file when the effect loads.
2. Allocates UBO slots and writes the defaults.
3. Registers a `tSettingItem<float>` or `tSettingItem<int>` named `MVP_<PARAM_NAME>` for every declared parameter, at owner access level.
4. Loads overrides from `moviepack_<activepack>.cfg` if it exists.
5. On moviepack unload, persists any tuned values back to that file and unregisters the settings.

**Why `MVP_`?** "MoviePack Variable". These are shared between scene hooks and post-process shaders — see §7.

**Why `tSettingItem` and not `tConfItem`?** `tSettingItem::Save()` returns false, so these values never land in `user.cfg` and never trigger "unknown variable" warnings on a future launch where the moviepack isn't loaded.

### Syntax

```
NAME <display name>
DESCRIPTION <one-line blurb>

PARAM_FLOAT <name> <slot> <default> <min> <max> <description>
PARAM_INT   <name> <slot> <default> <min> <max> <description>
PARAM_VEC4  <name> <slot> <r> <g> <b> <a> <description>
```

- `NAME` and `DESCRIPTION` are informational — used by the (still-pending) in-game menu.
- `PARAM_*` lines create tunable values readable from the fragment shader via `FP(slot)` / `IP(slot)` / `FP4(slot)`.

### Slot layout

The UBO has space for **32 float slots** and **16 int slots**, packed into `vec4`/`ivec4` arrays for std140 compatibility:

| UBO region | Size | Access |
|---|---|---|
| `fparams[8]` (vec4) | 32 float slots | `FP(0)`..`FP(31)` |
| `iparams[4]` (ivec4) | 16 int slots | `IP(0)`..`IP(15)` |

**Important rules:**

- Float slots and int slots are **independent** — `PARAM_FLOAT foo 0 ...` and `PARAM_INT bar 0 ...` don't collide.
- `PARAM_VEC4` occupies **4 consecutive float slots** starting at `<slot>`, and that slot must be **vec4-aligned**: 0, 4, 8, 12, 16, 20, 24, or 28. `FP4(slot)` returns a `vec4` containing the whole thing.
- A `PARAM_VEC4` at slot 4 means slots 4, 5, 6, 7 are consumed — don't also place a `PARAM_FLOAT` at any of them.

### Reading params in the shader

```glsl
#include "postprocess_params.glsl"

// Give your slots readable names — keeps the shader readable and
// makes .meta slot assignments the single source of truth.
#define P_INTENSITY  FP(0)
#define P_THRESHOLD  FP(1)
#define P_BANDS      IP(0)
#define P_TINT       FP4(4)

void main()
{
    vec3 glow = texture(...).rgb * P_INTENSITY;
    ...
}
```

### Example — `bloom.meta`

```
NAME Bloom (Tron Glow)
DESCRIPTION 2-level Gaussian blur pyramid applied to the scene emissive attachment.

PARAM_FLOAT intensity    0 1.5   0.0 5.0  Final composite strength
PARAM_FLOAT threshold    1 0.05  0.0 1.0  Emissive alpha cutoff
PARAM_FLOAT radius       2 1.5   0.5 4.0  Blur radius multiplier
PARAM_FLOAT half_weight  3 0.6   0.0 1.0  Half-res pyramid contribution
PARAM_VEC4  tint         4 1.0 1.0 1.0 1.0  Glow color tint
```

### Live tuning from the console

Once an effect is active, every declared parameter is a runtime config item:

```
MVP_INTENSITY 2.5
MVP_TINT 1.0 0.7 1.0 1.0
MVP_BANDS 5
```

Changes are picked up by the next rendered frame.

---

## 6. Parameter persistence

Tuned values survive across sessions when a moviepack is active:

- **Where**: `moviepack_<name>.cfg`, saved alongside `user.cfg` in the user data dir. The filename is derived from the moviepack's display name (lowercase, spaces→underscores).
- **When saved**: on moviepack deactivation (explicit switch, or game exit while active).
- **When loaded**: on moviepack activation, after the effect has been loaded and its `MVP_*` settings registered.
- **Never in `user.cfg`**: the engine uses `tSettingItem` for these, which forbids writes to `user.cfg`. Switching moviepacks cleanly removes the `MVP_*` entries from the settings registry, so you won't see "unknown variable" spam if you later launch without the moviepack.

---

## 7. The uber shader hook system

This section is only relevant if your effect needs to control which parts of the scene glow (or otherwise modify per-component rendering). Skip it if your effect just reads `SCENE_COLOR` / `SCENE_DEPTH`.

### Concept

The system uber fragment shader (`shaders/uber.frag`) calls small per-component **hook functions** defined in `uber_hooks.glsl`. Moviepacks can override `uber_hooks.glsl` to change those behaviours without touching `uber.frag` itself. This keeps moviepacks forward-compatible with engine updates that change the main shader body.

**Compilation flow:**

1. Moviepack activates.
2. The engine looks for `moviepack/shaders/uber_hooks.glsl`. If present, it's used; otherwise, the system version.
3. `uber.frag` is compiled at load time with the resolved include path. The `#include "uber_hooks.glsl"` at the top of `uber.frag` pulls in the overriding version first.

### Two kinds of hooks

The system `uber_hooks.glsl` defines two parallel sets of hook functions — one for **color** (what gets displayed) and one for **emissive** (what goes into `SCENE_EMISSIVE` for the bloom pass to pick up).

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

Each hook receives the shaded color and returns a potentially modified color. The default implementations are passthrough — return `color`.

#### Emissive hooks — control what glows

```glsl
vec3 hookCycleEmissive    (vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal);
vec3 hookCycleWallEmissive(vec4 color, vec2 texCoord, vec3 modelPos);
vec3 hookZoneEmissive     (vec4 color, vec2 texCoord, vec3 modelPos);
```

Return the per-pixel RGB glow contribution. Returning `vec3(0.0)` means "this pixel doesn't glow". The returned color is written (along with a computed alpha = max channel) to `SCENE_EMISSIVE`, which bloom reads as input.

Default implementations give cycles a full-intensity glow, cycle walls 80%, and zones 60% (since zones are alpha-blended and otherwise read as overbright).

**Only three components have emissive hooks.** Sky, floor, rim walls, and effects don't glow by default — override the color hook itself if you want them to contribute to bloom (return a brighter color that your color hook passes through to the color attachment).

### Globals available inside hooks

```glsl
uTime              // float, seconds (same as push constant)
uRenderContext     // int, rRenderContext enum value (for debug/switches)
uArenaBBox         // vec4, (minX, minY, maxX, maxY) world coords
lighting.arenaBBox // vec4, same as uArenaBBox (UBO version)
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

- Declare a `PARAM_FLOAT glow_boost ...` in your PP effect's `.meta` file.
- Read it via `FP(slot)` from both the bloom fragment shader AND your custom `uber_hooks.glsl` (include `postprocess_params.glsl` in the hook file too).
- Tuning `MVP_GLOW_BOOST` affects both scene shading and the bloom pass simultaneously.

### Minimum hooks-only moviepack

A moviepack can ship only a custom `uber_hooks.glsl` and rely on everything else from the system:

```
moviepack.aamvp/
    shaders/
        uber_hooks.glsl   # your custom hooks
    settings.cfg          # POST_PROCESS_ENABLED, POST_PROCESS_EFFECT, etc.
```

No `.spv` files, no `uber.frag`, no textures. The engine will compile `uber.frag` from the system shaders dir with your `uber_hooks.glsl` overriding the include at compile time via libshaderc.

---

## 8. Packaging as a moviepack

A `.aamvp.zip` moviepack with post-processing looks like this:

```
customBloom.aamvp.zip
├── settings.cfg
├── textures/                    # (standard moviepack textures, optional)
│   └── ...
├── models/                      # (standard moviepack models, optional)
│   └── ...
└── shaders/
    ├── uber_hooks.glsl          # (optional — override scene hooks)
    └── postprocess/
        └── <effect_name>/
            ├── <effect_name>.pipeline
            ├── <effect_name>.meta
            ├── <effect_name>.frag
            └── ...
```

`settings.cfg` enables your effect:

```
# customBloom.aamvp — Tron glow
POST_PROCESS_ENABLED 1
POST_PROCESS_EFFECT bloom
MVP_INTENSITY 2.0
MVP_TINT 0.8 0.9 1.0 1.0
```

All config items in `settings.cfg` are applied with owner-level elevation when the moviepack activates, so you can set Administrator-level keys here that users can't set from the console.

---

## 9. Tutorial — write a grayscale effect from scratch

Let's build the smallest non-trivial effect: a grayscale pass with a tunable mix amount.

### 9.1 Layout

```
shaders/postprocess/grayscale/
    grayscale.frag
    grayscale.meta
```

No `.pipeline` file — we're single-pass so the engine will synthesize the default.

### 9.2 `grayscale.frag`

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 2) uniform sampler2D uUnused2;
layout(set = 0, binding = 3) uniform sampler2D uUnused3;

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Parameter slots — keep in sync with grayscale.meta.
#define P_AMOUNT FP(0)

void main()
{
    vec4 c = texture(uSceneColor, vTexCoord);
    // Rec.709 luma weights
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 gray = vec3(luma);
    fragColor = vec4(mix(c.rgb, gray, P_AMOUNT), c.a);
}
```

### 9.3 `grayscale.meta`

```
NAME Grayscale
DESCRIPTION Desaturates the scene by a tunable amount.

PARAM_FLOAT amount 0 1.0 0.0 1.0 Desaturation strength (0 = color, 1 = full gray)
```

### 9.4 Enable it

Either edit `user.cfg` isn't possible (admin-only keys), so use a console line at runtime:

```
POST_PROCESS_ENABLED 1
POST_PROCESS_EFFECT grayscale
MVP_AMOUNT 0.7
```

Or ship it in a moviepack (§8) that declares the same three lines in `settings.cfg`.

### 9.5 Iterate

Every time you reload the moviepack (or call `POST_PROCESS_EFFECT grayscale` again), the effect re-compiles from disk. Shader errors are printed to stderr with line numbers — watch the terminal while iterating.

---

## 10. Troubleshooting

**Nothing happens when I enable my effect.**
Check that the directory name under `shaders/postprocess/` exactly matches the string you set `POST_PROCESS_EFFECT` to. Case-sensitive.

**"Cannot find shaders/postprocess/fullscreen.vert"**
The system shaders directory isn't on the search path. Make sure you didn't delete or rename the system `shaders/postprocess/` directory, and that your moviepack didn't shadow it with an empty one.

**Shader compile errors on load.**
libshaderc prints them to stderr with `path:line:col: message`. Common ones:
- Missing `#version 450` at the top.
- Forgetting to `#include "postprocess_params.glsl"` when you use `FP(...)` / `IP(...)`.
- Declaring fewer than 4 samplers in `set = 0` — the layout has 4 bindings and unused slots still need a dummy uniform.

**`MVP_*` console commands say "unknown variable" after switching moviepacks.**
Expected — those settings exist only while the effect is loaded. If you want the value back, re-activate the moviepack or re-type the `POST_PROCESS_EFFECT` line.

**My vec4 parameter values are wrong / packed into the wrong channels.**
Check your `.meta` slot — `PARAM_VEC4` must be aligned to 4 (slots 0, 4, 8, 12, ...). Reading via `FP4(slot)` with an unaligned slot gives garbage.

**The emissive attachment looks empty (bloom has nothing to glow).**
Your moviepack's `uber_hooks.glsl` is probably overriding the emissive hooks to `return vec3(0.0)`. Either omit emissive overrides (inherit the defaults) or emit non-zero for the components you want to glow.

**Vulkan validation warnings about "location = 1" writes.**
Should not happen in current builds — the engine compiles two variants of `uber.frag` (with and without `USE_EMISSIVE_OUT`) and picks the right one per render pass. If you see this, you're on an old build; rebuild from trunk.

---

## 11. Reference files

Start by reading these in order:

1. `shaders/postprocess/passthrough/passthrough.frag` — minimum viable fragment shader.
2. `shaders/postprocess/celshading/` — single-pass effect with samplers and `.meta` parameters.
3. `shaders/postprocess/bloom/` — multi-pass framegraph with intermediate resources.
4. `shaders/uber_hooks.glsl` — default hook implementations, documented contract.
5. `shaders/postprocess_params.glsl` — the UBO layout and `FP`/`IP` macros.
6. `moviepacks/customBloom.aamvp.zip` and `moviepacks/customCelShading.aamvp.zip` — complete moviepack examples you can unzip and modify.
