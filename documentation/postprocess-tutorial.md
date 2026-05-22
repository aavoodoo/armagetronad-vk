# Post-Process Shader Tutorial

Step-by-step guide to writing post-process effects for moviepacks, from the
simplest possible effect to a multi-pass bloom.

All complete, working copies of the files shown here are in
`documentation/postprocess-examples/`.

---

## How effects load

An effect is a folder under `shaders/postprocess/` (or inside a moviepack zip).
The folder name is the effect name. Two files are needed at minimum:

```
myeffect/
    myeffect.lua    ← render graph declaration
    myeffect.frag   ← GLSL fragment shader(s)
```

The renderer auto-loads an effect when a moviepack named `myeffect` is activated, by probing for `shaders/postprocess/myeffect/myeffect.lua` inside the pack. The pack's name and the effect script's directory name must match. Deactivating the pack turns PP off again.

The `myeffect.lua` script runs first. The global `effect` (an `EffectBuilder`
object) is pre-injected by the renderer. The script uses it to declare which
shader runs in each pass, what textures each pass reads, and which tunable
parameters to expose.

Built-in texture sources available to any effect:

| Name | Contents |
|------|----------|
| `"SCENE_COLOR"` | Full-res RGBA8 scene render |
| `"SCENE_EMISSIVE"` | Full-res RGBA8 emissive/glow layer |
| `"SCENE_DEPTH"` | Full-res D32 depth buffer |
| `"SWAPCHAIN"` | Final output — always the target of the last pass |

---

## Example 1 — Passthrough

The simplest possible effect: copy the scene to the screen untouched.

### `passthrough.lua`

```lua
-- Single pass: copies scene color to the swapchain.
effect:pass("passthrough", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
```

`effect:pass(shader, target)` declares one full-screen pass. The first argument
is both the shader name (which `.frag` file to compile) and the display name
used in logs. The second is where the output goes — `"SWAPCHAIN"` writes
directly to the screen.

`effect:sampler(binding, source)` binds a texture to the most recently declared
pass. Binding 0 maps to `layout(binding = 0)` in the GLSL.

