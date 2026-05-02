#version 450
// Bloom composite pass — additively combine the bloom pyramid over the
// original scene color and output to the swapchain.

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomHalf;
layout(set = 0, binding = 2) uniform sampler2D uBloomQuarter;
layout(set = 0, binding = 3) uniform sampler2D uUnused;

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Parameter slot assignments (must match bloom.lua):
//   FP(0) = intensity            (duplicated from extract; final multiplier)
//   FP(1) = threshold            (from extract pass)
//   FP(2) = radius               (from blur passes)
//   FP(3) = half_weight          (how much the 1/2 res level contributes)
//   FP4(4) = tint                (vec4 color tint applied to combined bloom)
#define P_INTENSITY  FP(0)
#define P_HALF       FP(3)
#define P_TINT       FP4(4)

void main()
{
    vec4 base = texture(uSceneColor, vTexCoord);

    // Sum the two pyramid levels. Bilinear sampling on the lower-res levels
    // automatically interpolates them up to the target resolution — we're
    // sampling the same UV coordinates, just reading from smaller textures.
    vec3 bloomHalf    = texture(uBloomHalf,    vTexCoord).rgb;
    vec3 bloomQuarter = texture(uBloomQuarter, vTexCoord).rgb;

    // The half-res level gives the tight halo around bright objects; the
    // quarter-res gives the broader outer glow. Mix them with the user's
    // half_weight balance (default 0.6), then tint and apply intensity.
    float halfWeight = clamp(P_HALF, 0.0, 1.0);
    vec3 bloom = bloomHalf * halfWeight + bloomQuarter * (1.0 - halfWeight);

    vec4 tint = P_TINT;
    fragColor = vec4(base.rgb + bloom * tint.rgb * P_INTENSITY, base.a);
}
