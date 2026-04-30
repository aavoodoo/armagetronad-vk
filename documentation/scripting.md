# Lua Scripting for Armagetron Advanced VK

This guide covers the Lua scripting layer available in the Vulkan client. Scripts let you react to game events, read and modify game state, and declare per-session settings — all without touching C++ source.

> **Platform note:** Scripting is client-side only (`#ifndef DEDICATED`). Scripts do not run in dedicated server builds and are unavailable when connected to a remote server as a client.

---

## 1. The Lua environment

A single LuaJIT 2.1 state is shared across the entire client. It is owned by the render layer and runs on the render thread only. The environment is sandboxed: `io`, `os`, `require`, `dofile`, `package`, `debug`, and related globals are removed at startup, so scripts cannot touch the filesystem directly or load arbitrary C modules.

Available standard libraries: `math`, `string`, `table`, `coroutine`, and the safe subset of `base` (`print`, `pairs`, `ipairs`, `tonumber`, `tostring`, `type`, `pcall`, `error`, `assert`, `select`, `unpack`, `next`, `rawget`, `rawset`, `rawequal`, `setmetatable`, `getmetatable`).

---

## 2. The startup script

Place a file named `initialize.lua` in the `scripts/` subdirectory of your data directory. It is loaded once at game startup, before the main menu appears.

```
<data-dir>/scripts/initialize.lua
```

This is the entry point for all your scripting. Define event handlers, declare settings, and set initial config values here.

---

## 3. Config access

The `config` table is a transparent proxy to the game's internal config system. Keys are case-insensitive (uppercased automatically).

```lua
-- Read a config value (returns number if the value is numeric, string otherwise)
local len = config.walls_length      -- equivalent to aa_config_get("WALLS_LENGTH")
print(len)                           -- e.g. 200

-- Write a config value (owner-level access, same as typing it in the console)
config.walls_length = 150
config.CYCLE_SPEED  = 20             -- same key, different case — both work
```

Any config item that exists in the C++ config registry can be read and written this way — built-in items and items declared with `aa_setting` alike.

### Low-level API

The proxy is built on two raw functions, available if you need them:

```lua
local v = aa_config_get("WALLS_LENGTH")   -- string | nil
local ok = aa_config_set("WALLS_LENGTH", "150")  -- bool (false if item not found)
```

---

## 4. Declaring per-session settings

`aa_setting` registers a new config item that lives only for the current session. It is never written to `user.cfg`. The value is stored both in the C++ config registry (so it can be changed from the console) and as a Lua global (so scripts can read it directly without going through `config`).

```lua
-- aa_setting(name, default_value)
-- name must be UPPER_CASE (console convention)
-- default_value can be a number or a string

aa_setting("MY_SPEED_MULTIPLIER", 1.5)
aa_setting("MY_WELCOME_MSG", "Hello!")

-- The default value is immediately available as a Lua global:
print(MY_SPEED_MULTIPLIER)   -- 1.5
print(MY_WELCOME_MSG)        -- Hello!

-- Also readable through the config proxy:
print(config.my_speed_multiplier)  -- 1.5
```

From the in-game console you can then type:
```
MY_SPEED_MULTIPLIER 2.0
```
…and the Lua global `MY_SPEED_MULTIPLIER` is updated automatically.

`aa_setting` is **idempotent**: calling it again with the same name (e.g. because `initialize.lua` is re-evaluated) is a no-op — the existing item and its current value are preserved.

---

## 5. Game event hooks

All game events that pass through the ladder-log system are dispatched to Lua automatically. Define a function named `on_<lowercase_event_name>` and it will be called with the event's arguments as a single space-separated string.

```lua
function on_new_round(args)
    print("Round started")
end

function on_death_frag(args)
    -- args = "victim killer"
    local victim, killer = args:match("(%S+)%s+(%S+)")
    print(killer .. " killed " .. victim)
end

function on_chat(args)
    -- args = "player_name message..."
    local player, message = args:match("(%S+)%s+(.*)")
    print("[chat] " .. player .. ": " .. message)
end
```

### Complete event reference

**Game state** (fired by `gGame.cpp`)

| Function | `args` content |
|----------|---------------|
| `on_new_round(args)` | _(empty)_ |
| `on_new_match(args)` | _(empty)_ |
| `on_round_winner(args)` | `"team_name"` |
| `on_match_winner(args)` | `"team_name"` |
| `on_game_end(args)` | _(empty)_ |
| `on_game_time(args)` | `"time"` |
| `on_new_warmup(args)` | _(empty)_ |
| `on_matches_left(args)` | `"count"` |

**Player lifecycle** (fired by `ePlayer.cpp`)

| Function | `args` content |
|----------|---------------|
| `on_player_entered(args)` | `"name ip"` |
| `on_player_left(args)` | `"name"` |
| `on_player_renamed(args)` | `"old_name new_name"` |
| `on_player_respawn(args)` | `"name"` |
| `on_round_score(args)` | `"name score"` |
| `on_chat(args)` | `"name message..."` |
| `on_command(args)` | `"name command..."` |

**Death** (fired by `gCycle.cpp`, `gExplosion.cpp`, `zEffector.cpp`)

| Function | `args` content |
|----------|---------------|
| `on_death_frag(args)` | `"victim killer"` |
| `on_death_suicide(args)` | `"victim"` |
| `on_death_teamkill(args)` | `"victim killer"` |
| `on_death_deathzone(args)` | `"victim"` |
| `on_death_explosion(args)` | `"victim"` |
| `on_sacrifice(args)` | `"victim"` |