### `passthrough.frag`

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(uSceneColor, vTexCoord);
    // Sample depth at 1/1024 weight so Metal/MoltenVK preserves full
    // D32_SFLOAT precision (prevents z-fighting on macOS Apple Silicon).
    float depth = texture(uSceneDepth, vTexCoord).r;
    fragColor.a = fragColor.a * (1.0 - 1.0/1024.0) + depth * (1.0/1024.0);
}
```

A few things to note about the GLSL template:

- `binding = 1` (`uSceneDepth`) is always declared, even if you don't use it.
  The renderer always binds scene depth to slot 1. Declaring it avoids a
  validation layer warning.
- `postprocess_params.glsl` defines the `FP(slot)`, `IP(slot)`, `FP4(slot)`
  macros used to read tunable parameters. Include it even when you have no
  parameters — it's a harmless header.
- The push constant block (`uResolution`, `uTime`, `uArenaBBox`) is available
  in every fragment shader for free.
- `vTexCoord` is the UV coordinate across the screen: `(0,0)` top-left,
  `(1,1)` bottom-right.

### Activate it

`passthrough` is the built-in identity effect — it ships with the base game and is loaded automatically as a no-op default. You don't need to do anything to use it; any moviepack that doesn't ship its own PP effect runs through passthrough on macOS, or with PP disabled on other platforms.

---

## Example 2 — Depthviz

A single-pass diagnostic overlay that draws the depth buffer as a grayscale
thumbnail in the corner. Introduces:

- Reading `SCENE_DEPTH`
- Declaring tunable parameters with `effect:param_*`
- Using `FP()` / `FP4()` macros to read parameters in the shader
- The `uResolution` push constant

### `depthviz.lua`

```lua
-- Single pass: reads scene color and depth, draws a depth thumbnail.
effect:pass("depthviz", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
effect:sampler(1, "SCENE_DEPTH")

-- Tunable parameters (exposed as MVP_DEPTHVIZ_* config items while loaded)
effect:param_float("size",        0, 0.25, 0.05, 0.9,
    "Thumbnail width as a fraction of the screen")
effect:param_float("rawMin",      1, 0.5,  0.0,  1.0,
    "Raw depth value mapped to WHITE (nearest)")
effect:param_float("rawMax",      2, 0.99, 0.0,  1.0,
    "Raw depth value mapped to BLACK (farthest)")
effect:param_float("border",      3, 2.0,  0.0,  10.0,
    "Border thickness in pixels")
effect:param_vec4 ("borderColor", 4, 1.0, 1.0, 0.0, 1.0,
    "Color of the rectangle outline (RGBA)")
effect:param_float("gamma",       5, 1.0,  0.1,  10.0,
    "Post-gamma curve. 1 = linear; >1 brightens midtones")
```

`effect:param_float(name, slot, default, min, max, description)` declares a
float parameter. The renderer:

1. Registers a config item named `MVP_<EFFECTNAME>_<NAME>` (e.g.
   `MVP_DEPTHVIZ_SIZE`) so the parameter can be tweaked from a Lua script or
   config file while the effect is loaded.
2. Packs the value into a std140 UBO that the shader reads via the macros.

`slot` is the index used in the shader with `FP(slot)`. Slots must be unique
within an effect.

`effect:param_vec4(name, slot, r, g, b, a, description)` is the same but
declares a `vec4`. Read it in the shader with `FP4(slot)`.

### `depthviz.frag` (key excerpt)

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Slot aliases — must match depthviz.lua
#define P_SIZE         FP(0)
#define P_RAW_MIN      FP(1)
#define P_RAW_MAX      FP(2)
#define P_BORDER_PX    FP(3)
#define P_BORDER_COLOR FP4(4)
#define P_GAMMA        FP(5)

void main()
{
    vec4 scene = texture(uSceneColor, vTexCoord);

    // Thumbnail rectangle in the top-right corner
    float size   = clamp(P_SIZE, 0.05, 0.9);
    float aspect = pc.uResolution.x / max(pc.uResolution.y, 1.0);
    vec2  rectSize = vec2(size, size * aspect);
    vec2  rectMin  = vec2(1.0 - rectSize.x, 0.0);
    vec2  rectMax  = vec2(1.0, rectSize.y);

    if (vTexCoord.x < rectMin.x || vTexCoord.y > rectMax.y) {
        fragColor = scene;
        return;
    }

    // Remap to full-screen UV and sample raw depth
    vec2  localUV = (vTexCoord - rectMin) / rectSize;
    float rawDepth = texture(uSceneDepth, localUV).r;

    // Contrast window: stretch [rawMin, rawMax] to full grey range
    float rawMin = clamp(P_RAW_MIN, 0.0, 0.9999);
    float rawMax = clamp(P_RAW_MAX, rawMin + 0.0001, 1.0);
    float t    = clamp((rawDepth - rawMin) / (rawMax - rawMin), 0.0, 1.0);
    float grey = pow(1.0 - t, 1.0 / max(P_GAMMA, 0.001));

    // Border
    float borderPx = max(P_BORDER_PX, 0.0);
    vec2  texel = 1.0 / max(pc.uResolution, vec2(1.0));
    float edgeDist = min(
        min((vTexCoord.x - rectMin.x) / texel.x, (rectMax.x - vTexCoord.x) / texel.x),
        min((vTexCoord.y - rectMin.y) / texel.y, (rectMax.y - vTexCoord.y) / texel.y));

    fragColor = (edgeDist < borderPx)
        ? P_BORDER_COLOR
        : vec4(grey, grey, grey, 1.0);
}
```

### Tweaking parameters at runtime

While the effect is active, all declared parameters are live config items.
From a Lua script:

```lua
aa_config_set("MVP_DEPTHVIZ_SIZE", "0.4")
aa_config_set("MVP_DEPTHVIZ_GAMMA", "2.0")
```

Or from the in-game console (owner level required):

```
MVP_DEPTHVIZ_SIZE 0.4
```

---

## Example 3 — Bloom

A multi-pass Gaussian bloom that glows the emissive layer over the scene.
Introduces:

- `effect:resource()` — intermediate render targets
- Multiple passes chained together
- Reusing a shader across passes with different inputs
- Reading `SCENE_EMISSIVE`

### Overview

Bloom needs several off-screen buffers to implement a Gaussian blur pyramid:

```
SCENE_EMISSIVE (full)
  → bloom_half (½ res)     ← extract pass: bright-pass + 2x downsample
  → bloom_half_tmp (½ res) ← horizontal blur
  → bloom_half (½ res)     ← vertical blur   (reuses bloom_half)
  → bloom_quarter (¼ res)  ← downsample again
  → bloom_quarter_tmp      ← horizontal blur
  → bloom_quarter          ← vertical blur
  + SCENE_COLOR
  → SWAPCHAIN              ← composite: additively blend both levels
```

Two pyramid levels gives smooth, wide glow without a huge blur radius.

### `bloom.lua`

```lua
-- Declare intermediate render targets
effect:resource("bloom_half",        "RGBA8", 0.5)   -- ½ resolution
effect:resource("bloom_half_tmp",    "RGBA8", 0.5)
effect:resource("bloom_quarter",     "RGBA8", 0.25)  -- ¼ resolution
effect:resource("bloom_quarter_tmp", "RGBA8", 0.25)

-- Level 1: extract bright pixels into half-res
effect:pass("bloom_extract", "bloom_half")
effect:sampler(0, "SCENE_EMISSIVE")

-- Level 1 separable Gaussian (horizontal then vertical)
effect:pass("bloom_blur_h", "bloom_half_tmp")
effect:sampler(0, "bloom_half")

effect:pass("bloom_blur_v", "bloom_half")
effect:sampler(0, "bloom_half_tmp")

-- Level 2: downsample the blurred half to quarter-res
effect:pass("bloom_downsample", "bloom_quarter")
effect:sampler(0, "bloom_half")

-- Level 2 separable Gaussian
effect:pass("bloom_blur_h", "bloom_quarter_tmp")
effect:sampler(0, "bloom_quarter")

effect:pass("bloom_blur_v", "bloom_quarter")
effect:sampler(0, "bloom_quarter_tmp")

-- Final composite: additively blend both pyramid levels over scene color
effect:pass("bloom_composite", "SWAPCHAIN")
effect:sampler(0, "SCENE_COLOR")
effect:sampler(1, "bloom_half")
effect:sampler(2, "bloom_quarter")

-- Tunable parameters
effect:param_float("intensity",   0, 1.5, 0.0, 5.0,  "Final composite strength")
effect:param_float("threshold",   1, 0.05, 0.0, 1.0, "Emissive alpha cutoff")
effect:param_float("radius",      2, 1.5, 0.5, 4.0,  "Blur radius multiplier")
effect:param_float("half_weight", 3, 0.6, 0.0, 1.0,  "Half-res level contribution")
effect:param_vec4 ("tint",        4, 1.0, 1.0, 1.0, 1.0, "Glow color tint (RGB)")
```

Key points:

- `effect:resource(name, format, scale)` allocates an off-screen render target.
  `scale` is relative to the swapchain resolution (0.5 = half in each dimension).
  Once declared, the name can be used as both a `sampler` source and a `pass`
  target.
- A shader name (`"bloom_blur_h"`) can appear in multiple `effect:pass()` calls.
  Each call is a separate draw but uses the same compiled shader. The sampler
  bindings are set per-pass.
- Passes execute in declaration order.

### `bloom_extract.frag` (key excerpt)

```glsl
layout(set = 0, binding = 0) uniform sampler2D uSceneEmissive;

#include "postprocess_params.glsl"

#define P_INTENSITY FP(0)
#define P_THRESHOLD FP(1)

void main()
{
    // 4-tap box filter: averages a 2×2 source block per tap
    vec2 srcTexel = 0.5 / pc.uResolution;
    vec4 s0 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2(-1.0, -1.0));
    vec4 s1 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2( 1.0, -1.0));
    vec4 s2 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2(-1.0,  1.0));
    vec4 s3 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2( 1.0,  1.0));
    vec4 avg = (s0 + s1 + s2 + s3) * 0.25;

    // Gate on emissive alpha (encodes how much each pixel should glow)
    float gate = smoothstep(P_THRESHOLD, P_THRESHOLD + 0.05, avg.a);
    fragColor = vec4(avg.rgb * gate * P_INTENSITY, avg.a * gate);
}
```

The `SCENE_EMISSIVE` attachment is populated by the uber shader: cycles, cycle
walls, and zones write their glow contribution there automatically. The alpha
channel encodes the maximum RGB channel — a quick scalar measure of how bright
the glow is. Thresholding on alpha removes dim noise without per-channel
comparisons.

### `bloom_blur_h.frag` / `bloom_blur_v.frag` (key excerpt)

```glsl
layout(set = 0, binding = 0) uniform sampler2D uSrc;

#define P_RADIUS FP(2)   // shared slot with bloom.lua's "radius" param

void main()
{
    vec2 texel = 1.0 / pc.uResolution;
    float radius = max(P_RADIUS, 0.1);

    // 9-tap Gaussian, horizontal (blur_v swaps the offset direction)
    const float weights[5] = float[](0.2270, 0.1945, 0.1216, 0.0540, 0.0162);
    vec4 color = texture(uSrc, vTexCoord) * weights[0];
    for (int i = 1; i <= 4; ++i) {
        vec2 offset = vec2(texel.x * float(i) * radius, 0.0);
        color += texture(uSrc, vTexCoord + offset) * weights[i];
        color += texture(uSrc, vTexCoord - offset) * weights[i];
    }
    fragColor = color;
}
```

The same parameter slot (`FP(2)` = radius) is shared across all passes of the
same effect. Parameters are global to the effect, not per-pass.

### `bloom_composite.frag` (key excerpt)

```glsl
layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomHalf;
layout(set = 0, binding = 2) uniform sampler2D uBloomQuarter;

#define P_INTENSITY   FP(0)
#define P_HALF_WEIGHT FP(3)
#define P_TINT        FP4(4)

void main()
{
    vec4 scene    = texture(uSceneColor,   vTexCoord);
    vec4 half_    = texture(uBloomHalf,    vTexCoord);
    vec4 quarter  = texture(uBloomQuarter, vTexCoord);

    float hw  = clamp(P_HALF_WEIGHT, 0.0, 1.0);
    vec3  glow = (half_.rgb * hw + quarter.rgb * (1.0 - hw))
                 * P_INTENSITY * P_TINT.rgb;

    fragColor = vec4(scene.rgb + glow, scene.a);
}
```

Additive blending (adding `glow` directly to the scene RGB) is how the classic
Tron glow works — it brightens the screen without replacing the geometry color.

---

## Packaging in a moviepack

An effect folder can live anywhere the renderer searches:

1. **Packed inside the moviepack zip** at `shaders/postprocess/<packName>/`
2. **Installed alongside the game** at `<datadir>/shaders/postprocess/<packName>/`

The effect is auto-discovered by the moviepack manager: a pack provides PP iff it ships `shaders/postprocess/<packName>/<packName>.lua` (the pack's name and the script directory name must match). No cfg key is needed — activating the pack turns PP on; deactivating turns it off.

### Minimal moviepack layout

For a pack named `myeffect`:

```
myeffect.aamvp.zip
├── settings.cfg            ← (optional) MVP_* tunables only
└── shaders/
    └── postprocess/
        └── myeffect/
            ├── myeffect.lua
            └── myeffect.frag
```

---

## Quick reference

### Lua API

```lua
-- Render graph
effect:resource(name, format, scale)   -- "RGBA8" | "RGBA16F" | "R8"
effect:pass(shader, target)
effect:sampler(binding, source)        -- source = built-in or resource name

-- Parameters
effect:param_float(name, slot, default, min, max, description)
effect:param_int  (name, slot, default, min, max, description)
effect:param_vec4 (name, slot, r, g, b, a, description)
```

### GLSL macros (from `postprocess_params.glsl`)

```glsl
FP(slot)    // float parameter at slot
IP(slot)    // int parameter at slot
FP4(slot)   // vec4 parameter at slot
```

### Push constants (available in every fragment shader)

```glsl
layout(push_constant) uniform PushConstants {
    vec2  uResolution;  // render target size in pixels
    float uTime;        // game time in seconds
    float _pad;
    vec4  uArenaBBox;   // arena bounding box: (minX, minY, maxX, maxY)
} pc;
```

### Activate / deactivate

Both are implicit. An effect activates when the moviepack of the same name is selected and deactivates when the moviepack is changed or set to None.

### Tweak a parameter from Lua while the effect is active

```lua
-- Parameters are exposed as MVP_<EFFECTNAME>_<PARAMNAME> (uppercase)
aa_config_set("MVP_BLOOM_INTENSITY", "2.0")
aa_config_set("MVP_DEPTHVIZ_SIZE", "0.35")
```
