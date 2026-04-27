#version 450

// Minimal fragment shader for MoltenVK compatibility.
// Some Metal drivers require a fragment shader even for depth-only passes.
// No color output (depth-only render pass has 0 color attachments).
void main()
{
    // Depth is written automatically by the fixed-function depth test.
}
