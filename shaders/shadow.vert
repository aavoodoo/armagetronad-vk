#version 450

layout(push_constant) uniform PushConstants {
    mat4 uLightVP;
} pc;

// Position from rVertex20 (only field needed for shadow depth)
layout(location = 0) in vec3 aPosition;

void main()
{
    gl_Position = pc.uLightVP * vec4(aPosition, 1.0);
}
