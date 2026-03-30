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

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 sampledColor = vec3(1.0);
    if (pushData.useTexture != 0) {
        sampledColor = texture(texSampler[pushData.textureIndex], fragUV).rgb;
    }
    vec3 textureAlbedo = sampledColor * fragColor.rgb;
    vec3 baseColor = mix(fragColor.rgb, textureAlbedo, float(pushData.useTexture));

    if (pushData.emissive != 0) {
        outColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 lightVector = ubo.lightPos.xyz - fragWorldPos;
    float lightDistance = max(length(lightVector), 0.001);
    vec3 L = lightVector / lightDistance;
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 R = reflect(-L, N);
    float attenuation = 1.0 / (1.0 + 0.00035 * lightDistance * lightDistance);

    vec3 ambientColor = pushData.materialParams1.rgb;
    float shininess = pushData.materialParams1.a;
    vec3 specularColor = pushData.materialParams2.rgb;
    vec3 ambient = ubo.lightAmbient.rgb * mix(ambientColor, baseColor, float(pushData.useTexture));
    float ndotl = max(dot(N, L), 0.0);
    float sunlight = smoothstep(0.08, 0.28, ndotl) * ndotl;
    vec3 diffuse = sunlight * attenuation * ubo.lightDiffuse.rgb * baseColor;

    float spec = sunlight > 0.0 ? pow(max(dot(V, R), 0.0), shininess) : 0.0;
    spec *= smoothstep(0.22, 0.55, ndotl);
    vec3 specular = spec * attenuation * ubo.lightSpecular.rgb * specularColor;

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, 1.0);
}
