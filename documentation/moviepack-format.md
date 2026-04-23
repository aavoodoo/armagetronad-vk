# Moviepack Format (.aamvp.zip)

Moviepacks replace game textures, models, shaders, sounds, and configuration to give the game a completely different look and feel. This document describes the `.aamvp.zip` file format and how to create one.

For shader-specific authoring, see `documentation/uber-shader-hooks.md` and `documentation/postprocess-authoring.md`.

---

## 1. Overview

A moviepack is a ZIP archive with the extension `.aamvp.zip`. When activated, the engine:

1. Extracts the archive to a temporary `moviepack/` directory inside the user data path.
2. Validates that `settings.cfg` exists at the root of the extracted contents.
3. Applies `settings.cfg` with owner-level access elevation (so it can set admin-level keys).
4. Reloads all textures, models, sounds, fonts, and shaders — moviepack assets override system assets.
5. Notifies the post-process system to pick up any PP effects shipped in the pack.

On deactivation (switching to another pack or to "None"), the temporary directory is cleaned up, resources are reloaded from the system paths, and any `MVP_*` parameter settings are persisted to `moviepack_<name>.cfg` in the user data directory.

---

## 2. File naming

The filename determines the display name shown in the in-game moviepack menu:

```
mypack.aamvp.zip        → "mypack"
neon_city.aamvp.zip     → "neon city"     (underscores become spaces)
TronClassic.aamvp.zip   → "TronClassic"
```

The `.aamvp.zip` suffix is required. Files without it are not scanned.

---

## 3. Where to place the file

Drop `.aamvp.zip` files into the `moviepacks/` subdirectory of any data path. The engine scans all data paths in priority order:

| Platform | Typical path |
|---|---|
| macOS (dev build) | `<repo>/moviepacks/` |
| macOS (installed) | `~/Library/Application Support/Armagetron Advanced/moviepacks/` |
| iOS | `<app-container>/Documents/moviepacks/` (accessible via Files app) |
| Android | `<external-storage>/Android/data/org.armagetronad.armagetronad/files/moviepacks/` |
| Linux | `~/.armagetronad/moviepacks/` or `$XDG_DATA_HOME/armagetronad/moviepacks/` |

If the same pack name appears in multiple data paths, the higher-priority path wins (earlier in the search order).

---

## 4. Directory layout inside the ZIP

All paths are relative to the ZIP root. There is no top-level directory wrapper — files sit directly at the root of the archive.

```
settings.cfg                         # REQUIRED — pack configuration
preview.png                          # OPTIONAL — menu preview image (any size, shown as thumbnail)
title.jpg                            # OPTIONAL — title screen image (shown on Enter in menu)

textures/                            # OPTIONAL — override game textures
    rim_wall.png                     #   non-moviepack rim wall texture override
    ...

rim_wall_a.png                       # OPTIONAL — moviepack rim wall textures (A/B/C/D)
rim_wall_b.png                       #   loaded when sg_MoviePack() is true
rim_wall_c.png                       #   (note: these are at the root, NOT in textures/)
rim_wall_d.png
dir_wall.png                         # OPTIONAL — cycle trail/wall texture
floor.png                            # OPTIONAL — arena floor texture
floor_a.png                          # OPTIONAL — floor detail layer A
floor_b.png                          # OPTIONAL — floor detail layer B
bike.png                             # OPTIONAL — cycle body texture
cycle.ASE                            # OPTIONAL — custom cycle 3D model (ASE format)

shaders/                             # OPTIONAL — shader overrides
    uber_hooks.glsl                  #   per-component scene shading hooks
    uber.vert                        #   full vertex shader override (advanced)
    uber.frag                        #   full fragment shader override (advanced)
    postprocess/                     #   post-processing effects
        <effect_name>/
            <effect_name>.pipeline
            <effect_name>.meta
            <effect_name>.frag
            ...

sound/                               # OPTIONAL — sound overrides
    ...

music/                               # OPTIONAL — music overrides
    ...
```

### Texture path conventions

The engine resolves moviepack textures from `moviepack/` relative to data paths. When a ZIP is extracted, its contents go into the user data `moviepack/` directory, which is searched first.

**Moviepack-mode textures** (`rim_wall_a.png` through `rim_wall_d.png`, `dir_wall.png`, `floor.png`, `floor_a.png`, `floor_b.png`, `bike.png`) sit at the ZIP root. The engine loads them as `moviepack/<filename>`.

