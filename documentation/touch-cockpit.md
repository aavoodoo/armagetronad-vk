# Touch cockpit

This document covers the touch-input cockpit system: how on-screen
buttons are authored, positioned, dispatched, and rendered. The runtime
lives in `src/tron/cockpit/cTouchButton.*` and `src/tron/cockpit/cCockpit.*`;
the default overlay XML is `resource/proto/AATeam/touch-buttons-0.1.aacockpit.xml`.

## Overview

Touch buttons are HUD widgets that fire `uAction`s when tapped. They are
authored as `<TouchButton>` elements inside a cockpit XML. A primary
cockpit can optionally load a *touch overlay* — a second cockpit file
whose widgets are merged into the primary cockpit's widget list. Touch
buttons in the overlay render on top of the primary cockpit's widgets.

The default touch overlay (`touch-buttons-0.1.aacockpit.xml`) ships the
system buttons (menu, score, gyro, chat, camera, spec), the control
buttons (turn-left, brake, turn-right), and cockpit-action keys 1/2/3.

ENABLE_TOUCH controls the input model:
- `1` — tap zones (no on-screen UI, regions of the screen are mapped to actions).
- `2` — gestures.
- `3` — on-screen buttons (this document).
- `0` — off.

Each `<TouchButton>` declares which modes it appears in via `touchMode=`.

## XML attributes

### `<TouchButton>` element

```xml
<TouchButton action="…"
             player="1"
             touchMode="all|1|2|3"
             actionMode="hold|tap"
             toggleVisual="true|false"
             invert="true|false"
             tintActive="r,g,b"
             visibleIf="chatEnabled|gyroAvailable|always"
             picker="instant_chat|touch_mode"
             viewport="all|top|cycle">
    <Position .../>
    <Size .../>
    <Caption>...</Caption>          <!-- optional -->
    <Background>...</Background>     <!-- optional, SDF icon -->
</TouchButton>
```

| Attribute | Default | Effect |
|---|---|---|
| `action` | required | uAction name (CYCLE_TURN_LEFT, INGAME_MENU, COCKPIT_KEY_1, etc.) |
| `player` | `1` | 1-based player index for `uActionPlayer` dispatch |
| `touchMode` | mode 3 | `"all"` (every mode 1..3) or a single mode number |
| `actionMode` | `tap` | `hold` fires both press and release; `tap` fires only on press |
| `toggleVisual` | `false` | When `true`, visual state mirrors a queried game-state flag instead of `pressed_` |
| `invert` | `false` | Inverts the queried state (e.g. spec button: lit when NOT spectating) |
| `tintActive` | `0,1,0.4` (green) | RGB triplet (0-1 floats) modulating the SDF when `toggleVisual` + state is ON |
| `visibleIf` | (none) | Predefined predicate; absent → auto-rules apply (see below) |
| `picker` | (none) | Enables the drag-to-open dropdown picker. Values: `instant_chat`, `touch_mode` |
| `viewport` | `all` | Inherited from `CommonWidgetAttributes` (which viewports show this widget) |

### `<Position>` (anchor layout)

```xml
<Position anchorH="left|center|right|stretch"
          anchorV="top|center|bottom|stretch"
          offsetX="0.02" offsetY="-0.12"
          stretchMinX="0.0" stretchMaxX="1.0"
          stretchMinY="0.0" stretchMaxY="1.0"/>
```

Anchor + offset model (Unity-style). The anchor point is in viewport-normalized
SDL space (`anchorH="left"` → screen left, `offsetX="0.02"` → 2% to the right).
Pivot is auto-derived from the anchor (`anchorV="bottom"` puts the widget's
*bottom edge* at the anchor + offset, not its center).

Stretch axes use `stretchMin*` / `stretchMax*` ranges instead of offset.

The new anchor model is per-widget opt-in. Widgets without any anchor
attributes still use the legacy `<Position x="…" y="…"/>` additive path.

### `<Size>` (size mode)

```xml
<Size mode="aspect-locked" longest="0.057" aspect="1.0"/>
<!-- OR legacy -->
<Size width="0.05" height="0.05"/>
```

