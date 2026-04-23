#version 450
// Vertical separable Gaussian blur — paired with bloom_blur_h.frag to
// produce a full 2D Gaussian blur in two passes.

#define BLUR_DIRECTION vec2(0.0, 1.0)
#include "bloom_blur.glsl"