**Non-moviepack texture overrides** (like `textures/rim_wall.png`) override the corresponding system texture and go in a `textures/` subdirectory inside the ZIP. These are loaded by `rFileTexture` from the standard `textures/` search path and the extracted `moviepack/textures/` directory is found first.

### Shader path conventions

Shaders go in a `shaders/` subdirectory. The include resolver searches `moviepack/shaders/` before `shaders/`, so a moviepack's `uber_hooks.glsl` takes precedence over the system version. See `documentation/uber-shader-hooks.md` for full details.

---

## 5. settings.cfg — required

Every moviepack must contain a `settings.cfg` at the ZIP root. The engine checks for its existence to validate the pack — a ZIP without `settings.cfg` is rejected with an error message.

The file uses the same syntax as the game's main `settings.cfg`: one setting per line, `KEY value` pairs, `#` for comments.

### Common settings

| Key | Type | Description |
|---|---|---|
| `MOVIEPACK_FLOOR_RED` | float (0-1) | Floor color red component |
| `MOVIEPACK_FLOOR_GREEN` | float (0-1) | Floor color green component |
| `MOVIEPACK_FLOOR_BLUE` | float (0-1) | Floor color blue component |
| `MOVIEPACK_RIM_WALL_STRETCH_X` | float | Rim wall texture horizontal stretch |
| `MOVIEPACK_RIM_WALL_STRETCH_Y` | float | Rim wall texture vertical stretch |
| `MOVIEPACK_WALL_STRETCH` | float | Cycle wall texture stretch factor |
| `GRID_SIZE_MOVIEPACK` | float | Floor grid size when moviepack is active |
| `FLOOR_DETAIL` | int (0-2) | Floor rendering detail level (0=off, 1=simple, 2=full) |
| `POST_PROCESS_ENABLED` | bool (0/1) | Enable post-processing pipeline |
| `POST_PROCESS_EFFECT` | string | Active post-process effect directory name |
| `MVP_*` | float/int | Post-process and hook tunable parameters |

Settings are applied with owner access elevation, so they can set admin-level keys that players cannot change from the console.

### Minimal settings.cfg

```
# mypack — just a texture replacement, no special settings
MOVIEPACK_FLOOR_RED 1
MOVIEPACK_FLOOR_GREEN 1
MOVIEPACK_FLOOR_BLUE 1
```

### Settings with post-processing

```
# neonbloom — custom hooks + bloom effect
POST_PROCESS_ENABLED 1
POST_PROCESS_EFFECT bloom
MVP_INTENSITY 2.5
MVP_THRESHOLD 0.05
MVP_TINT 0.8 0.9 1.0 1.0
```

---

## 6. preview.png — optional

A preview image displayed as a thumbnail in the moviepack selection menu. Can be any size; the renderer scales it to fit the preview area. Recommended size: 256x256 or 512x512.

If omitted, the menu shows a grey placeholder rectangle.

---

## 7. title.jpg — optional

Displayed full-screen when the player presses Enter on the moviepack menu item. The game waits for a keypress before returning to the menu. If omitted, pressing Enter still activates the pack but skips the title display.

---

## 8. Creating a .aamvp.zip

### From the command line

```bash
cd mypack/
zip -r ../mypack.aamvp.zip settings.cfg preview.png title.jpg \
    rim_wall_a.png rim_wall_b.png rim_wall_c.png rim_wall_d.png \
    dir_wall.png floor.png bike.png cycle.ASE \
    shaders/ sound/ music/
```

The key rule: **no top-level wrapper directory**. `settings.cfg` must be at the root of the archive, not inside `mypack/settings.cfg`.

### Verify the structure

```bash
unzip -l mypack.aamvp.zip | head -20
```

You should see `settings.cfg` directly, not `mypack/settings.cfg`.

---

## 9. Moviepack types — from simple to advanced

### Texture-only pack

Replace some or all game textures. No shader changes, no post-processing.

```
settings.cfg
rim_wall_a.png
rim_wall_b.png
rim_wall_c.png
rim_wall_d.png
dir_wall.png
floor.png
preview.png
title.jpg
```

### Hooks-only pack

Custom per-component scene shading without touching textures.

```
settings.cfg
shaders/
    uber_hooks.glsl
```

See `documentation/uber-shader-hooks.md` for the hook API.

### Full post-process pack

Custom hooks + multi-pass post-processing (bloom, cel-shading, color grading, etc.).

```
settings.cfg
shaders/
    uber_hooks.glsl
    postprocess/
        myeffect/
            myeffect.pipeline
            myeffect.meta
            myeffect.frag
            myeffect_blur.frag
```

