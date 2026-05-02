#version 450

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
    mat4 uTexMatrix;
    mat4 uNormalMatrix;
} pc;

layout(set = 1, binding = 0) uniform LightingUBO {
    vec4 lightPos[2];
    vec4 lightDiffuse[2];
    vec4 lightSpecular[2];
    vec4 materialDiffuse;
    vec4 materialSpecular;
    int  lightingEnabled;
    int  shadowEnabled;
    // 8 bytes implicit std140 padding
    vec4 arenaBBox;
    mat4 shadowVP[2];
} lighting;

// Vertex inputs — same locations for both pipeline variants:
//   Unlit (rVertex20,   20b): loc0=pos(f32x3, off 0), loc1=color(u8x4,  off 12), loc2=texcoord(i16x2, off 16)
//   Lit   (rVertexLit32,32b): loc0=pos(f32x3, off 0), loc1=normal(i8x4, off 12), loc2=texcoord(i16x2, off 20)
//
// loc2 always carries texcoord; the byte offset differs between pipelines.
// loc1 carries color (unlit) or packed normal (lit). Color for lit meshes is
// always white — no dedicated color attribute needed in the lit pipeline.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aSlot1;    // color (unlit) | packed normal (lit, SNORM)
layout(location = 2) in vec2 aTexCoord; // texcoord (both paths, pipeline selects byte offset)

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec3 vModelPos;

void main()
{
    // Pass object-space position to the fragment shader.
    // For non-transformed geometry (floor, walls, zones) this IS world-space;
    // for cycles it's local to the cycle — hook authors can branch on the
    // render context ID if they need to distinguish.
    vModelPos = aPosition;

    // The GL→Vulkan depth remap + reverse-Z is baked into the projection
    // matrix (see VkClipReverseZ in rVulkanRender.cpp). Pipelines pair
    // this with depthCompareOp = GREATER_OR_EQUAL. The Y-flip is handled
    // by the rasterizer via VK_KHR_maintenance1 negative-height viewport
    // (see VkViewport setup in rVulkanRender.cpp), so the shader just
    // outputs GL-style (Y-up) clip coords directly.
    gl_Position = pc.uMVP * vec4(aPosition, 1.0);

    // Lighting flag in push constant uTexMatrix[2][3] — per-draw, not shared UBO
    if (pc.uTexMatrix[2][3] > 0.5)
    {
        // Lit path: aSlot1 = packed normal (i8x4 SNORM at byte 12)
        //           aTexCoord = texcoord (i16x2 SNORM at byte 20, skipping the color at 16)
        // Normal matrix is per-draw (push constant), not per-frame (UBO),
        // so each model part (body, wheels) gets its own correct transform.
        vNormal = mat3(pc.uNormalMatrix) * aSlot1.xyz;
        vColor = vec4(1.0);
        vTexCoord = aTexCoord;
    }
    else
    {
        // Unlit path: aSlot1 = color (u8x4 UNORM at byte 12)
        //             aTexCoord = texcoord (i16x2 SNORM at byte 16)
        vColor = aSlot1;
        vTexCoord = aTexCoord;
        vNormal = vec3(0.0, 0.0, 1.0);
    }

    // SDF font mode: texMatrix[3][3] is stored as -fontMode (always negative).
    if (pc.uTexMatrix[3][3] < 0.0)
    {
        // SDF mode: skip full matrix transform but apply UV scale if provided.
        // texMatrix[3][0] and [3][1] store UV scale factors for textured SDF
        // (0 = no scale, use coords as-is — e.g. font atlas coords).
        float scaleU = pc.uTexMatrix[3][0];
        float scaleV = pc.uTexMatrix[3][1];
        if (scaleU != 0.0) vTexCoord.x *= scaleU;
        if (scaleV != 0.0) vTexCoord.y *= scaleV;
    }
    else
    {
        vec4 tc = pc.uTexMatrix * vec4(vTexCoord, 0.0, 1.0);
        vTexCoord = tc.xy / tc.w;
    }
}
