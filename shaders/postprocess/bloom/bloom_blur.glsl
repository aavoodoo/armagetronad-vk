// Shared body for the separable Gaussian bloom blur.
// Include from bloom_blur_h.frag / bloom_blur_v.frag with BLUR_DIRECTION
// #defined to vec2(1, 0) or vec2(0, 1) respectively.

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

// FP(2) = blur radius scale (1.0 = standard, 2.0 = wider glow)
#define P_RADIUS FP(2)

void main()
{
    // 9-tap separable Gaussian. Weights normalized to sum to 1.
    const float weights[9] = float[](
        0.0162, 0.0540, 0.1216, 0.1946, 0.2270,
        0.1946, 0.1216, 0.0540, 0.0162
    );
    const float offsets[9] = float[](
        -4.0, -3.0, -2.0, -1.0,  0.0,
         1.0,  2.0,  3.0,  4.0
    );

    // Step size in UV space. Multiplied by P_RADIUS so the user can widen
    // the halo without changing the kernel size.
    vec2 step = (BLUR_DIRECTION / pc.uResolution) * max(P_RADIUS, 0.5);

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; ++i)
    {
        sum += texture(uInput, vTexCoord + step * offsets[i]) * weights[i];
    }
    fragColor = sum;
}
