#version 450

// Debug: set to 1 to visualize depth buffer as grayscale.
// Reverse-Z + linearization: near surfaces read BLACK, far surfaces read
// WHITE (forward-Z mental model). See the DEBUG_DEPTH_VIZ block at the
// bottom of main() for the linearization formula and tunable n/f window.
#define DEBUG_DEPTH_VIZ 0

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 0) uniform LightingUBO {
    vec4 lightPos[2];
    vec4 lightDiffuse[2];
    vec4 lightSpecular[2];
    vec4 materialDiffuse;
    vec4 materialSpecular;
    int  lightingEnabled;
    int  shadowEnabled;
    // 8 bytes implicit std140 padding to next vec4 boundary
    vec4 arenaBBox;     // (minX, minY, maxX, maxY)
    mat4 shadowVP[2];   // light view-projection matrices
} lighting;

// Shadow maps (sampler2DShadow for hardware PCF via comparison sampler)
layout(set = 1, binding = 1) uniform sampler2DShadow uShadowMap0;
layout(set = 1, binding = 2) uniform sampler2DShadow uShadowMap1;

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
    mat4 uTexMatrix;
    mat4 uNormalMatrix;
} pc;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec3 vModelPos;

// Output 0: normal scene color (what gets displayed through post-processing).
// Output 1: per-component emissive contribution. Default is vec4(0) — only
// hook functions that explicitly opt in (cycles, walls, zones) populate it.
// The bloom post-process effect samples this instead of the scene color so
// it can glow objects regardless of their final-composite brightness.
//
// USE_EMISSIVE_OUT is defined by the C++ side (rVulkanShader::CompileGLSL)
// when compiling the variant used for the post-process offscreen render
// pass (2 color attachments). When compiling for the plain swapchain pass
// (1 color attachment), the define is absent and we drop the location=1
// output entirely — otherwise Vulkan logs a pipeline-creation warning for
// every pipeline bound to the 1-attachment render pass:
//   "writes to output Location 1 but there is no
//    VkSubpassDescription::pColorAttachments[1]"
layout(location = 0) out vec4 fragColor;
#ifdef USE_EMISSIVE_OUT
layout(location = 1) out vec4 emissiveOut;
#define WRITE_EMISSIVE(v) emissiveOut = (v)
#else
#define WRITE_EMISSIVE(v) do { } while (false)
#endif

// Globals exposed to uber_hooks.glsl — kept stable across engine updates.
// Packed into push constants by the C++ side (BuildPushConstants):
//   uTexMatrix[2][0] = time, [2][1] = render context
//   For unlit geometry only (normalMatrix is free):
//     uNormalMatrix[2] = camera world position (xyz) — for parallax
//     uNormalMatrix[3] = arena bbox (minX,minY,maxX,maxY) — for floor effects
#define uTime          (pc.uTexMatrix[2][0])
#define uRenderContext (int(pc.uTexMatrix[2][1]))
#define uCameraPos     (pc.uNormalMatrix[2].xyz)
#define uArenaBBox     (pc.uNormalMatrix[3])

#include "uber_hooks.glsl"

float sdfMedian(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

// Shadow mapping: compute shadow factor for a world-space position.
// Returns 0.0 = fully in shadow, 1.0 = fully lit.
// Uses 4-tap PCF for soft shadow edges.
float computeShadow(vec3 worldPos)
{
    if (lighting.shadowEnabled == 0) return 1.0;

    float shadow = 0.0;
    for (int i = 0; i < 2; i++)
    {
        vec4 lightSpace = lighting.shadowVP[i] * vec4(worldPos, 1.0);
        vec3 projCoords = lightSpace.xyz / lightSpace.w;
        // NDC X,Y are in [-1,1], remap to UV [0,1] for texture sampling.
        // Z is already [0,1] from the clip matrix.
        projCoords.xy = projCoords.xy * 0.5 + 0.5;

        // Skip if outside shadow map range
        if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
            projCoords.y < 0.0 || projCoords.y > 1.0 ||
            projCoords.z < 0.0 || projCoords.z > 1.0)
        {
            shadow += 0.5; // outside = lit (per light contribution)
            continue;
        }

        // 4-tap PCF (2x2 kernel)
        float texelSize = 1.0 / 2048.0; // SHADOW_MAP_SIZE
        float pcf = 0.0;
        for (int x = -1; x <= 0; x++)
        {
            for (int y = -1; y <= 0; y++)
            {
                vec2 offset = vec2(float(x) + 0.5, float(y) + 0.5) * texelSize;
                vec3 sampleCoord = vec3(projCoords.xy + offset, projCoords.z);
                if (i == 0)
                    pcf += texture(uShadowMap0, sampleCoord);
                else
                    pcf += texture(uShadowMap1, sampleCoord);
            }
        }
        shadow += pcf / 4.0 * 0.5; // each light contributes half
    }

    return shadow;
}