- `mode="aspect-locked"` — `longest` is the half-extent on the longer pixel axis
  expressed as a fraction of the cockpit viewport's longer side. `aspect` is
  width:height in pixels. Yields a square-in-pixels widget regardless of FBO
  shape — including portrait-leaning sub-viewports (see *Coordinate system*).
- `mode="fixed"` / `mode="proportional"` — `width` and `height` are independent
  fractions of the viewport.
- Legacy `width`/`height` attributes (no `mode`) use the original additive path.

## Authoring the visual state

### Static buttons (action fires per tap)

```xml
<TouchButton action="CYCLE_TURN_LEFT" touchMode="3">
    <Position anchorH="left" anchorV="bottom"
              offsetX="0.02" offsetY="-0.12"/>
    <Size mode="aspect-locked" longest="0.11" aspect="1.0"/>
    <Background>
        <Image>
            <Graphic name="btn_left_sdf" version="0.1"
                     author="armagetronad" category="cockpits"
                     extension="png" sdf="sdf"/>
        </Image>
    </Background>
</TouchButton>
```

### Toggle-visual buttons

`toggleVisual="true"` ties the rendered tint and alpha to a queried game
flag. The flag is dispatched by the button's `action`:

| Action | State source |
|---|---|
| `TOGGLE_SPECTATOR` | `ePlayer::spectate` (intent — updates immediately on press, not round-synced) |
| `SCORE` | `ePlayerNetID::GetShowScoresViewport(myVp)` (per-viewport HUD slot) |
| `COCKPIT_KEY_3` | `su_IsGyroActive()` (drives the gyro button) |

`invert="true"` flips the state — the spec button uses it so the gaming-pad
icon lights up when the player is NOT spectating (= will play next round).

When the state is ON, the SDF icon's RGB is modulated by `tintActive`
(default green). Plain non-toggle buttons render at full white regardless
of press state.

### Auto-hide rules (`visibleIf`)

If no `visibleIf` attribute is set, two automatic rules apply:

1. **`COCKPIT_KEY_<N>` buttons** auto-hide when no widget in the cockpit
   has registered an event handler for that key — pressing the button
   would be a no-op otherwise. Add a widget with `<MapModes
   toggleKey="N">` (or any future event-registering mechanism) to make
   the button appear.

2. Other actions always show by default.

Explicit `visibleIf` values override the auto-rules:

