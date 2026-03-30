#version 330 core
in vec3 v_position;
in vec3 v_normal;
in vec2 v_texcoord;

uniform vec3 u_light_pos;
uniform vec3 u_light_diffuse;
uniform vec3 u_material_color;
uniform float u_alpha;
uniform sampler2D u_alpha_texture;
uniform float u_use_alpha_texture;

out vec4 frag_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 light_vector = u_light_pos - v_position;
    float light_distance = max(length(light_vector), 0.001);
    vec3 L = light_vector / light_distance;
    float diff = max(dot(N, L), 0.0);
    float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);
    vec3 color = (0.12 + 0.88 * diff * attenuation) * u_material_color;
    float alpha_sample = texture(u_alpha_texture, v_texcoord).r;
    float alpha = u_alpha * mix(1.0, alpha_sample, u_use_alpha_texture);
    frag_color = vec4(color, alpha);
}