void main()
{
    // Default emissive is zero — only the non-font path's hook switch
    // overrides it for components that should glow. Initializing here
    // guarantees every code path writes a defined value to the emissive
    // attachment (Vulkan requires all declared outputs be written).
    // Expands to a no-op in the USE_EMISSIVE_OUT-off variant.
    WRITE_EMISSIVE(vec4(0.0));

    // SDF font mode: pc.uTexMatrix[3][3] = -fontMode (negative sentinel, 0 = normal rendering).
    float fontMode = -pc.uTexMatrix[3][3];
    if (fontMode > 3.5)
    {
        // SDF outline rendering (fontMode 4).
        // Unified mode: a useTexture flag selects where inside color comes from.
        //
        // Parameters packed in texMatrix:
        //   [0][0] = screenPxRange
        //   [0][1] = outlineWidth (0.0-0.5 in SDF units)
        //   [0][2] = inside R,  [0][3] = inside G       (used when useTexture=0)
        //   [1][0] = inside B,  [1][1] = inside A       (used when useTexture=0)
        //   [1][2] = outline R, [1][3] = outline G
        //   [2][0] = outline B
        //   [2][1] = useTexture flag (0=color from push constants, 1=color from texture RGB)
        //   [2][2] = invert flag (1=invert SDF, for B&W images where black=inside)
        //
        // SDF source: alpha channel when useTexture=1 (RGBA PNG with A=SDF),
        //             red channel when useTexture=0 (single-channel SDF texture).

        float screenPxRange = pc.uTexMatrix[0][0];
        float outlineWidth  = pc.uTexMatrix[0][1];
        float useTexture    = pc.uTexMatrix[2][1];
        float invertSDF     = pc.uTexMatrix[2][2];

        vec4 texSample = texture(uTexture, vTexCoord);

        // SDF distance field. Single-channel textures are stored as (V,V,V,V)
        // in the Vulkan renderer — all channels have the same value.
        // For RGBA textures with SDF in alpha, .a works. For single-channel, .r works.
        // Using .a covers both cases since V is in all channels.
        float sd = texSample.a;
        if (invertSDF > 0.5) sd = 1.0 - sd;

        float dist = screenPxRange * (sd - 0.5);

        // Compute alpha for inside fill and outline band
        float insideAlpha  = clamp(dist + 0.5, 0.0, 1.0);
        float outlineAlpha = clamp(dist + outlineWidth * screenPxRange + 0.5, 0.0, 1.0);

        // Inside color: from texture RGB × vColor, or from push constants
        vec4 insideColor;
        if (useTexture > 0.5) {
            insideColor = vec4(texSample.rgb * vColor.rgb, vColor.a);
        } else {
            insideColor = vec4(pc.uTexMatrix[0][2], pc.uTexMatrix[0][3],
                               pc.uTexMatrix[1][0], pc.uTexMatrix[1][1]);
        }

        // Outline color always from push constants
        vec3 outlineColor = vec3(pc.uTexMatrix[1][2], pc.uTexMatrix[1][3],
                                 pc.uTexMatrix[2][0]);

        // Composite: outline behind inside fill, outside is transparent
        vec3 rgb = mix(outlineColor, insideColor.rgb, insideAlpha);
        float a = outlineAlpha * mix(1.0, insideColor.a, insideAlpha);

        fragColor = vec4(rgb, a * vColor.a);
        if (fragColor.a < 0.01) discard;
        WRITE_EMISSIVE(vec4(0.0));
    }
    else if (fontMode > 0.5)
    {
        // Font SDF modes: 1=SDF, 2=MSDF, 3=MTSDF
        float screenPxRange = pc.uTexMatrix[0][0];
        float alpha;
        if (fontMode < 1.5)
        {
            float sd = texture(uTexture, vTexCoord).r;
            float screenPxDist = screenPxRange * (sd - 0.5);
            alpha = clamp(screenPxDist + 0.5, 0.0, 1.0);
        }
        else if (fontMode < 2.5)
        {
            vec3 msd = texture(uTexture, vTexCoord).rgb;
            float sd = sdfMedian(msd.r, msd.g, msd.b);
            float screenPxDist = screenPxRange * (sd - 0.5);
            alpha = clamp(screenPxDist + 0.5, 0.0, 1.0);
        }
        else
        {
            vec4 mtsdf = texture(uTexture, vTexCoord);
            float sd = sdfMedian(mtsdf.r, mtsdf.g, mtsdf.b);
            float screenPxDist = screenPxRange * (sd - 0.5);
            alpha = clamp(screenPxDist + 0.5, 0.0, 1.0);
        }
        fragColor = vec4(vColor.rgb, vColor.a * alpha);
        if (fragColor.a < 0.01) discard;
    }
    else
    {
        vec4 texColor = texture(uTexture, vTexCoord);

        // Pre-shadow base color for emissive hooks. Shadows attenuate the visible
        // fragColor but must NOT suppress bloom — a face in shadow still glows.
        // For the lit (cycle body) path this is texColor; for the unlit (wall) path
        // it is vColor*texColor captured before the shadow multiply.
        vec4 emissiveBase = texColor;

        // Lighting flag in push constant uTexMatrix[2][3] (per-draw, not shared UBO)
        if (pc.uTexMatrix[2][3] > 0.5)
        {
            // Blinn-Phong lighting.
            // Material color sourcing (partial-tint model):
            //   Non-instanced path: vColor = white (uber.vert lit path hardcodes vec4(1.0));
            //     materialDiffuse_ (team color) is in pc.uNormalMatrix[3].rgb.
            //     matColor = white * team_color = team_color.
            //   Instanced path: vColor = aInstanceColor (team color per instance);
            //     pc.uNormalMatrix[3].rgb = white (DrawInstancedModelMesh sets it to 1,1,1).
            //     matColor = team_color * white = team_color.
            //   In both cases matColor = team_color for diffuse/specular lighting.
            //
            // The texture is pre-processed by gTextureCycle::ProcessImage: transparent areas
            // are filled with the team color; opaque (highlight) areas keep their original
            // white/gray color. Lighting modulates that pre-tinted texture — the texColor
            // multiplication does NOT include vColor because the tint is already baked in.
            vec3 N = normalize(vNormal);
            vec3 V = vec3(0.0, 0.0, 1.0);  // infinite viewer (+Z toward camera in view space)

            // Two-sided lighting: flip normal if it faces away from the camera.
            // Models (ASE, .mod) may have faces with inconsistent winding; after
            // disabling face culling they render with normals pointing into the screen.
            // gl_FrontFacing is unreliable here (pipeline key normalizes frontFaceCW=true
            // when cullFace=false, swapping the convention). Camera-space dot product is
            // winding-convention-independent.
            if (dot(N, V) < 0.0) N = -N;

            // matColor = white (neutral lighting).
            // Team color comes from gTextureCycle::ProcessImage which bakes the player
            // color into the transparent areas of the texture before GPU upload.
            // Opaque (white/gray highlight) pixels are left untouched by ProcessImage
            // and must not be re-tinted here — white lighting keeps them white.
            // Using team_color as matColor (the old "BUGFIX" approach) makes the
            // lighting itself team-colored, washing out all highlights to solid team color.
            vec3 matColor = vec3(1.0);

            vec3 diffuseLight = vec3(0.15);  // base ambient
            vec3 specularLight = vec3(0.0);

            for (int i = 0; i < 2; i++)
            {
                vec3 L = normalize(lighting.lightPos[i].xyz);
                float NdotL = max(dot(N, L), 0.0);

                // Half-Lambert (wrap) diffuse: maps NdotL from [0,1] to [0.5,1].
                // Prevents any surface from going fully dark, giving a soft
                // game-friendly look (same technique as Half-Life 2 / Source).
                float halfLambert = NdotL * 0.5 + 0.5;
                diffuseLight += halfLambert * lighting.lightDiffuse[i].rgb * matColor;

                if (NdotL > 0.0)
                {
                    vec3 H = normalize(L + V);
                    float NdotH = max(dot(N, H), 0.0);
                    specularLight += pow(NdotH, 16.0) * lighting.lightSpecular[i].rgb * matColor;
                }
            }

            // Apply shadow mapping: shadow factor attenuates direct lighting but
            // preserves the ambient base so shadowed areas aren't pitch black.
            float shadowFactor = computeShadow(vModelPos);
            vec3 ambient = vec3(0.15);
            vec3 directDiffuse = diffuseLight - ambient; // separate direct from ambient
            // texColor carries the pre-baked partial tint from ProcessImage;
            // don't multiply by vColor again (that would double-tint).
            fragColor.rgb = texColor.rgb * (ambient + directDiffuse * shadowFactor) + specularLight * shadowFactor;
            fragColor.a = texColor.a;
        }
        else
        {
            // Normal unlit rendering: vertex color * texture
            fragColor = vColor * texColor;
            emissiveBase = fragColor;  // save before brightening + shadow for emissive hooks

            // Cycle-wall trail glow: the begin-gradient (streaming) portion of
            // the wall used to bake a per-vertex brightening
            //   cr = r + cfunc(rat), cfunc(rat) = rat*rat
            // into the CPU vertex color, with rat going 0→1 from junction to
            // cycle tip. Computing it CPU-side ran the channel through uint8
            // packing — `r + cfunc(rat) > 1` clipped to 1 and the shader's
            // emissive-side de-brightening then over-subtracted for bright
            // team colors. We now reproduce the same `+ cfunc * texColor`
            // contribution here in fp32, recovered from the alpha channel:
            //   cfunc(rat) = rat*rat = 1 - (1 - rat*rat) = 1 - afunc(rat)
            //              = 1 - vColor.a   (afunc encodes vColor.a)
            // Static walls and the GPU-compute path send alpha = 1, so the
            // brightening evaluates to zero — this is a no-op for them.
            // Applied before the shadow multiply to match the historical
            // behaviour where shadow attenuated the whole composite.
            if (uRenderContext == 7)
            {
                float brightening = max(0.0, 1.0 - vColor.a);
                fragColor.rgb += brightening * texColor.rgb;
            }

            // Shadow mapping on unlit surfaces (floor, cycle walls)
            if (lighting.shadowEnabled != 0)
            {
                int ctx = uRenderContext;
                if (ctx == 5 || ctx == 7) // floor or cycle wall
                {
                    float sf = computeShadow(vModelPos);
                    fragColor.rgb *= mix(0.5, 1.0, sf);
                }
            }
        }

        // Dispatch to the per-component color hook function. Default hooks
        // are passthrough — moviepacks can override uber_hooks.glsl.
        // Render context IDs match rRenderContext enum in rRendererState.h.
        switch (uRenderContext)
        {
            case 4:  fragColor = hookSky      (fragColor, vTexCoord, vModelPos); break;
            case 5:  fragColor = hookFloor    (fragColor, vTexCoord, vModelPos); break;
            case 6:  fragColor = hookRimWall  (fragColor, vTexCoord, vModelPos); break;
            case 7:  fragColor = hookCycleWall(fragColor, vTexCoord, vModelPos); break;
            case 8:  fragColor = hookCycle    (fragColor, vTexCoord, vModelPos, vNormal); break;
            case 9:  fragColor = hookZone     (fragColor, vTexCoord, vModelPos); break;
            case 10: fragColor = hookEffects  (fragColor, vTexCoord, vModelPos); break;
            default: break;
        }

        // Dispatch to the per-component EMISSIVE hook. Default (initialized
        // at top of main) is vec4(0) — the pixel contributes nothing to bloom.
        // Cycles, cycle walls, and zones have non-zero defaults so they glow.
        // All other components (floor, rim walls, sky, HUD) stay dark.
        // Alpha of the emissive output doubles as a "how much does this
        // pixel glow" scalar.
        vec3 em = vec3(0.0);
        // Per-attachment source alpha for the emissive output. The pipeline
        // shares one blend mode across both attachments (rVulkanPipeline.cpp:415);
        // when that blend mode is Alpha, each attachment's blend reads its own
        // location's alpha as src_alpha — color uses fragColor.a, emissive uses
        // emissiveOut.a. We exploit that: writing emissiveOut.a = 1 makes the
        // emissive attachment behave as a full replace (`em*1 + dst*0`),
        // unaffected by the per-vertex fade alpha that the color attachment
        // uses. Cycle walls in the begin-gradient (alpha-blend) path set this
        // to 1 so bloom/cel-shading see the same intensity as on the static
        // portion. Other contexts keep the legacy `clamp(emMax)` encoding.
        // 0.0 means "no glow at this pixel" — leave at default for the unused
        // contexts; non-zero contexts overwrite below.
        float emAlpha = 0.0;
        switch (uRenderContext)
        {
            case 6:
                em = hookRimWallEmissive(fragColor, vTexCoord, vModelPos);
                emAlpha = clamp(max(max(em.r, em.g), em.b), 0.0, 1.0);
                break;
            case 7: {
                // Cycle walls — vColor.rgb carries the flat team color (CPU
                // no longer brightens it); the visible color path above adds
                // the trail glow into fragColor only. emissiveBase = vColor *
                // texColor is the un-brightened wall surface — exactly what
                // bloom and cel-shading want to mark, uniformly across the
                // streaming + static portions.
                em = hookCycleWallEmissive(emissiveBase, vTexCoord, vModelPos);
                // Force full opaque replace on the emissive attachment so the
                // begin-gradient's alpha-blend doesn't fade the bloom/mask
                // toward the cycle tip. Mask-based effects (celshading.frag
                // gates on `emissive.a > 0.01`) get a clean binary cover.
                emAlpha = 1.0;
                break;
            }
            case 8:
                em = hookCycleEmissive(emissiveBase, vTexCoord, vModelPos, vNormal);
                emAlpha = clamp(max(max(em.r, em.g), em.b), 0.0, 1.0);
                break;
            case 9:
                em = hookZoneEmissive(fragColor, vTexCoord, vModelPos);
                emAlpha = clamp(max(max(em.r, em.g), em.b), 0.0, 1.0);
                break;
            default:
                break;
        }
        WRITE_EMISSIVE(vec4(em, emAlpha));

        // Alpha-test flag in uTexMatrix[2][2] — only discard when explicitly requested
        if (pc.uTexMatrix[2][2] > 0.5 && fragColor.a < 0.01) discard;

#if DEBUG_DEPTH_VIZ
        // Reverse-Z linearized depth visualization.
        // Reverse-Z maps near = 1.0, far = 0.0 in the buffer; raw values
        // cluster near 0 for typical gameplay distances, leaving little
        // visible contrast. We linearize back to view-space distance and
        // normalize so close geometry reads as BLACK and the zFar plane
        // reads as WHITE — the forward-Z mental model.
        //
        // d = (n*f) / (n + z_buf * (f - n))  is the inverse of the
        // reverse-Z projection. n,f below are visualization hardcoded
        // values, NOT the actual rendering near/far — they only choose
        // the "interesting" distance band that maps to the gradient.
        const float dN = 0.05;
        const float dF = 50.0;
        float zb = gl_FragCoord.z;
        float d_lin = (dN * dF) / (dN + zb * (dF - dN));
        float v = clamp(d_lin / dF, 0.0, 1.0);
        fragColor = vec4(v, v, v, 1.0);
        // Zero out emissive too so post-process bloom doesn't dump glow
        // on top of the depth gradient, which would otherwise bias the
        // visual reading when a moviepack with bloom is active.
        WRITE_EMISSIVE(vec4(0.0));
#endif
    }
}
