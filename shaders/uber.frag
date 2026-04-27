#version 450

// Debug: set to 1 to visualize depth buffer as grayscale.
// Closer objects = bright (near z≈1 reversed-Z), farther = dark (z≈0).
#define DEBUG_DEPTH_VIZ 0

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 0) uniform LightingUBO {
    vec4 lightPos[2];
    vec4 lightDiffuse[2];
    vec4 lightSpecular[2];
    vec4 materialDiffuse;
    vec4 materialSpecular;
    int  lightingEnabled;
    vec4 arenaBBox;   // (minX, minY, maxX, maxY)
} lighting;

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

        // Lighting flag in push constant uTexMatrix[2][3] (per-draw, not shared UBO)
        if (pc.uTexMatrix[2][3] > 0.5)
        {
            // Blinn-Phong lighting matching GL3 formula:
            //   color = texColor * vColor * (ambient + diffuse) + specular
            // vColor acts as effective material (GL_COLOR_MATERIAL behavior).
            // Specular is additive and NOT modulated by texture color.
            vec3 N = normalize(vNormal);
            vec3 V = vec3(0.0, 0.0, 1.0);  // infinite viewer (+Z toward camera in view space)

            // Per-draw material color lives in the 4th column of normalMatrix
            // (push constant = per-draw, unlike UBO which is per-frame and
            // would make all cycles share the last-written color).
            vec3 matColor = pc.uNormalMatrix[3].rgb;

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

            fragColor.rgb = texColor.rgb * vColor.rgb * diffuseLight + specularLight;
            fragColor.a = texColor.a * vColor.a;
        }
        else
        {
            // Normal unlit rendering: vertex color * texture
            fragColor = vColor * texColor;
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
        switch (uRenderContext)
        {
            case 7:  em = hookCycleWallEmissive(fragColor, vTexCoord, vModelPos); break;
            case 8:  em = hookCycleEmissive    (fragColor, vTexCoord, vModelPos, vNormal); break;
            case 9:  em = hookZoneEmissive     (fragColor, vTexCoord, vModelPos); break;
            default: break;
        }
        float emMax = max(max(em.r, em.g), em.b);
        WRITE_EMISSIVE(vec4(em, clamp(emMax, 0.0, 1.0)));

        // Alpha-test flag in uTexMatrix[2][2] — only discard when explicitly requested
        if (pc.uTexMatrix[2][2] > 0.5 && fragColor.a < 0.01) discard;

#if DEBUG_DEPTH_VIZ
        // Overwrite color with depth for visualization.
        // Reversed-Z: z≈1 = near (bright), z≈0 = far (dark).
        // Amplify low values so differences at gameplay distance are visible.
        float d = gl_FragCoord.z;
        fragColor = vec4(d, d, d, 1.0);
#endif
    }
}
