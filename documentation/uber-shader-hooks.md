# Uber Shader Hooks for Moviepack Authors

This guide documents the uber shader hook mechanism — a stable extension point that lets moviepacks customize per-component scene shading without forking the main fragment shader.

Hooks are useful on their own (change how cycles look, tint the floor, make zones shimmer) and also feed the post-process system by writing to the emissive attachment that bloom reads. If you haven't already, skim `documentation/postprocess-authoring.md` — §7 of that doc summarizes hooks from the post-process angle; this document is the deep dive.

---

## 1. What hooks are and why they exist

The Vulkan renderer uses a single "uber" fragment shader (`shaders/uber.frag`) for all in-game 3D geometry — floor, walls, cycles, zones, the lot. It dispatches to the correct code path based on a per-draw render context ID.

Rather than expecting moviepack authors to fork and maintain their own `uber.frag` (which would need to be kept in sync every time the engine's shader changes), the uber shader delegates the per-component "look" decisions to a small set of **hook functions** defined in a separate file, `shaders/uber_hooks.glsl`:

```
uber.frag                               uber_hooks.glsl
─────────────                           ────────────────
computes base color                ─▶   hookFloor() modifies it
                                        hookCycle() does lighting tweaks
                                        hookZoneEmissive() drives bloom
                                        ...
writes to color / emissive outputs
```

A moviepack that ships its own `uber_hooks.glsl` overrides these functions. The engine compiles the **system** `uber.frag` against the **moviepack's** hooks file at activation time, via libshaderc. As long as the hook signatures don't change, the moviepack stays forward-compatible with engine shader updates.

### What hooks don't do

- Hooks can't add new vertex attributes, new samplers, or new UBOs. Those would require matching C++ changes to the pipeline layout.
- Hooks can't change which passes run. The scene is still drawn once with the uber shader; bloom still runs as a separate post-process. What hooks *can* do is control what goes into the inputs those stages read.
- Hooks can't opt out of the render context dispatch. Every 3D fragment goes through exactly one color hook (based on its context) and at most one emissive hook.

---

## 2. Compilation flow

Every time a moviepack is activated (including at startup with the default moviepack), the renderer's `ReloadShaders` path runs:

```
1. Build the include search path, in order:
   [0] moviepack/shaders/           (only if moviepack ships uber_hooks.glsl)
   [1] shaders/                     (system shaders dir — always present)

2. Resolve uber.vert and uber.frag:
   - Try moviepack/shaders/uber.vert first
   - Fall back to shaders/uber.vert

3. Compile with libshaderc:
   - Include resolver walks the search path for #include "..." lines
   - #include "uber_hooks.glsl" inside uber.frag picks up the moviepack
     version if one exists, else the system version
   - The fragment shader is compiled TWICE: once without USE_EMISSIVE_OUT
     (used for render passes with 1 color attachment, e.g. the swapchain
     pass) and once with (used for the 2-attachment post-process scene pass)

4. Pipeline manager swaps in the new shader modules
```

Consequences:

- **No build toolchain needed.** You ship only `.glsl` source; the engine compiles it at load time. A hooks-only moviepack can literally be three text files in a zip.
- **Compilation errors are survivable.** If your hooks file fails to compile, the engine logs the error to stderr and keeps the previous shaders active — the game doesn't crash, it just doesn't apply your changes. Watch the terminal while iterating.
- **Hot-reload on moviepack switch.** Switching to a different moviepack (or reactivating the same one) triggers a full shader reload. You don't have to restart the game to try a change.

---

## 3. The hook interface

The contract is defined in `shaders/uber_hooks.glsl`. Every hook function has a **stable signature** — moviepack authors can rely on it staying unchanged across engine versions. The system's default implementations are passthrough: they return the input unchanged, so a moviepack that omits a hook sees the stock renderer behavior.

There are two parallel families of hooks:

- **Color hooks** (7 functions) — modify the final displayed color for a component.
- **Emissive hooks** (3 functions) — return how much a fragment contributes to the bloom input.

### 3.1 Color hooks

```glsl
vec4 hookSky      (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookFloor    (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookRimWall  (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookCycleWall(vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookCycle    (vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal);
vec4 hookZone     (vec4 color, vec2 texCoord, vec3 modelPos);
vec4 hookEffects  (vec4 color, vec2 texCoord, vec3 modelPos);
```

**When they run.** After `uber.frag` has computed the base color (texture sample + lighting for lit meshes, plain `vColor * texColor` for unlit), the render context switch dispatches to exactly one of these hooks:

| Context | Hook | Description |
|---|---|---|
| `Game3D_Sky` (4) | `hookSky` | Sky / background fragments |
| `Game3D_Floor` (5) | `hookFloor` | Arena floor |
| `Game3D_RimWalls` (6) | `hookRimWall` | Arena rim walls |
| `Game3D_PlayerWalls` (7) | `hookCycleWall` | Cycle trails / walls |
| `Game3D_Cycles` (8) | `hookCycle` | Cycle bodies (lit, with normals) |
| `Game3D_Zones` (9) | `hookZone` | Win zones, death zones, fortress zones |
| `Game3D_Effects` (10) | `hookEffects` | Sparks, explosions, particles |

(HUD, menu, title-screen, and font fragments are NOT routed through hooks — they go through a parallel code path in `uber.frag`.)

**Inputs.**

- `color` — the base color `uber.frag` computed before the hook ran. For lit geometry this already includes texture + diffuse + specular. For unlit it's `vColor * texColor`.
- `texCoord` — the `vTexCoord` the fragment was drawn with. Not always the same coordinate space across components — floor uses world-scaled coordinates, cycles use model-local, walls are 1D parameterized along their length.
- `modelPos` — the **model-space** position of the fragment as output by the vertex shader. For non-transformed geometry (floor, rim walls, player walls, zones) this is effectively world space. For transformed geometry (cycles) it's local to the cycle. Use `uArenaBBox` to normalize floor/wall positions into a 0..1 range.
- `normal` (cycle only) — the **view-space** surface normal, already normalized. Use it for additional lighting, rim light, or depth-based edge detection tricks.

**Output.** Return a `vec4` — the new color. Alpha is respected (it flows into the color attachment blend).

### 3.2 Emissive hooks

```glsl
vec3 hookCycleEmissive    (vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal);
vec3 hookCycleWallEmissive(vec4 color, vec2 texCoord, vec3 modelPos);
vec3 hookZoneEmissive     (vec4 color, vec2 texCoord, vec3 modelPos);
```

**When they run.** Immediately after the color hook, but only when the fragment shader is compiled in the `USE_EMISSIVE_OUT` variant — i.e. when drawing to the post-process offscreen pass. In the plain variant (swapchain pass, post-process disabled) these hooks still exist in the source, but the emissive write is compiled out entirely and the alpha of the emissive attachment is never produced.

Emissive hooks only exist for **three components**: `Game3D_Cycles`, `Game3D_PlayerWalls`, `Game3D_Zones`. Everything else writes `vec4(0.0)` to the emissive attachment by default and doesn't contribute to bloom.

**Inputs.** Same as the color hooks, plus `color` is the color as it looked **after** the color hook ran. This means if your `hookCycle` brightens the cycle body, your `hookCycleEmissive` receives the already-brightened color.

**Output.** Return a `vec3` — the raw emissive RGB. The shader wraps this into `vec4(em, clamp(max(em.r, em.g, em.b), 0.0, 1.0))` before writing to `SCENE_EMISSIVE`, so the alpha channel automatically encodes "how much this pixel glows" (max of the three components).

**Default implementations** (cause: all three components glow with the stock moviepack):

| Hook | Default | Why |
|---|---|---|
| `hookCycleEmissive` | `return color.rgb;` | Full cycle color — strongest glow |
| `hookCycleWallEmissive` | `return color.rgb * 0.8;` | 80% — walls are everywhere, needs to be slightly dimmer than cycles to not overwhelm |
| `hookZoneEmissive` | `return color.rgb * 0.6;` | 60% — zones are already alpha-blended, full intensity reads as overbright |

Override any of them (or replace with `return vec3(0.0);` to kill that component's bloom contribution entirely).

---

## 4. Available globals inside hooks

`uber_hooks.glsl` is `#include`d into `uber.frag` after the uniforms are declared, so these globals are visible from hook code:

| Name | Type | Meaning |
|---|---|---|
| `uTime` | `float` | Seconds since the engine started (frame-synchronous). Good for animation; not stable across save-states. |
| `uArenaBBox` | `vec4` | Arena bounding box `(minX, minY, maxX, maxY)` in world units. Use it to normalize world positions. |
| `uRenderContext` | `int` | Current render context enum value (see §3.1). Mostly useful for asserts / debugging; the dispatch switch already picks the right hook for you. |
| `lighting.arenaBBox` | `vec4` | Same as `uArenaBBox`, read directly from the lighting UBO. Either form works. |

Under the hood these are `#define`s that unpack values from push constants and the lighting UBO:

```glsl
#define uTime          (pc.uTexMatrix[2][0])
#define uRenderContext (int(pc.uTexMatrix[2][1]))
#define uArenaBBox     (lighting.arenaBBox)
```

You can also use `uMVP`, `uTexMatrix`, and `uNormalMatrix` from the push constants and everything in the `lighting` UBO — they're in scope — but treat that as lower-stability API than the `u*`-prefixed macros.

### Using `MVP_*` parameters in hooks

Hook fragments and post-process fragments share the same `PostProcessParams` UBO. To read a tunable parameter from a hook, just `#include "postprocess_params.glsl"` at the top of your `uber_hooks.glsl`:

```glsl
#include "postprocess_params.glsl"

#define P_FLOOR_TINT FP4(4)

vec4 hookFloor(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return vec4(color.rgb * P_FLOOR_TINT.rgb, color.a);
}
```

The parameter slot must be declared in your post-process effect's `.lua` script (same slot-numbering rules as post-process shaders — see §4 of `postprocess-authoring.md`). This is the idiomatic way to expose tunable knobs that affect both scene shading and bloom simultaneously: set `MVP_<EFFECT>_FLOOR_TINT` once and it drives both ends.

If your moviepack ships hooks but no post-process effect, you can still use `FP()`/`IP()`/`FP4()` — the UBO is always bound. But without a `.lua` script to declare defaults you'll be reading uninitialized memory, so the only portable thing to do is not read from params in that case.

---

## 5. Packaging a hooks-only moviepack

The simplest moviepack that uses hooks ships just two files:

```
mymoviepack.aamvp.zip
├── settings.cfg
└── shaders/
    └── uber_hooks.glsl
```

No textures, no models, no `.spv` files, no `uber.frag`. The engine:

1. Locates `moviepack/shaders/uber_hooks.glsl` when resolving includes.
2. Compiles the **system** `shaders/uber.frag` against your hooks.
3. Swaps the compiled result into the pipeline manager.

That's it. Your custom hooks run for every 3D fragment.

You don't need any post-process configuration — hooks work whether `POST_PROCESS_ENABLED` is 0 or 1. The only difference is whether the emissive hooks are compiled in (PP on) or compiled out (PP off). Hooks-only moviepacks typically leave post-processing alone and focus on color hooks.

`settings.cfg` can still be used to lock in MVP values or toggle post-processing if your hooks are designed to work with bloom — see `moviepacks/customCelShading.aamvp.zip` for an example that does both.

---

## 6. Tutorial — a "neon floor" hook from scratch

Let's write a small moviepack that makes the arena floor glow neon pink, with a tunable pulse rate, without touching post-processing.

### 6.1 Files

```
neonfloor/
├── settings.cfg
└── shaders/
    └── uber_hooks.glsl
```

### 6.2 `shaders/uber_hooks.glsl`

Start by copying the stock `shaders/uber_hooks.glsl` from the source tree — it has every hook defined as passthrough, which is the correct baseline. Then modify `hookFloor`:

```glsl
// (all the other hooks unchanged — copy them verbatim from the system file)

vec4 hookFloor(vec4 color, vec2 texCoord, vec3 modelPos)
{
    // Normalize floor world position into 0..1 across the arena.
    vec2 arenaSize = uArenaBBox.zw - uArenaBBox.xy;
    vec2 uv = (modelPos.xy - uArenaBBox.xy) / arenaSize;

    // Pulse amplitude — half-second sine wave between 0.3 and 1.0
    float pulse = 0.65 + 0.35 * sin(uTime * 6.28);

    // Neon pink tint, stronger near arena center
    float centerBias = 1.0 - length(uv - vec2(0.5)) * 1.5;
    centerBias = clamp(centerBias, 0.0, 1.0);

    vec3 neon = vec3(1.0, 0.2, 0.8);
    vec3 tinted = mix(color.rgb, neon, centerBias * pulse * 0.6);

    return vec4(tinted, color.a);
}
```

Note how the hook uses three of the standard globals: `uArenaBBox` to size the floor, `uTime` to drive the pulse, and `modelPos` to compute per-pixel distance from arena center.

### 6.3 `settings.cfg`

Just a comment — nothing to configure. The moviepack does its thing purely through the hooks file.

```
# neonfloor.aamvp — pulsing neon pink arena floor
```

### 6.4 Package and test

Zip the two files into `neonfloor.aamvp.zip`, drop it in `moviepacks/`, and activate from the in-game moviepack menu. The first activation will show shader compile messages on stderr — any syntax errors in your hook code are flagged there with line numbers.

### 6.5 Iterate

- Edit `shaders/uber_hooks.glsl`.
- Re-zip the pack (or edit in place and re-activate — the moviepack extractor re-extracts on each activation).
- In-game, switch away from the pack and back to trigger a shader reload. Changes are live within a second or two.

If you want to expose `speed` and `intensity` as tunable parameters, add a `shaders/postprocess/neon/neon.lua` script declaring `effect:param_float("speed", 0, 6.28, 0.0, 30.0, "pulse rate")` and read `FP(0)` in your hook. The MVP registry will pick it up automatically when the moviepack loads.

---

## 7. Reference: complete system hooks file

`shaders/uber_hooks.glsl` is the authoritative starting point. Always copy it whole, even if you only change one hook — missing a hook function entirely is a compile error, not a fallback to the default.

```glsl
// Color hooks
vec4 hookSky      (vec4 c, vec2 tc, vec3 mp)              { return c; }
vec4 hookFloor    (vec4 c, vec2 tc, vec3 mp)              { return c; }
vec4 hookRimWall  (vec4 c, vec2 tc, vec3 mp)              { return c; }
vec4 hookCycleWall(vec4 c, vec2 tc, vec3 mp)              { return c; }
vec4 hookCycle    (vec4 c, vec2 tc, vec3 mp, vec3 n)      { return c; }
vec4 hookZone     (vec4 c, vec2 tc, vec3 mp)              { return c; }
vec4 hookEffects  (vec4 c, vec2 tc, vec3 mp)              { return c; }

// Emissive hooks — drive bloom
vec3 hookCycleEmissive    (vec4 c, vec2 tc, vec3 mp, vec3 n) { return c.rgb;       }
vec3 hookCycleWallEmissive(vec4 c, vec2 tc, vec3 mp)         { return c.rgb * 0.8; }
vec3 hookZoneEmissive     (vec4 c, vec2 tc, vec3 mp)         { return c.rgb * 0.6; }
```

---

## 8. Troubleshooting

**"uber_hooks.glsl" not found during compile.**
You probably shipped your hooks file at the wrong path inside the zip. It must be at `shaders/uber_hooks.glsl` relative to the moviepack root — *not* `moviepack/shaders/uber_hooks.glsl` (the "moviepack/" prefix is added by the extractor, not you).

**"Undefined function hookFloor" (or any other hook) at compile time.**
Your custom `uber_hooks.glsl` is missing that hook function entirely. Hooks must all be defined — even if you only care about one, copy the others as passthrough. Use §7 above as a checklist.

**Hooks compile but the scene looks stock.**
Either the moviepack isn't actually active (check the moviepack menu), or your include path isn't finding the moviepack version. Watch stderr at moviepack activation — there's a `[Vulkan] ReloadShaders: includePaths = [...]` line that should show `moviepack/shaders` before `shaders`.

**Scene looks right, but bloom has nothing to glow.**
Your hooks file is overriding the emissive hooks with `return vec3(0.0)`. Either copy the default implementations verbatim (see §3.2) or adjust the multipliers. Alternatively, post-processing might be disabled — check `POST_PROCESS_ENABLED`.

**`uTime` jumps when pausing or during replay.**
`uTime` is tied to the real render clock, not game time. Don't use it for physics-coupled effects. For game-time coupling, you'd need a new global — file an issue.

**Pipeline validation warnings about location=1.**
Should not happen in current builds — the engine compiles a separate variant without the emissive output for the swapchain pass. If you see it, rebuild from trunk.

**Hook changes don't take effect after editing.**
Did you re-activate the moviepack after editing? Hot reload only fires on moviepack (re)activation or an explicit `/shaders` console reload (if supported). In-place edits without a reload are invisible.

---

## 9. Related documentation

- `documentation/postprocess-authoring.md` — The post-processing pipeline. Read §4 (Lua render graph + parameters), §7 (hook ↔ PP interaction), and §8 (packaging) for topics that overlap with this document.
- `shaders/uber_hooks.glsl` — The authoritative hook interface. Whatever is in this file is what the engine compiles against if no moviepack override exists.
- `shaders/uber.frag` — The main fragment shader. You don't override this, but reading it helps you understand exactly where and how hooks are called.
- `shaders/postprocess_params.glsl` — The shared `MVP_*` UBO declaration and `FP()`/`IP()`/`FP4()` macros. Include this in your hooks if you want tunable parameters.
