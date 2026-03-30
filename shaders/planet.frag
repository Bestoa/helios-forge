#version 330 core
in vec3 v_position;
in vec3 v_normal;
in vec2 v_texcoord;

uniform vec3 u_light_pos;
uniform vec3 u_camera_pos;
uniform vec3 u_light_ambient;
uniform vec3 u_light_diffuse;
uniform vec3 u_light_specular;

uniform vec3 u_material_ambient;
uniform vec3 u_material_diffuse;
uniform vec3 u_material_specular;
uniform float u_material_shininess;
uniform float u_emission;
uniform sampler2D u_texture;
uniform float u_texture_mix;

out vec4 frag_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 light_vector = u_light_pos - v_position;
    float light_distance = max(length(light_vector), 0.001);
    vec3 L = light_vector / light_distance;
    vec3 V = normalize(u_camera_pos - v_position);
    vec3 R = reflect(-L, N);
    vec3 sampled_color = texture(u_texture, v_texcoord).rgb;
    vec3 texture_albedo = sampled_color * u_material_diffuse;
    vec3 base_color = mix(u_material_diffuse, texture_albedo, u_texture_mix);
    float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);

    vec3 ambient = u_light_ambient * mix(u_material_ambient, base_color, u_texture_mix);

    float ndotl = max(dot(N, L), 0.0);
    float sunlight = smoothstep(0.08, 0.28, ndotl) * ndotl;
    vec3 diffuse = sunlight * attenuation * u_light_diffuse * base_color;

    float spec = sunlight > 0.0 ? pow(max(dot(V, R), 0.0), u_material_shininess) : 0.0;
    spec *= smoothstep(0.22, 0.55, ndotl);
    vec3 specular = spec * attenuation * u_light_specular * u_material_specular;

    vec3 emission = u_emission * base_color;

    vec3 result = ambient + diffuse + specular + emission;
    frag_color = vec4(result, 1.0);
}
