// uber_hooks.glsl — Stable hook interface for moviepack shader customization.
//
// Override any of these functions in your moviepack by shipping a custom
// uber_hooks.glsl in the moviepack's shaders/ directory. The game will compile
// the system uber.frag against YOUR hooks file at moviepack load time.
//
// Default implementations are passthrough — they return the input color
// unchanged, so the scene looks identical to the stock renderer.
//
// Contract: this file is #included into uber.frag AFTER uSceneParams and the
// lighting UBO are declared. These globals are available to hooks:
//
//   uTime              — float,   per-draw frame time in seconds
//   uArenaBBox         — vec4,    (minX, minY, maxX, maxY) in world units
//   uRenderContext     — int,     current rRenderContext enum value
//   lighting.arenaBBox — vec4,    same as uArenaBBox (UBO version)
//
// Render context IDs (from rRendererState.h):
//   4 = Sky, 5 = Floor, 6 = RimWalls, 7 = PlayerWalls,
//   8 = Cycles, 9 = Zones, 10 = Effects

// Called for sky/background fragments
vec4 hookSky(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// Called for arena floor fragments
vec4 hookFloor(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// Called for rim wall fragments
vec4 hookRimWall(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// Called for cycle trail/wall fragments
vec4 hookCycleWall(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// Called for cycle body fragments (lit geometry with normals)
vec4 hookCycle(vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal)
{
    return color;
}

// Called for zone fragments
vec4 hookZone(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// Called for effects (sparks, explosions)
vec4 hookEffects(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color;
}

// =============================================================================
// EMISSIVE HOOKS
// =============================================================================
// Return the per-pixel glow contribution that the bloom post-process will
// sample from SCENE_EMISSIVE. Returning vec3(0) means "this pixel does not
// glow". The returned RGB is treated as the raw emissive color — bloom will
// tint, blur, and additively composite it over the scene.
//
// Defaults emit the component's color at different intensities. Moviepack
// authors can override any of these to change which components contribute.

// Called for cycle body fragments. Default: full cycle color (strong glow).
vec3 hookCycleEmissive(vec4 color, vec2 texCoord, vec3 modelPos, vec3 normal)
{
    return color.rgb;
}

// Called for cycle trail/wall fragments. Default: 80% of wall color.
vec3 hookCycleWallEmissive(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color.rgb * 0.8;
}

// Called for zone fragments. Default: 60% of zone color (they're already
// alpha-blended so too strong a glow reads as overbright).
vec3 hookZoneEmissive(vec4 color, vec2 texCoord, vec3 modelPos)
{
    return color.rgb * 0.6;
}
