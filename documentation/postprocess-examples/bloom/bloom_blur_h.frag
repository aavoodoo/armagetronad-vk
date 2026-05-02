#version 450
// Horizontal separable Gaussian blur — one of the two passes of the
// bloom pyramid's per-level blur.

#define BLUR_DIRECTION vec2(1.0, 0.0)
#include "bloom_blur.glsl"
