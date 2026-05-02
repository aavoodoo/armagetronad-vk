#version 450
// Bloom extract pass — sample the full-resolution SCENE_EMISSIVE attachment
// and write a half-resolution bright-pass result. The input is already
// per-component "what should glow" from the uber shader's emissive hooks,
// so we don't need a luminance threshold here — a simple threshold of "any
// non-zero alpha" is enough, with an intensity multiplier for tuning.
//
// Downsamples 2x via 4-tap bilinear average (standard 2x2 box filter). The
// bilinear samples are offset by half a source texel to get the 4 source
// pixels in one vec4 read each.

layout(set = 0, binding = 0) uniform sampler2D uSceneEmissive;
layout(set = 0, binding = 1) uniform sampler2D uUnused; // SCENE_DEPTH placeholder

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;  // target (half res) resolution
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Parameter slot assignments (must match bloom.lua):
//   FP(0) = intensity   (multiplier applied to the extracted glow)
//   FP(1) = threshold   (alpha cutoff from emissive attachment)
#define P_INTENSITY FP(0)
#define P_THRESHOLD FP(1)

void main()
{
    // Source texel size (full res, 2x our target resolution).
    vec2 srcTexel = 0.5 / pc.uResolution;

    // 4-tap bilinear box filter — samples 4 source pixels in one tap each
    // with bilinear offsets that average 2x2 groups.
    vec4 s0 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2(-1.0, -1.0));
    vec4 s1 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2( 1.0, -1.0));
    vec4 s2 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2(-1.0,  1.0));
    vec4 s3 = texture(uSceneEmissive, vTexCoord + srcTexel * vec2( 1.0,  1.0));
    vec4 avg = (s0 + s1 + s2 + s3) * 0.25;

    // Bright pass: multiply by intensity and threshold alpha. The emissive
    // attachment's alpha already encodes "how much this pixel should glow"
    // (from the uber shader's max-channel derivation), so the threshold
    // here just removes near-black noise.
    float gate = smoothstep(P_THRESHOLD, P_THRESHOLD + 0.05, avg.a);
    fragColor = vec4(avg.rgb * gate * P_INTENSITY, avg.a * gate);
}
