#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 viewProj;
    vec4 lightPos;
    vec4 cameraPos;
    vec4 lightAmbient;
    vec4 lightDiffuse;
    vec4 lightSpecular;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler[10];

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

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 lightVector = ubo.lightPos.xyz - fragWorldPos;
    float lightDistance = max(length(lightVector), 0.001);
    vec3 L = lightVector / lightDistance;
    float diff = max(dot(N, L), 0.0);
    float attenuation = 1.0 / (1.0 + 0.00035 * lightDistance * lightDistance);
    vec3 color = (0.12 + 0.88 * diff * attenuation) * fragColor.rgb;
    float alphaSample = texture(texSampler[pushData.textureIndex], fragUV).r;
    float alpha = fragColor.a * mix(1.0, alphaSample, float(pushData.useTexture));
    outColor = vec4(color, alpha);
}
