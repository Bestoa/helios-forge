#version 450

layout(location = 0) in float fragBrightness;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) {
        discard;
    }
    float falloff = exp(-2.8 * r2);
    vec3 color = vec3(0.78, 0.86, 1.0) * fragBrightness * (0.65 + 0.35 * falloff);
    outColor = vec4(color, falloff);
}
