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

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec2 fragUV;

void main() {
    vec4 worldPos = pushData.model * vec4(inPos, 1.0);
    gl_Position = ubo.viewProj * worldPos;
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(pushData.model) * inNormal;
    fragColor = pushData.color;
    fragUV = inUV;
}
