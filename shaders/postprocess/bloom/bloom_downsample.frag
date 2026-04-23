#version 450
// Bloom downsample pass — 4-tap box filter from a larger source into a
// half-sized target. Used to build the next level of the bloom pyramid.

layout(set = 0, binding = 0) uniform sampler2D uInput;
layout(set = 0, binding = 1) uniform sampler2D uUnused;

#include "postprocess_params.glsl"

layout(push_constant) uniform PushConstants {
    vec2  uResolution;
    float uTime;
    float _pad;
    vec4  uArenaBBox;
} pc;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Source texel is 1/target_size / 2 (source is 2x target in each dim).
    vec2 srcTexel = 0.5 / pc.uResolution;

    vec4 s0 = texture(uInput, vTexCoord + srcTexel * vec2(-1.0, -1.0));
    vec4 s1 = texture(uInput, vTexCoord + srcTexel * vec2( 1.0, -1.0));
    vec4 s2 = texture(uInput, vTexCoord + srcTexel * vec2(-1.0,  1.0));
    vec4 s3 = texture(uInput, vTexCoord + srcTexel * vec2( 1.0,  1.0));
    fragColor = (s0 + s1 + s2 + s3) * 0.25;
}