See `documentation/postprocess-authoring.md` for the PP pipeline.

### Complete overhaul

Everything: textures, models, shaders, sounds, floor config.

```
settings.cfg
preview.png
title.jpg
rim_wall_a.png  rim_wall_b.png  rim_wall_c.png  rim_wall_d.png
dir_wall.png
floor.png  floor_a.png  floor_b.png
bike.png
cycle.ASE
shaders/
    uber_hooks.glsl
    postprocess/
        bloom/
            bloom.pipeline
            bloom.meta
            bloom_extract.frag
            bloom_blur_h.frag
            bloom_blur_v.frag
            bloom_downsample.frag
            bloom_composite.frag
sound/
    ...
```

---

## 10. Activation lifecycle

```
User selects pack in menu
    │
    ├─ DeactivateMoviepack()
    │   ├─ Persist MVP_* values to moviepack_<old>.cfg
    │   ├─ Unregister dynamic tSettingItems
    │   ├─ Clean up extracted temp directory
    │   └─ Reload all resources (textures, models, sounds, fonts, shaders)
    │
    ├─ ExtractZipToDirectory() → user-data/moviepack/
    │   └─ Validates settings.cfg exists
    │
    ├─ Apply settings.cfg (owner access elevation)
    │
    ├─ ActivateMoviepack()
    │   ├─ Flush all caches (texture, model, sound, font, surface)
    │   ├─ Reload Vulkan shaders (picks up uber_hooks.glsl override)
    │   ├─ Notify post-process system (loads effects, registers MVP_* settings)
    │   └─ Set sg_moviepackInstalled = true
    │
    └─ Game renders with new assets
```

### Config persistence

Tuned `MVP_*` parameter values are saved per-pack to `moviepack_<name>.cfg` in the user data directory (alongside `user.cfg`). On re-activation, these saved values are loaded after `settings.cfg`, so player tweaks survive across sessions. The engine uses `tSettingItem` (not `tConfItem`) for MVP values, so they never pollute `user.cfg` and are cleanly removed when the pack is deactivated.

---

## 11. Legacy folder moviepack

The engine also supports an unzipped `moviepack/` folder directly in a data path (the "classic" moviepack from original Armagetron). This is detected by checking for `moviepack/settings.cfg` in the data search path.

If both a legacy folder and `.aamvp.zip` files exist, the legacy folder appears as "$moviepack_classic" in the menu. ZIP moviepacks appear with display names derived from their filenames.

The legacy format uses the same file layout as the ZIP contents — just unzipped into a `moviepack/` directory. The ZIP format is preferred for distribution.

---

## 12. Troubleshooting

**Pack doesn't appear in the menu.**
Check that the file is in a `moviepacks/` directory on a data search path, and that the extension is exactly `.aamvp.zip` (case-sensitive).

**"Invalid moviepack" error on activation.**
The ZIP is missing `settings.cfg` at the root. Check with `unzip -l` that `settings.cfg` is at the top level, not nested inside a subdirectory.

**Textures don't change after activating.**
The pack's texture filenames must match what the engine expects. Rim walls: `rim_wall_a.png` through `rim_wall_d.png`. Floor: `floor.png`. Trail: `dir_wall.png`. Check filenames are at the ZIP root (not in a subdirectory).

**Shaders don't take effect.**
Shader files must be in `shaders/` inside the ZIP. Check stderr for compile errors — the engine logs them with file paths and line numbers.

**Post-process effect not loading.**
Verify `POST_PROCESS_ENABLED 1` and `POST_PROCESS_EFFECT <name>` are in `settings.cfg`. The effect directory must be at `shaders/postprocess/<name>/` inside the ZIP.

**Preview image not showing.**
The file must be named exactly `preview.png` at the ZIP root. JPEG is not supported for previews.

---

## 13. Reference: shipped moviepacks

| Pack | Description |
|---|---|
| `original.aamvp.zip` | The original Armagetron textures and cycle model from 2000 |
| `gltron.aamvp.zip` | GLtron-inspired texture set |
| `armagetronadGL3.aamvp.zip` | Higher-resolution textures for the GL3/Vulkan renderer |

Unzip any of these to see working examples of the format.

---

## 14. Related documentation

- `documentation/uber-shader-hooks.md` — Per-component scene shading hooks (color + emissive)
- `documentation/postprocess-authoring.md` — Post-processing pipeline, framegraph DSL, `.meta` parameters
- `documentation/directories.txt` — Data path search order and directory layout
