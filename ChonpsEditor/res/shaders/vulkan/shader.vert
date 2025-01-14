#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out flat int fragTexIndex;

struct ObjectData
{
	mat4 camMatrix;
    int texIndex;
};

layout(std140, set = 0, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} ob;

void main()
{
    gl_Position = ob.objects[gl_InstanceIndex].camMatrix * vec4(inPos, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragTexIndex = ob.objects[gl_InstanceIndex].texIndex;
}