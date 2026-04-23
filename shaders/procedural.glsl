// procedural.glsl - Utility functions for procedural animated shaders
// Include this file in moviepack shaders for Tron-style effects
//
// Usage: #include "procedural.glsl" (requires shader include support)
// Or copy the functions you need into your shader

//=============================================================================
// RENDER CONTEXT CONSTANTS (must match rRenderContext enum in rGL3Render.h)
//=============================================================================
const int RENDER_CTX_UNKNOWN = 0;
const int RENDER_CTX_TITLE_SCREEN = 1;
const int RENDER_CTX_MENU = 2;
const int RENDER_CTX_GAME3D = 3;
const int RENDER_CTX_SKY = 4;
const int RENDER_CTX_FLOOR = 5;
const int RENDER_CTX_RIM_WALLS = 6;
const int RENDER_CTX_PLAYER_WALLS = 7;
const int RENDER_CTX_CYCLES = 8;
const int RENDER_CTX_ZONES = 9;
const int RENDER_CTX_EFFECTS = 10;
const int RENDER_CTX_HUD = 11;

//=============================================================================
// HASH / NOISE FUNCTIONS
//=============================================================================

// Simple hash function for seeding procedural noise
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Hash function for vec2 output
vec2 hash2(vec2 p)
{
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// Value noise (smooth interpolated noise)
float noise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Cubic interpolation for smoother results
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// Fractal Brownian Motion (layered noise)
float fbm(vec2 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++)
    {
        value += amplitude * noise2D(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

//=============================================================================
// GRID PATTERNS
//=============================================================================

// Rectangular grid pattern
// Returns 1.0 on grid lines, 0.0 elsewhere
// spacing: distance between grid lines
// width: line thickness
float grid(vec2 uv, float spacing, float width)
{
    vec2 g = abs(fract(uv / spacing - 0.5) - 0.5) * spacing;
    float d = min(g.x, g.y);
    return 1.0 - smoothstep(0.0, width, d);
}

// Grid with glow effect
// Returns soft falloff for glow rendering
float gridGlow(vec2 uv, float spacing, float width, float glowSize)
{
    vec2 g = abs(fract(uv / spacing - 0.5) - 0.5) * spacing;
    float d = min(g.x, g.y);
    return exp(-d * d / (glowSize * glowSize));
}

// Hexagonal grid coordinates
// Returns hex cell center and distance to edge
vec3 hexGrid(vec2 uv)
{
    const vec2 s = vec2(1.0, 1.732050808);

    vec4 hC = floor(vec4(uv, uv - vec2(0.5, 1.0)) / s.xyxy) + 0.5;
    vec4 h = vec4(uv - hC.xy * s, uv - (hC.zw + 0.5) * s);

    vec2 hex = dot(h.xy, h.xy) < dot(h.zw, h.zw) ? h.xy : h.zw;

    float edgeDist = max(abs(hex.x), abs(hex.x * 0.5 + hex.y * 0.866025404));

    return vec3(hex, edgeDist);
}

//=============================================================================
// VORONOI / CELLULAR PATTERNS
//=============================================================================

// Voronoi distance field
// Returns distance to nearest cell center
vec3 voronoi(vec2 uv, float time)
{
    vec2 n = floor(uv);
    vec2 f = fract(uv);

    float minDist = 8.0;
    vec2 minPoint = vec2(0.0);
    vec2 minCell = vec2(0.0);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = hash2(n + neighbor);

            // Animate cell centers
            point = 0.5 + 0.5 * sin(time * 0.5 + 6.2831 * point);

            vec2 diff = neighbor + point - f;
            float dist = length(diff);

            if (dist < minDist)
            {
                minDist = dist;
                minPoint = point;
                minCell = n + neighbor;
            }
        }
    }

    return vec3(minDist, minCell);
}

//=============================================================================
// TIME-BASED ANIMATION
//=============================================================================

// Smooth pulse oscillation (0 to 1)
float pulse(float time, float frequency)
{
    return 0.5 + 0.5 * sin(time * frequency * 6.2831);
}

// Sawtooth wave (0 to 1)
float sawtooth(float time, float frequency)
{
    return fract(time * frequency);
}

// Triangle wave (0 to 1)
float triangle(float time, float frequency)
{
    return abs(2.0 * fract(time * frequency) - 1.0);
}

// Smooth step pulse (sharp on, soft off)
float stepPulse(float time, float frequency, float duty)
{
    float t = fract(time * frequency);
    return smoothstep(0.0, 0.1, t) * smoothstep(duty + 0.1, duty, t);
}

//=============================================================================
// TRON-STYLE EFFECTS
//=============================================================================

// Tron color ramp (dark blue -> cyan -> white)
vec3 tronRamp(float t)
{
    vec3 darkBlue = vec3(0.0, 0.1, 0.2);
    vec3 cyan = vec3(0.0, 0.8, 1.0);
    vec3 white = vec3(1.0);

    if (t < 0.5)
    {
        return mix(darkBlue, cyan, t * 2.0);
    }
    else
    {
        return mix(cyan, white, (t - 0.5) * 2.0);
    }
}

// Scanline effect (horizontal lines like CRT)
float scanLines(vec2 uv, float density, float intensity)
{
    return 1.0 - intensity * (0.5 + 0.5 * sin(uv.y * density * 3.14159));
}

// Energy wave traveling outward from a point
float energyWave(vec2 uv, vec2 center, float time, float speed, float width)
{
    float dist = length(uv - center);
    float wave = fract(dist * 0.5 - time * speed);
    return smoothstep(0.0, width, wave) * smoothstep(width * 2.0, width, wave);
}

// Circuit trace effect
float circuitTrace(vec2 uv, float time, float scale)
{
    vec2 p = uv * scale;
    vec2 i = floor(p);
    vec2 f = fract(p);

    float h = hash(i);

    // Determine trace direction based on cell hash
    float traceX = step(0.5, h) * step(0.4, f.x) * step(f.x, 0.6);
    float traceY = step(h, 0.5) * step(0.4, f.y) * step(f.y, 0.6);

    // Animate along trace
    float animX = smoothstep(0.0, 1.0, fract(time * 0.5 + hash(i + vec2(0.1, 0.2))));
    float animY = smoothstep(0.0, 1.0, fract(time * 0.5 + hash(i + vec2(0.3, 0.4))));

    float trace = max(traceX * step(f.x, animX), traceY * step(f.y, animY));

    return trace;
}

// Neon glow falloff
float neonGlow(float dist, float radius, float intensity)
{
    return intensity * exp(-dist * dist / (radius * radius));
}

// Digital glitch effect
float glitch(vec2 uv, float time, float intensity)
{
    float blockY = floor(uv.y * 20.0);
    float noise = hash(vec2(blockY, floor(time * 10.0)));

    if (noise > 1.0 - intensity * 0.1)
    {
        return hash(vec2(uv.x * 100.0, time));
    }
    return 0.0;
}

// Data stream (Matrix-style falling characters placeholder)
float dataStream(vec2 uv, float time, float density)
{
    vec2 cell = floor(uv * vec2(density, density * 2.0));
    float offset = hash(vec2(cell.x, 0.0));
    float speed = 0.5 + hash(vec2(cell.x, 1.0)) * 0.5;

    float y = fract(uv.y * density * 2.0 - time * speed - offset);
    float brightness = smoothstep(1.0, 0.0, y);

    // Only show some columns
    float columnMask = step(hash(vec2(cell.x, 2.0)), 0.3);

    return brightness * columnMask;
}
