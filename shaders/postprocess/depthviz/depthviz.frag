#version 450
// Depth buffer visualization overlay (diagnostic post-process effect).
//
// Draws the scene normally, then composites a small grayscale rectangle in
// the top-right corner showing the current depth buffer contents. Useful
// for diagnosing depth precision / ordering bugs — see whether the z-buffer
// actually resolves distinct values for the geometry in view, or whether it
// has collapsed to a flat slab.
//
// Activate:     POST_PROCESS_ENABLED 1
//               POST_PROCESS_EFFECT  depthviz
//
// Tunables (see depthviz.meta):
//   FP(0) size        — overlay width as a fraction of the screen (0..1)
//   FP(1) rawMin      — raw depth value mapped to WHITE (nearest)
//   FP(2) rawMax      — raw depth value mapped to BLACK (farthest)
//   FP(3) border      — border thickness in pixels
//   FP4(4) borderColor — rgba color of the rectangle outline
//   FP(5) gamma       — post-curve (>1 brightens mid, <1 darkens)
//
// Why raw depth instead of linearization? Because we don't know the actual
// projection near/far from the shader side — and in AA they vary per frame
// (zNear is recomputed from wall proximity, zFar from arena extent). A raw
// window is projection-agnostic: pick a [rawMin, rawMax] slice of the [0,1]
// depth buffer, stretch it to full grey range, apply a gamma curve. Typical
// gameplay depth values cluster in [0.7, 0.98] because of perspective — so
// defaults rawMin=0.5 rawMax=0.99 give clear contrast out of the box. Sweep
// rawMin upward to zoom into the far distance; drop rawMax to push very
// close geometry to pure white.

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

// Parameter slot assignments — must match depthviz.meta.
#define P_SIZE         FP(0)
#define P_RAW_MIN      FP(1)
#define P_RAW_MAX      FP(2)
#define P_BORDER_PX    FP(3)
#define P_BORDER_COLOR FP4(4)
#define P_GAMMA        FP(5)

void main()
{
    // --- Pass 1: base scene color, untouched ---
    vec4 scene = texture(uSceneColor, vTexCoord);

    // --- Overlay rectangle in normalized screen space [0,1] ---
    // Top-right corner. vTexCoord.y = 0 at the top of the screen.
    float size = clamp(P_SIZE, 0.05, 0.9);
    float aspect = pc.uResolution.x / max(pc.uResolution.y, 1.0);
    // Preserve the scene aspect ratio so the thumbnail is not squashed.
    vec2  rectSize = vec2(size, size * aspect);
    vec2  rectMin  = vec2(1.0 - rectSize.x, 0.0);
    vec2  rectMax  = vec2(1.0, rectSize.y);

    if (vTexCoord.x < rectMin.x || vTexCoord.y > rectMax.y)
    {
        fragColor = scene;
        return;
    }

    // --- Inside the overlay: remap to full-screen UV and sample raw depth ---
    vec2 localUV = (vTexCoord - rectMin) / rectSize;   // 0..1 within the overlay
    float rawDepth = texture(uSceneDepth, localUV).r;  // already in [0, 1]

    // --- Contrast window: stretch [rawMin, rawMax] to full grey range ---
    // Near surfaces (raw ≈ rawMin) → white. Far surfaces (raw ≈ rawMax) → black.
    float rawMin = clamp(P_RAW_MIN, 0.0, 0.9999);
    float rawMax = clamp(P_RAW_MAX, rawMin + 0.0001, 1.0);
    float t = clamp((rawDepth - rawMin) / (rawMax - rawMin), 0.0, 1.0);
    float grey = 1.0 - t;

    // --- Optional post-gamma for fine-tuning contrast ---
    float gamma = max(P_GAMMA, 0.001);
    if (abs(gamma - 1.0) > 0.001)
        grey = pow(grey, 1.0 / gamma);

    // --- Thin colored border so the overlay is visible against black scenes ---
    float borderPx = max(P_BORDER_PX, 0.0);
    vec2  texel    = 1.0 / max(pc.uResolution, vec2(1.0));
    vec2  distFromMin = (vTexCoord - rectMin) / texel;
    vec2  distFromMax = (rectMax - vTexCoord) / texel;
    float edgeDist = min(min(distFromMin.x, distFromMin.y),
                          min(distFromMax.x, distFromMax.y));
    bool  onBorder = edgeDist < borderPx;

    if (onBorder)
    {
        fragColor = P_BORDER_COLOR;
    }
    else
    {
        fragColor = vec4(grey, grey, grey, 1.0);
    }
}
