#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct StarIn {
    float3 position [[attribute(0)]];
    float brightness [[attribute(1)]];
};

struct OrbitIn {
    float3 position [[attribute(0)]];
};

struct VertexUniforms {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 normal_matrix_col0;
    float4 normal_matrix_col1;
    float4 normal_matrix_col2;
    float4 point_params;
};

struct PlanetFragmentUniforms {
    float4 light_pos;
    float4 camera_pos;
    float4 light_ambient;
    float4 light_diffuse;
    float4 light_specular;
    float4 material_ambient;
    float4 material_diffuse;
    float4 material_specular;
    float4 params;
};

struct RingFragmentUniforms {
    float4 light_pos;
    float4 light_diffuse;
    float4 material_color_alpha;
    float4 params;
};

struct OrbitFragmentUniforms {
    float4 color;
};

struct PlanetVaryings {
    float4 position [[position]];
    float3 world_position;
    float3 normal;
    float2 uv;
};

struct StarVaryings {
    float4 position [[position]];
    float brightness;
    float point_size [[point_size]];
};

vertex PlanetVaryings planet_vertex(VertexIn in [[stage_in]], constant VertexUniforms &u [[buffer(1)]]) {
    PlanetVaryings out;
    float4 world_pos = u.model * float4(in.position, 1.0);
    out.position = u.projection * u.view * world_pos;
    out.world_position = world_pos.xyz;
    float3x3 normal_matrix = float3x3(u.normal_matrix_col0.xyz, u.normal_matrix_col1.xyz, u.normal_matrix_col2.xyz);
    out.normal = normalize(normal_matrix * in.normal);
    out.uv = in.uv;
    return out;
}

fragment float4 planet_fragment(PlanetVaryings in [[stage_in]],
                                constant PlanetFragmentUniforms &u [[buffer(0)]],
                                texture2d<float> tex [[texture(0)]],
                                sampler tex_sampler [[sampler(0)]]) {
    float3 N = normalize(in.normal);
    float3 light_vector = u.light_pos.xyz - in.world_position;
    float light_distance = max(length(light_vector), 0.001);
    float3 L = light_vector / light_distance;
    float3 V = normalize(u.camera_pos.xyz - in.world_position);
    float3 R = reflect(-L, N);
    float3 sampled = tex.get_width() > 0 ? tex.sample(tex_sampler, in.uv).rgb : float3(1.0);
    float3 texture_albedo = sampled * u.material_diffuse.xyz;
    float3 base_color = mix(u.material_diffuse.xyz, texture_albedo, u.params.z);
    float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);
    float3 ambient = u.light_ambient.xyz * mix(u.material_ambient.xyz, base_color, u.params.z);
    float ndotl = max(dot(N, L), 0.0);
    float sunlight = smoothstep(0.08, 0.28, ndotl) * ndotl;
    float3 diffuse = sunlight * attenuation * u.light_diffuse.xyz * base_color;
    float spec = sunlight > 0.0 ? pow(max(dot(V, R), 0.0), u.params.x) : 0.0;
    spec *= smoothstep(0.22, 0.55, ndotl);
    float3 specular = spec * attenuation * u.light_specular.xyz * u.material_specular.xyz;
    float3 emission = u.params.y * base_color;
    return float4(ambient + diffuse + specular + emission, 1.0);
}

fragment float4 ring_fragment(PlanetVaryings in [[stage_in]],
                              constant RingFragmentUniforms &u [[buffer(0)]],
                              texture2d<float> alpha_tex [[texture(0)]],
                              sampler alpha_sampler [[sampler(0)]]) {
    float3 N = normalize(in.normal);
    float3 light_vector = u.light_pos.xyz - in.world_position;
    float light_distance = max(length(light_vector), 0.001);
    float3 L = light_vector / light_distance;
    float diff = max(dot(N, L), 0.0);
    float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);
    float3 color = (0.12 + 0.88 * diff * attenuation) * u.material_color_alpha.xyz;
    float alpha_sample = alpha_tex.get_width() > 0 ? alpha_tex.sample(alpha_sampler, in.uv).r : 1.0;
    float alpha = u.material_color_alpha.w * mix(1.0, alpha_sample, u.params.x);
    return float4(color, alpha);
}

vertex StarVaryings star_vertex(StarIn in [[stage_in]], constant VertexUniforms &u [[buffer(1)]]) {
    StarVaryings out;
    out.position = u.projection * u.view * float4(in.position, 1.0);
    out.brightness = in.brightness;
    out.point_size = u.point_params.x;
    return out;
}

fragment float4 star_fragment(StarVaryings in [[stage_in]]) {
    return float4(in.brightness, in.brightness, in.brightness, 1.0);
}

vertex float4 orbit_vertex(OrbitIn in [[stage_in]], constant VertexUniforms &u [[buffer(1)]]) {
    return u.projection * u.view * float4(in.position, 1.0);
}

fragment float4 orbit_fragment(constant OrbitFragmentUniforms &u [[buffer(0)]]) {
    return u.color;
}
