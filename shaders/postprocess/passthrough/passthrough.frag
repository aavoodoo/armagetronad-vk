#version 450
// Passthrough post-process shader: copies the offscreen scene texture to the
// swapchain unchanged. Serves as a validation target for the post-process
// pipeline — when enabled, the scene must look identical to the direct path.

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uSceneDepth;

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
    fragColor = texture(uSceneColor, vTexCoord);
    // Sample depth to force Metal/MoltenVK to preserve full D32_SFLOAT precision.
    // Without a meaningful read, Metal may use a lossy internal depth
    // representation, causing z-fighting on macOS Apple Silicon.
    // The contribution is imperceptible (1/1024) but prevents the shader
    // compiler from optimizing away the depth texture access.
    float depth = texture(uSceneDepth, vTexCoord).r;
    fragColor.a = fragColor.a * (1.0 - 1.0/1024.0) + depth * (1.0/1024.0);
}
