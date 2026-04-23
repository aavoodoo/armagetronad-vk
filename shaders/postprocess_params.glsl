// Shared parameter block for post-process fragment shaders.
// #include this file and access parameters via FP(slot) and IP(slot).
//
// The C++ side writes parameter defaults via rPostProcessPostProcess::SetActiveEffect
// into a std140-packed UBO. Because std140 pads scalar array elements to 16
// bytes (a `float fparams[32]` would waste 384 bytes), we store the params
// as vec4/ivec4 arrays instead and expose linear scalar indexing via the
// FP / IP macros below. Shader authors see `FP(7)` and get the 8th float
// slot — no awareness of the vec4 packing required.

layout(std140, set = 1, binding = 0) uniform PostProcessParams {
    vec4  fparams[8];   // 32 float slots, linear
    ivec4 iparams[4];   // 16 int slots, linear
    vec4  _reserved[4]; // reserved for future use
} pp;

#define FP(slot)  pp.fparams[(slot) >> 2][(slot) & 3]
#define IP(slot)  pp.iparams[(slot) >> 2][(slot) & 3]

// Convenience: read 4 consecutive float slots as a vec4 (for colors, etc).
#define FP4(slot) pp.fparams[(slot) >> 2]