| Value | True when |
|---|---|
| `chatEnabled` | `ENABLE_CHAT` is true AND game is not standalone (= chat will actually be delivered to another player) |
| `gyroAvailable` | Host exposes a gyroscope sensor AND single-viewport mode (split-screen on one device can't share a single gyro) |
| `always` | Always visible |

Unknown predicate values fall through to *always visible* (fail-safe so a
typo doesn't make a button disappear silently).

### Picker (drag-to-select)

`picker="instant_chat"` opens an 8-slot dropdown of the player's
`INSTANT_CHAT_STRING_<player>_<i>` cfg values on drag-from-button-beyond-
threshold. Each slot fires `INSTANT_CHAT_<i>` when released. The button's
default action (CHAT) is deferred to release so a tap (no drag) still
opens the chat prompt.

`picker="touch_mode"` opens a 3-slot picker that flips `ENABLE_TOUCH` to
1/2/3 (tap-zones / gestures / buttons).

Pickers stack to the edge of the screen, on the same side as the
originating button. Slot rectangles are 5.2× the button's half-width.

## Coordinate system

The cockpit renders into a per-viewport FBO via `EqualAspectBottom()`:

```cpp
ret.height = std::max(width * sr_screenWidth / sr_screenHeight, 1.0f);
```

This produces:

| FBO aspect | OpenGL viewport |
|---|---|
| Landscape (W ≥ H) | `vpW × vpW` pixels, anchored at FBO bottom — extends above the screen, clipped. Cockpit NDC y +1 maps off-screen; FBO top visible at NDC y = `2H/W - 1` (e.g. 0.125 for 16:9). |
| Portrait (W < H) | `vpW × vpH` pixels — covers the entire FBO. Cockpit NDC y +1 maps to the FBO top exactly. |

`ApplyAnchorLayout` computes `visTopY = min(1, 2/vpAspect - 1)` so that
`anchorV="top"` lands at the FBO top in both regimes. Touch hit-test
mirrors the same math in `rViewport::TouchToCockpitHud` — `hy = (1 - ly)
× vpH_px × 2 / max(vpW_px, vpH_px) - 1`.

The cockpit's NDC X range always maps linearly to the FBO width (`hx = lx
× 2 - 1`).

### Legacy widgets in portrait sub-viewports

The viewport widening in `EqualAspectBottom` would otherwise shift legacy
widgets (those without anchor attributes) UP in portrait-leaning sub-
viewports, because their `m_position.y` is multiplied by `factor` to
compensate for the (previously square) cockpit aspect — and the new
viewport pixel height is larger than what that compensation assumed.

`SetFactor` works around this via `sg_LegacyYFactor`: when `vpAspect <
1` (portrait FBO), legacy widgets' Y factor is multiplied by `vpAspect`
so they land at the same FBO-pixel position they had before the
widening. Landscape FBOs are unchanged. The new anchor model is also
unchanged — `ApplyAnchorLayout` receives the raw `factor` and derives
its own `vpAspect`. Result: default cockpit's gauges look identical
across full-screen and any sub-viewport orientation.

### Aspect-locked size math

For `mode="aspect-locked"` widgets the goal is square-in-pixels with
sizes keyed off the SHORTER FBO axis. Formula:

```
pixel_full = longest × min(FBO_W, FBO_H) × (16/9)
```

Effects:

| FBO | min(W, H) | Pixel button (longest=0.057) |
|---|---:|---:|
| 1920×1080 (16:9 landscape) | 1080 | **110 px** (calibration baseline) |
| 3440×1440 (21:9 ultrawide) | 1440 | 146 px |
| 5120×1440 (32:9 ultrawide) | 1440 | 146 px (no further growth with W) |
| 1080×1920 (rotated 16:9 portrait) | 1080 | 110 px |
| 540×1920 (extreme portrait) | 540 | 55 px (natural shrink with W/H) |
| 480×854 (mobile portrait) | 480 | 49 px |

The `16/9` constant is a calibration factor so `longest` values tuned on
16:9 landscape don't need updating. The pixel size is THEN converted to
NDC half-extents using the actual OpenGL viewport (W × max(W, H)) — so
widgets stay pixel-square even when the viewport is non-square (portrait
FBO after the EqualAspectBottom widening).

`aspect` (widget aspect ratio, default 1.0) selects which widget axis
gets the full pixel size: `aspect ≥ 1` → X is the longer; otherwise Y.

Stretched axes override the size on that axis with the stretch range
(scaled by `visRangeY` for Y so the SDL-fraction maps to NDC half-extent
correctly).

## Dispatch routing

`TouchButton::OnPress / OnDrag / OnRelease` route into
`DispatchBoundAction` which dispatches based on the action's runtime type:

| Action kind | Fires |
|---|---|
| `actionName_` starts with `COCKPIT_KEY_` | `cCockpit::HandleEvent(keyNum, true)` on every cockpit (fanned via `FOREACH_COCKPIT`) |
| `actionName_ == "SCORE"` (with viewport context) | `ePlayerNetID::SetShowScoresViewport(vpIdx, !current)` |
| `uActionGlobal` (INGAME_MENU, CONSOLE_INPUT, …) | `uActionGlobalFunc::GlobalAct(action, 1.0)` |
| `uActionPlayer` / `uActionCamera` | `pc->Act(action, 1.0)` on press; `pc->Act(action, 0.0)` on release if `actionMode="hold"` |

Pickers are dispatched separately — `FireSlot` reads `slots_[i].action`
and uses the same routing.

### Per-viewport SCORE

The SCORE button binding flips a per-viewport scoreboard flag, not the
global one. The viewport index is captured at press time and stored in
the active-finger binding so MOTION / UP events keep targeting the
originating viewport even if the finger drifts onto another.

## Touch input lifecycle

`SDL_EVENT_FINGER_{DOWN,MOTION,UP}` arrive in `uInput.cpp` and are
forwarded to `cCockpit::ProcessTouch(x, y, type, fingerId)`. The cockpit
walks viewport configurations in `FOREACH_VIEWPORT`, converts the
screen-space coords to cockpit-NDC via `rViewport::TouchToCockpitHud`
(accounting for per-viewport visual rotation), and dispatches to the
matching `TouchButton`.

Per-finger state (`s_activeFingers`) maps `int64_t fingerId →
{TouchButton*, int vpIdx}`. ClearWidgets (called on cockpit reload)
scrubs entries pointing to about-to-be-deleted buttons via
`sg_DropFingerBindings` to avoid use-after-free.

Mouse input on desktop is forwarded as finger ID 0 when `ENABLE_TOUCH >=
1`, so the same dispatch path serves both touch devices and desktop
testing.

## Default touch overlay buttons

`resource/proto/AATeam/touch-buttons-0.1.aacockpit.xml` ships:

### Left column (top → bottom)

- **Menu** (INGAME_MENU, `picker="touch_mode"`) — tap opens the in-game menu, drag opens touch-mode picker.
- **Score** (SCORE, `toggleVisual="true"`) — green when this viewport's scoreboard is visible.
- **Gyro** (COCKPIT_KEY_3, `toggleVisual="true" visibleIf="gyroAvailable"`) — green when gyro look is active; hidden on devices without a gyroscope.

### Right column (top → bottom)

- **Chat** (CHAT, `picker="instant_chat" visibleIf="chatEnabled"`) — tap opens chat prompt, drag opens instant-chat picker. Hidden in local games.
- **Camera** (SWITCH_VIEW) — toggle in-cycle / external / etc.
- **Spec** (TOGGLE_SPECTATOR, `toggleVisual="true" invert="true"`) — green when the player will play next round, dim when going to spectate.

### Bottom controls (mode 3 only)

- **Turn left** (CYCLE_TURN_LEFT) bottom-left.
- **Brake** (CYCLE_BRAKE, `actionMode="hold"`) bottom-right upper.
- **Turn right** (CYCLE_TURN_RIGHT) bottom-right lower.

### Cockpit-action keys (mode 3 only, stacked above turn-left)

- **Key 1** (COCKPIT_KEY_1) — auto-hides when no widget listens.
- **Key 2** (COCKPIT_KEY_2) — auto-hides when no widget listens.
- **Key 3** (COCKPIT_KEY_3, triangle icon) — auto-hides when no widget listens. Tap fires the same event as the gyro button on the right column; the gyro button stays visible because its `visibleIf="gyroAvailable"` overrides the auto-hide rule.

## Chat scroll inset

When `ENABLE_TOUCH >= 1`, the chat console's text + background insets by
~13% of viewport width on each side so the side button columns aren't
covered. The inset is driven from `rConsoleGraph.cpp:Render` (reads
`su_GetEnableTouch()` each frame).

## Authoring a custom touch cockpit

Set `COCKPIT_FILE` (or use the cockpit pack manager) to point at any
`.aacockpit.xml` file. To override just the touch overlay, override
`TOUCH_COCKPIT_FILE` (defaults to
`AATeam/touch/touch-buttons-0.1.aacockpit.xml`).

The two slots are merged at load time — the primary cockpit's widgets
render first, the touch overlay's render last (= visually on top).

## SDF icons

Touch buttons use single-channel SDF PNGs rendered through the cockpit
SDF shader (`SetSDFMode(1)`). Icons live at
`resource/binary/<author>/<category>/<name>-<version>.aatex.png`. The
ones shipped with the default overlay:

- `armagetronad/cockpits/btn_{left,right,brake}_sdf-0.1.aatex.png` — control buttons.
- `armagetronad/cockpits/btn_{1,2,3}_sdf-0.1.aatex.png` — cockpit-action keys (3 = triangle).
- `armagetronad/cockpits/overlay_{menu,score,gyro,chat,camera,play,console}_sdf-0.1.aatex.png` — system buttons.

Generate new SDF icons with `batch/make/svg_to_sdf_icon.py` (requires
Inkscape + PIL), or render directly to PNG via PIL using the same
blur-SDF pipeline (Gaussian blur ~24 on a 1024×1024 rasterization,
Lanczos downsample to 128×128).
