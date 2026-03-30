#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 viewProj;
    vec4 lightPos;
    vec4 cameraPos;
    vec4 lightAmbient;
    vec4 lightDiffuse;
    vec4 lightSpecular;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 materialParams1;
    vec4 materialParams2;
    int textureIndex;
    int useTexture;
    int emissive;
    int reserved;
} pushData;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out float fragBrightness;

void main() {
    gl_Position = ubo.viewProj * pushData.model * vec4(inPos, 1.0);
    gl_PointSize = inUV.y;
    fragBrightness = inUV.x;
}
