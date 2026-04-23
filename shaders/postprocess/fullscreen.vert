#version 450
// Fullscreen triangle vertex shader for post-processing.
// Generates a single oversized triangle covering the viewport from just
// gl_VertexIndex — no vertex buffer required. Draw with vkCmdDraw(cmd, 3, 1, 0, 0).
layout(location = 0) out vec2 vTexCoord;

void main()
{
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    vTexCoord = pos;
}