**Zones / areas** (fired by `zEffector.cpp`, `gWinZone.cpp`)

| Function | `args` content |
|----------|---------------|
| `on_winzone_player_enter(args)` | `"player_name"` |
| `on_basezone_conquered(args)` | `"zone team"` |
| `on_basezone_conquerer(args)` | `"zone player"` |

**Teams** (fired by `eTeam.cpp`)

| Function | `args` content |
|----------|---------------|
| `on_team_created(args)` | `"team_name"` |
| `on_team_destroyed(args)` | `"team_name"` |
| `on_team_renamed(args)` | `"old_name new_name"` |
| `on_team_player_added(args)` | `"team player"` |
| `on_team_player_removed(args)` | `"team player"` |
| `on_round_score_team(args)` | `"team score"` |
| `on_online_player(args)` | `"name"` |
| `on_online_ai(args)` | `"name"` |
| `on_online_team(args)` | `"name"` |
| `on_num_humans(args)` | `"count"` |

---

## 6. Per-frame hook

`on_frame()` is called every rendered frame, after all per-frame game tasks complete. Use it for continuous monitoring or HUD logic. Keep it fast — it runs on the render thread.

```lua
local frame_count = 0
function on_frame()
    frame_count = frame_count + 1
end
```

---

## 7. Reading game state

### Players

`aa_players()` returns an array of tables, one per active player.

```lua
for _, p in ipairs(aa_players()) do
    print(p.name, p.score, p.ping)
    if p.alive then
        print("  position:", p.x, p.y)
        print("  direction:", p.dir_x, p.dir_y)
        print("  speed:", p.speed, "rubber:", p.rubber)
    end
end
```

**Player table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name |
| `score` | integer | Current score |
| `ping` | number | Network ping (ms) |
| `team` | string | Team name, or `""` if no team |
| `spectating` | bool | True if spectating |
| `chatting` | bool | True if chat input is open |
| `color_r` | integer | Red component (0–255) |
| `color_g` | integer | Green component (0–255) |
| `color_b` | integer | Blue component (0–255) |
| `alive` | bool | True if the cycle is alive |
| `x` | number | World X position _(alive only)_ |
| `y` | number | World Y position _(alive only)_ |
| `dir_x` | number | Direction vector X _(alive only)_ |
| `dir_y` | number | Direction vector Y _(alive only)_ |
| `speed` | number | Current speed _(alive only)_ |
| `rubber` | number | Current rubber value _(alive only)_ |

### Teams

`aa_teams()` returns an array of tables, one per team.

```lua
for _, t in ipairs(aa_teams()) do
    print(t.name, t.score, t.alive_players .. "/" .. t.num_players)
    for _, pname in ipairs(t.players) do
        print("  - " .. pname)
    end
end
```

**Team table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Team name |
| `score` | integer | Current score |
| `num_players` | integer | Total players in team |
| `num_humans` | integer | Human players only |
| `num_ais` | integer | AI players only |
| `alive_players` | integer | Currently alive players |
| `color_r` | integer | Red (0–255) |
| `color_g` | integer | Green (0–255) |
| `color_b` | integer | Blue (0–255) |
| `players` | array of string | Player names |

---

## 8. Quick testing with /eval

From the in-game chat or console, prefix any Lua expression with `/eval` to run it immediately:

```
/eval print(config.walls_length)
/eval config.walls_length = 300
/eval for _,p in ipairs(aa_players()) do print(p.name, p.score) end
/eval MY_SPEED_MULTIPLIER = 2.0
```

`/eval` uses `lua_pcall` — errors are logged but do not crash the game.

---

## 9. Worked examples

### Score announcer

```lua
function on_death_frag(args)
    local victim, killer = args:match("(%S+)%s+(%S+)")
    if not killer then return end
    for _, p in ipairs(aa_players()) do
        if p.name == killer then
            -- could trigger a config item tied to a sound or display
            config.last_kill_score = tostring(p.score)
            break
        end
    end
end
```

### Dynamic wall length based on player count

```lua
aa_setting("BASE_WALLS_LENGTH", 200)

function on_new_round()
    local players = aa_players()
    local count = 0
    for _ in ipairs(players) do count = count + 1 end
    -- Shorter walls with more players
    config.walls_length = math.max(50, BASE_WALLS_LENGTH - count * 5)
end
```

### Per-round config reset

```lua
local saved_speed

function on_new_match()
    saved_speed = config.cycle_speed
end

function on_round_winner(args)
    print("Round won by: " .. args)
    -- Restore speed each round
    if saved_speed then
        config.cycle_speed = saved_speed
    end
end
```

---

## 10. Notes and limitations

- **One shared state**: effect scripts (post-process `.lua` files) and game scripts share the same Lua state and global namespace. Avoid global name collisions between your `initialize.lua` and any effect-related globals (`effect`, `SCENE_COLOR`, etc.).
- **Render thread only**: all Lua execution happens on the render thread. Do not call `aa_players()` or modify config from a background thread.
- **Client-side only**: hooks do not fire when connected to a remote server as a client (the ladder-log writer is gated by `sn_GetNetState() != nCLIENT`).
- **No write-back to game objects**: `aa_players()` and `aa_teams()` return snapshots. You can read state but cannot directly mutate player objects — use `aa_config_set` / the `config` proxy to influence gameplay through config items.
- **Errors are non-fatal**: all Lua calls use `pcall`. A runtime error in any hook prints to the log and continues; it does not crash the game.
