#version 450

// Instanced vertex shader for cycle rendering.
//
// Binding layout is identical to uber.vert so this shader uses the same
// pipeline layout, descriptor sets, and fragment shader (uber.frag).
//
// Push constant contract (differs from uber.vert — caller must set accordingly):
//   pc.uMVP         = Projection × View  (no Model; Model comes per-instance)
//   pc.uTexMatrix   = texture matrix + per-draw flags (identical to uber.vert)
//   pc.uNormalMatrix = View matrix only   (no Model; used to build normal matrix
//                     per instance as mat3(uNormalMatrix * instanceModel))
//
// Per-instance data (second vertex buffer, VK_VERTEX_INPUT_RATE_INSTANCE):
//   location 4-7: model matrix columns (4 × vec4 = mat4)
//   location 8:   instance color (team color, RGBA)
//
// Vertex data layout: rVertexLit32 (32 bytes, VK_VERTEX_INPUT_RATE_VERTEX)
//   location 0: position  (vec3,  offset  0, R32G32B32_SFLOAT)
//   location 1: normal    (vec4,  offset 12, R8G8B8A8_SNORM)
//   location 2: texcoord  (vec2,  offset 20, R16G16_SNORM)
//
// Normal correctness: cycles are rigid-body transforms (rotation + translation),
// so transpose(inverse(mat3(M))) == mat3(M). We use mat3(V*M) directly, which
// is equivalent to the full normal matrix at zero extra cost.

layout(push_constant) uniform PushConstants {
    mat4 uMVP;        // Projection × View  (caller fills VP, not MVP)
    mat4 uTexMatrix;  // Texture matrix + per-draw flags (see rVulkanRender.h)
    mat4 uNormalMatrix; // View matrix  (caller fills V, not MV)
} pc;

layout(set = 1, binding = 0) uniform LightingUBO {
    vec4 lightPos[2];
    vec4 lightDiffuse[2];
    vec4 lightSpecular[2];
    vec4 materialDiffuse;
    vec4 materialSpecular;
    int  lightingEnabled;
    vec4 arenaBBox;
} lighting;

// Per-vertex (rVertexLit32)
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aSlot1;    // packed normal (i8x4 SNORM)
layout(location = 2) in vec2 aTexCoord;

// Per-instance (model matrix as 4 columns + team color)
layout(location = 4) in vec4 aModelCol0;
layout(location = 5) in vec4 aModelCol1;
layout(location = 6) in vec4 aModelCol2;
layout(location = 7) in vec4 aModelCol3;
layout(location = 8) in vec4 aInstanceColor;

// Outputs — same locations as uber.vert so uber.frag is used unchanged
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec3 vModelPos;

void main()
{
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);

    // Full clip-space position: P * V * M * pos
    vec4 clipPos = pc.uMVP * model * vec4(aPosition, 1.0);

    // Vulkan clip space: Y is inverted vs OpenGL
    clipPos.y = -clipPos.y;
    // Vulkan depth range [0,1]; GLM/OpenGL produces [-w,+w] — remap
    clipPos.z = (clipPos.z + clipPos.w) * 0.5;
    gl_Position = clipPos;

    vModelPos = aPosition;

    // Normal → view space.  mat3(V * M) is correct for rigid-body transforms
    // (rotation + translation only) — no need for the expensive inverse-transpose.
    vNormal = mat3(pc.uNormalMatrix * model) * aSlot1.xyz;

    // Instance color carries the cycle team color; the fragment shader will
    // multiply it against the texture sample (same as vColor in uber.vert).
    vColor = aInstanceColor;

    // Texture coordinates — same logic as uber.vert
    if (pc.uTexMatrix[3][3] < 0.0)
    {
        // SDF font mode sentinel — not applicable to cycle geometry but handled
        // generically for pipeline compatibility
        vTexCoord = aTexCoord;
    }
    else
    {
        vec4 tc = pc.uTexMatrix * vec4(aTexCoord, 0.0, 1.0);
        vTexCoord = tc.xy / tc.w;
    }
}
