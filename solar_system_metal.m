#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <simd/simd.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define MAX_PATH_LEN 1024

typedef struct {
    float m[16];
} Mat4;

typedef struct {
    float m[9];
} Mat3;

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    Vec3 periapsis_dir;
    Vec3 minor_dir;
} OrbitFrame;

typedef struct {
    id<MTLBuffer> vertex_buffer;
    id<MTLBuffer> index_buffer;
    uint32_t index_count;
    uint32_t vertex_count;
    MTLPrimitiveType primitive;
} Mesh;

typedef struct {
    char name[16];
    float orbit_radius;
    float orbit_eccentricity;
    float orbit_inclination_deg;
    float orbit_ascending_node_deg;
    float orbit_periapsis_deg;
    float orbit_days;
    float orbit_mean_anomaly_deg;
    float size;
    Vec3 color;
    float axial_tilt_deg;
    float rotation_period_days;
    float rotation_angle_deg;
    bool has_ring;
    Vec3 ring_color;
    float ring_inner;
    float ring_outer;
    OrbitFrame orbit_frame;
    char texture_name[64];
    id<MTLTexture> texture;
} Planet;

typedef struct {
    float move_speed;
    float mouse_sensitivity;
    float pitch_deg;
    float yaw_deg;
    bool dragging;
    double last_mouse_x;
    double last_mouse_y;
    float zoom_delta;
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    Mat4 view_matrix;
} Camera;

typedef struct {
    Planet planets[8];
    float sun_radius;
    Vec3 sun_color;
    float sun_rotation_period_days;
    float sun_rotation_angle_deg;
    id<MTLTexture> sun_texture;
    id<MTLTexture> ring_alpha_texture;
    Mesh sphere_mesh;
    Mesh sun_mesh;
    Mesh ring_mesh;
    Mesh star_mesh;
    Mesh orbit_meshes[8];
} SolarSystem;

typedef struct {
    const char *frame_output;
    int max_frames;
    bool has_max_frames;
} Args;

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
} Vertex;

typedef struct {
    float position[3];
    float brightness;
} StarVertex;

typedef struct {
    float model[16];
    float view[16];
    float projection[16];
    float normal_matrix_col0[4];
    float normal_matrix_col1[4];
    float normal_matrix_col2[4];
    float point_params[4];
} VertexUniforms;

typedef struct {
    float light_pos[4];
    float camera_pos[4];
    float light_ambient[4];
    float light_diffuse[4];
    float light_specular[4];
    float material_ambient[4];
    float material_diffuse[4];
    float material_specular[4];
    float params[4];
} PlanetFragmentUniforms;

typedef struct {
    float light_pos[4];
    float light_diffuse[4];
    float material_color_alpha[4];
    float params[4];
} RingFragmentUniforms;

typedef struct {
    float color[4];
} OrbitFragmentUniforms;

typedef struct {
    bool w;
    bool a;
    bool s;
    bool d;
    bool space;
    bool plus;
    bool minus;
    bool lock_toggle;
    bool escape;
    bool digits[10];
    bool mouse_down;
    double mouse_x;
    double mouse_y;
    float scroll_delta;
} InputState;

typedef struct {
    NSWindow *window;
    NSView *view;
    bool should_close;

    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLLibrary> shader_library;
    id<MTLRenderPipelineState> planet_pipeline;
    id<MTLRenderPipelineState> ring_pipeline;
    id<MTLRenderPipelineState> star_pipeline;
    id<MTLRenderPipelineState> orbit_pipeline;
    id<MTLDepthStencilState> depth_state;
    id<MTLDepthStencilState> depth_no_write_state;
    id<MTLSamplerState> repeat_sampler;
    id<MTLSamplerState> clamp_sampler;
    CAMetalLayer *metal_layer;
    id<MTLTexture> depth_texture;

    CGSize drawable_size;
    float backing_scale;

    Args args;
    char base_dir[MAX_PATH_LEN];

    Camera camera;
    SolarSystem solar_system;
    InputState input;
    bool prev_space;
    bool prev_plus;
    bool prev_minus;
    bool prev_lock;
    bool prev_digits[10];
    bool paused;
    bool camera_locked;
    float speed;
    int target_index;
    int frame_count;
    float fps;
    double last_time;
    double fps_timer;
    int fps_frames;
} MetalApp;

static MetalApp *g_app = NULL;

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static float randf_range(float min_v, float max_v) {
    return min_v + (max_v - min_v) * ((float) rand() / (float) RAND_MAX);
}

static Vec3 vec3(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

static Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static Vec3 vec3_scale(Vec3 a, float s) {
    return vec3(a.x * s, a.y * s, a.z * s);
}

static float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static float vec3_length(Vec3 a) {
    return sqrtf(vec3_dot(a, a));
}

static Vec3 vec3_normalize(Vec3 a) {
    float len = vec3_length(a);
    if (len < 1e-6f) {
        return vec3(0.0f, 0.0f, 0.0f);
    }
    return vec3_scale(a, 1.0f / len);
}

static Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

static Mat4 mat4_mul(Mat4 a, Mat4 b) {
    Mat4 out = {0};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[row * 4 + k] * b.m[k * 4 + col];
            }
            out.m[row * 4 + col] = sum;
        }
    }
    return out;
}

static Mat4 mat4_translate(Vec3 v) {
    Mat4 m = mat4_identity();
    m.m[3] = v.x;
    m.m[7] = v.y;
    m.m[11] = v.z;
    return m;
}

static Mat4 mat4_scale(float sx, float sy, float sz) {
    Mat4 m = mat4_identity();
    m.m[0] = sx;
    m.m[5] = sy;
    m.m[10] = sz;
    return m;
}

static Mat4 mat4_rotate_y(float degrees) {
    float rad = degrees * (float) M_PI / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 m = mat4_identity();
    m.m[0] = c;
    m.m[2] = s;
    m.m[8] = -s;
    m.m[10] = c;
    return m;
}

static Mat4 mat4_rotate_z(float degrees) {
    float rad = degrees * (float) M_PI / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 m = mat4_identity();
    m.m[0] = c;
    m.m[1] = -s;
    m.m[4] = s;
    m.m[5] = c;
    return m;
}

static Mat4 mat4_perspective(float fov_deg, float aspect, float near_z, float far_z) {
    float f = 1.0f / tanf((fov_deg * (float) M_PI / 180.0f) / 2.0f);
    Mat4 m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = far_z / (near_z - far_z);
    m.m[11] = (far_z * near_z) / (near_z - far_z);
    m.m[14] = -1.0f;
    return m;
}

static Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(center, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);
    Mat4 m = mat4_identity();
    m.m[0] = s.x; m.m[1] = s.y; m.m[2] = s.z;
    m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
    m.m[8] = -f.x; m.m[9] = -f.y; m.m[10] = -f.z;
    m.m[3] = -vec3_dot(s, eye);
    m.m[7] = -vec3_dot(u, eye);
    m.m[11] = vec3_dot(f, eye);
    return m;
}

static Mat3 mat3_from_mat4_inverse_transpose(Mat4 m4) {
    float a = m4.m[0], b = m4.m[1], c = m4.m[2];
    float d = m4.m[4], e = m4.m[5], f = m4.m[6];
    float g = m4.m[8], h = m4.m[9], i = m4.m[10];

    float A = e * i - f * h;
    float B = -(d * i - f * g);
    float C = d * h - e * g;
    float D = -(b * i - c * h);
    float E = a * i - c * g;
    float F = -(a * h - b * g);
    float G = b * f - c * e;
    float H = -(a * f - c * d);
    float Iterm = a * e - b * d;

    float det = a * A + b * B + c * C;
    if (fabsf(det) < 1e-8f) {
        Mat3 ident = {.m = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        return ident;
    }

    float inv_det = 1.0f / det;
    Mat3 inv = {.m = {
        A * inv_det, D * inv_det, G * inv_det,
        B * inv_det, E * inv_det, H * inv_det,
        C * inv_det, F * inv_det, Iterm * inv_det
    }};

    Mat3 out = {0};
    out.m[0] = inv.m[0]; out.m[1] = inv.m[3]; out.m[2] = inv.m[6];
    out.m[3] = inv.m[1]; out.m[4] = inv.m[4]; out.m[5] = inv.m[7];
    out.m[6] = inv.m[2]; out.m[7] = inv.m[5]; out.m[8] = inv.m[8];
    return out;
}

static void join_path(char *out, size_t out_size, const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a + 1 + len_b + 1 > out_size) {
        fail("Path buffer too small.");
    }
    memcpy(out, a, len_a);
    out[len_a] = '/';
    memcpy(out + len_a + 1, b, len_b + 1);
}

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    char *buffer = (char *) malloc((size_t) size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if (fread(buffer, 1, (size_t) size, fp) != (size_t) size) {
        fclose(fp);
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static void init_base_dir(char *out, const char *argv0) {
    char resolved[MAX_PATH_LEN];
    if (!realpath(argv0, resolved)) {
        if (!getcwd(resolved, sizeof(resolved))) {
            fail("Failed to determine executable directory.");
        }
    }
    char *slash = strrchr(resolved, '/');
    if (slash) {
        *slash = '\0';
        snprintf(out, MAX_PATH_LEN, "%s", resolved);
    } else {
        snprintf(out, MAX_PATH_LEN, ".");
    }
}

static simd_float4x4 simd_mat4_from_row_major(const Mat4 *m) {
    simd_float4x4 out;
    out.columns[0] = (simd_float4){m->m[0], m->m[4], m->m[8], m->m[12]};
    out.columns[1] = (simd_float4){m->m[1], m->m[5], m->m[9], m->m[13]};
    out.columns[2] = (simd_float4){m->m[2], m->m[6], m->m[10], m->m[14]};
    out.columns[3] = (simd_float4){m->m[3], m->m[7], m->m[11], m->m[15]};
    return out;
}

static void copy_mat4_uniform(float out[16], const Mat4 *m) {
    simd_float4x4 simd_m = simd_mat4_from_row_major(m);
    memcpy(out, &simd_m, sizeof(simd_m));
}

static void copy_mat3_uniform(float out_col0[4], float out_col1[4], float out_col2[4], const Mat3 *m) {
    out_col0[0] = m->m[0]; out_col0[1] = m->m[3]; out_col0[2] = m->m[6]; out_col0[3] = 0.0f;
    out_col1[0] = m->m[1]; out_col1[1] = m->m[4]; out_col1[2] = m->m[7]; out_col1[3] = 0.0f;
    out_col2[0] = m->m[2]; out_col2[1] = m->m[5]; out_col2[2] = m->m[8]; out_col2[3] = 0.0f;
}

static void mesh_init(Mesh *mesh, MTLPrimitiveType primitive) {
    memset((void *) mesh, 0, sizeof(*mesh));
    mesh->primitive = primitive;
}

static void mesh_upload(Mesh *mesh, id<MTLDevice> device, const void *vertices, size_t vertex_bytes, size_t vertex_stride, const uint32_t *indices, size_t index_count) {
    mesh->vertex_buffer = [device newBufferWithBytes:vertices length:vertex_bytes options:MTLResourceStorageModeShared];
    mesh->vertex_count = (uint32_t) (vertex_bytes / vertex_stride);
    if (indices && index_count > 0) {
        mesh->index_buffer = [device newBufferWithBytes:indices length:index_count * sizeof(uint32_t) options:MTLResourceStorageModeShared];
        mesh->index_count = (uint32_t) index_count;
    }
}

static id<MTLTexture> load_texture_2d(id<MTLDevice> device, const char *path, bool srgb) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "Texture load failed: %s\n", path);
        return nil;
    }

    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm) width:(NSUInteger) width height:(NSUInteger) height mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger) width, (NSUInteger) height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:(NSUInteger) width * 4];
    stbi_image_free(pixels);
    return texture;
}

static bool save_texture_png(id<MTLTexture> texture, const char *path) {
    NSUInteger width = texture.width;
    NSUInteger height = texture.height;
    size_t src_stride = width * 4;
    size_t total = src_stride * height;
    uint8_t *bgra = (uint8_t *) malloc(total);
    uint8_t *rgba = (uint8_t *) malloc(total);
    if (!bgra || !rgba) {
        free(bgra);
        free(rgba);
        fprintf(stderr, "Failed to allocate screenshot buffers.\n");
        return false;
    }

    [texture getBytes:bgra bytesPerRow:src_stride fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    for (NSUInteger y = 0; y < height; ++y) {
        size_t src_row = (height - 1 - y) * src_stride;
        size_t dst_row = y * src_stride;
        for (NSUInteger x = 0; x < width; ++x) {
            uint8_t b = bgra[src_row + x * 4 + 0];
            uint8_t g = bgra[src_row + x * 4 + 1];
            uint8_t r = bgra[src_row + x * 4 + 2];
            uint8_t a = bgra[src_row + x * 4 + 3];
            rgba[dst_row + x * 4 + 0] = r;
            rgba[dst_row + x * 4 + 1] = g;
            rgba[dst_row + x * 4 + 2] = b;
            rgba[dst_row + x * 4 + 3] = a;
        }
    }
    int ok = stbi_write_png(path, (int) width, (int) height, 4, rgba, (int) src_stride);
    free(bgra);
    free(rgba);
    return ok != 0;
}

static void create_sphere_mesh(id<MTLDevice> device, float radius, int slices, int stacks, Mesh *mesh) {
    int vertex_count = (stacks + 1) * (slices + 1);
    Vertex *vertices = (Vertex *) calloc((size_t) vertex_count, sizeof(Vertex));
    size_t max_indices = (size_t) stacks * (size_t) slices * 6;
    uint32_t *indices = (uint32_t *) malloc(max_indices * sizeof(uint32_t));
    size_t index_count = 0;

    int v = 0;
    for (int i = 0; i <= stacks; ++i) {
        float phi = (float) M_PI * (float) i / (float) stacks;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float) M_PI * (float) j / (float) slices;
            float sin_theta = sinf(theta);
            float cos_theta = cosf(theta);
            float x = cos_theta * sin_phi;
            float y = cos_phi;
            float z = sin_theta * sin_phi;

            vertices[v].position[0] = radius * x;
            vertices[v].position[1] = radius * y;
            vertices[v].position[2] = radius * z;
            vertices[v].normal[0] = x;
            vertices[v].normal[1] = y;
            vertices[v].normal[2] = z;
            vertices[v].uv[0] = 1.0f - (float) j / (float) slices;
            vertices[v].uv[1] = 1.0f - (float) i / (float) stacks;
            ++v;
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t p1 = (uint32_t) (i * (slices + 1) + j);
            uint32_t p2 = p1 + (uint32_t) slices + 1;
            if (i > 0) {
                indices[index_count++] = p1;
                indices[index_count++] = p1 + 1;
                indices[index_count++] = p2;
            }
            if (i < stacks - 1) {
                indices[index_count++] = p1 + 1;
                indices[index_count++] = p2 + 1;
                indices[index_count++] = p2;
            }
        }
    }

    mesh_init(mesh, MTLPrimitiveTypeTriangle);
    mesh_upload(mesh, device, vertices, (size_t) vertex_count * sizeof(Vertex), sizeof(Vertex), indices, index_count);
    free(vertices);
    free(indices);
}

static void create_ring_mesh(id<MTLDevice> device, float inner_radius, float outer_radius, int segments, Mesh *mesh) {
    int vertex_count = 4 * (segments + 1);
    Vertex *vertices = (Vertex *) calloc((size_t) vertex_count, sizeof(Vertex));
    uint32_t *indices = (uint32_t *) malloc((size_t) segments * 12 * sizeof(uint32_t));
    size_t index_count = 0;
    int vi = 0;

    for (int side_idx = 0; side_idx < 2; ++side_idx) {
        float side = side_idx == 0 ? -1.0f : 1.0f;
        for (int i = 0; i <= segments; ++i) {
            float theta = 2.0f * (float) M_PI * (float) i / (float) segments;
            float c = cosf(theta);
            float s = sinf(theta);

            vertices[vi].position[0] = inner_radius * c;
            vertices[vi].position[1] = 0.0f;
            vertices[vi].position[2] = inner_radius * s;
            vertices[vi].normal[1] = side;
            vertices[vi].uv[0] = 0.0f;
            vertices[vi].uv[1] = (float) i / (float) segments;
            ++vi;

            vertices[vi].position[0] = outer_radius * c;
            vertices[vi].position[1] = 0.0f;
            vertices[vi].position[2] = outer_radius * s;
            vertices[vi].normal[1] = side;
            vertices[vi].uv[0] = 1.0f;
            vertices[vi].uv[1] = (float) i / (float) segments;
            ++vi;
        }

        uint32_t base = side_idx == 0 ? 0 : (uint32_t) (2 * (segments + 1));
        for (int i = 0; i < segments; ++i) {
            uint32_t p1 = base + (uint32_t) (2 * i);
            uint32_t p2 = p1 + 1;
            uint32_t p3 = p1 + 2;
            uint32_t p4 = p1 + 3;
            if (side_idx == 1) {
                indices[index_count++] = p1;
                indices[index_count++] = p2;
                indices[index_count++] = p3;
                indices[index_count++] = p2;
                indices[index_count++] = p4;
                indices[index_count++] = p3;
            } else {
                indices[index_count++] = p2;
                indices[index_count++] = p1;
                indices[index_count++] = p4;
                indices[index_count++] = p1;
                indices[index_count++] = p3;
                indices[index_count++] = p4;
            }
        }
    }

    mesh_init(mesh, MTLPrimitiveTypeTriangle);
    mesh_upload(mesh, device, vertices, (size_t) vertex_count * sizeof(Vertex), sizeof(Vertex), indices, index_count);
    free(vertices);
    free(indices);
}

static float deg_to_rad(float degrees) {
    return degrees * (float) M_PI / 180.0f;
}

static float solve_eccentric_anomaly(float mean_anomaly, float eccentricity) {
    float eccentric_anomaly = eccentricity < 0.8f ? mean_anomaly : (float) M_PI;
    for (int i = 0; i < 6; ++i) {
        float sin_e = sinf(eccentric_anomaly);
        float cos_e = cosf(eccentric_anomaly);
        float f = eccentric_anomaly - eccentricity * sin_e - mean_anomaly;
        float fp = 1.0f - eccentricity * cos_e;
        if (fabsf(fp) < 1e-4f) {
            fp = fp < 0.0f ? -1e-4f : 1e-4f;
        }
        eccentric_anomaly -= f / fp;
    }
    return eccentric_anomaly;
}

static OrbitFrame orbit_frame_from_elements(float ascending_node_deg, float inclination_deg, float periapsis_deg) {
    float node = deg_to_rad(ascending_node_deg);
    float inclination = deg_to_rad(inclination_deg);
    float periapsis = deg_to_rad(periapsis_deg);
    float cos_node = cosf(node);
    float sin_node = sinf(node);
    float cos_inclination = cosf(inclination);
    float sin_inclination = sinf(inclination);
    float cos_periapsis = cosf(periapsis);
    float sin_periapsis = sinf(periapsis);

    OrbitFrame frame;
    frame.periapsis_dir = vec3(
        cos_node * cos_periapsis - sin_node * sin_periapsis * cos_inclination,
        sin_periapsis * sin_inclination,
        sin_node * cos_periapsis + cos_node * sin_periapsis * cos_inclination
    );
    frame.minor_dir = vec3(
        -cos_node * sin_periapsis - sin_node * cos_periapsis * cos_inclination,
        cos_periapsis * sin_inclination,
        -sin_node * sin_periapsis + cos_node * cos_periapsis * cos_inclination
    );
    return frame;
}

static Vec3 orbit_to_world(const Planet *planet, float radius, float true_anomaly) {
    float cos_v = cosf(true_anomaly);
    float sin_v = sinf(true_anomaly);
    return vec3_add(
        vec3_scale(planet->orbit_frame.periapsis_dir, radius * cos_v),
        vec3_scale(planet->orbit_frame.minor_dir, radius * sin_v)
    );
}

static void create_orbit_path(id<MTLDevice> device, const Planet *planet, int segments, Mesh *mesh) {
    int vertex_count = segments + 1;
    simd_float3 *positions = (simd_float3 *) malloc((size_t) vertex_count * sizeof(simd_float3));
    float semi_latus_rectum = planet->orbit_radius * (1.0f - planet->orbit_eccentricity * planet->orbit_eccentricity);
    for (int i = 0; i <= segments; ++i) {
        float true_anomaly = 2.0f * (float) M_PI * (float) i / (float) segments;
        float radius = semi_latus_rectum / (1.0f + planet->orbit_eccentricity * cosf(true_anomaly));
        Vec3 position = orbit_to_world(planet, radius, true_anomaly);
        positions[i] = (simd_float3){position.x, position.y, position.z};
    }
    mesh_init(mesh, MTLPrimitiveTypeLineStrip);
    mesh_upload(mesh, device, positions, (size_t) vertex_count * sizeof(simd_float3), sizeof(simd_float3), NULL, 0);
    free(positions);
}

static void generate_stars(id<MTLDevice> device, int count, float radius, Mesh *mesh) {
    StarVertex *vertices = (StarVertex *) calloc((size_t) count, sizeof(StarVertex));
    srand(7);
    for (int i = 0; i < count; ++i) {
        float theta = randf_range(0.0f, 2.0f * (float) M_PI);
        float phi = randf_range(-(float) M_PI / 2.0f, (float) M_PI / 2.0f);
        vertices[i].position[0] = radius * cosf(phi) * cosf(theta);
        vertices[i].position[1] = radius * sinf(phi);
        vertices[i].position[2] = radius * cosf(phi) * sinf(theta);
        vertices[i].brightness = randf_range(0.4f, 1.0f);
    }
    mesh_init(mesh, MTLPrimitiveTypePoint);
    mesh_upload(mesh, device, vertices, (size_t) count * sizeof(StarVertex), sizeof(StarVertex), NULL, 0);
    free(vertices);
}

static void camera_update_view(Camera *camera) {
    Vec3 world_up = vec3(0.0f, 1.0f, 0.0f);
    float pitch = camera->pitch_deg * (float) M_PI / 180.0f;
    float yaw = camera->yaw_deg * (float) M_PI / 180.0f;
    Vec3 forward = vec3(cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw));
    camera->forward = vec3_normalize(forward);
    camera->right = vec3_normalize(vec3_cross(camera->forward, world_up));
    if (vec3_length(camera->right) < 1e-6f) {
        camera->right = vec3(1.0f, 0.0f, 0.0f);
    }
    camera->up = vec3_normalize(vec3_cross(camera->right, camera->forward));
    if (vec3_length(camera->up) < 1e-6f) {
        camera->up = world_up;
    }
    camera->view_matrix = mat4_look_at(camera->eye, vec3_add(camera->eye, camera->forward), camera->up);
}

static void camera_set_forward(Camera *camera, Vec3 forward) {
    Vec3 world_up = vec3(0.0f, 1.0f, 0.0f);
    if (vec3_length(forward) < 1e-6f) {
        return;
    }
    camera->forward = vec3_normalize(forward);
    camera->yaw_deg = atan2f(camera->forward.z, camera->forward.x) * 180.0f / (float) M_PI;
    camera->pitch_deg = asinf(fmaxf(-1.0f, fminf(1.0f, camera->forward.y))) * 180.0f / (float) M_PI;
    camera->right = vec3_normalize(vec3_cross(camera->forward, world_up));
    if (vec3_length(camera->right) < 1e-6f) {
        camera->right = vec3(1.0f, 0.0f, 0.0f);
    }
    camera->up = vec3_normalize(vec3_cross(camera->right, camera->forward));
    if (vec3_length(camera->up) < 1e-6f) {
        camera->up = world_up;
    }
    camera->view_matrix = mat4_look_at(camera->eye, vec3_add(camera->eye, camera->forward), camera->up);
}

static void camera_look_at_target(Camera *camera, Vec3 target) {
    camera_set_forward(camera, vec3_sub(target, camera->eye));
}

static void camera_init(Camera *camera) {
    memset(camera, 0, sizeof(*camera));
    camera->move_speed = 24.0f;
    camera->mouse_sensitivity = 0.2f;
    camera->pitch_deg = -20.0f;
    camera->yaw_deg = -90.0f;
    camera->eye = vec3(0.0f, 12.0f, 60.0f);
    camera_update_view(camera);
}

static void camera_move(Camera *camera, float forward_amount, float right_amount, float dt) {
    Vec3 movement = vec3_add(vec3_scale(camera->forward, forward_amount), vec3_scale(camera->right, right_amount));
    float len = vec3_length(movement);
    if (len < 1e-6f) {
        return;
    }
    movement = vec3_scale(movement, 1.0f / len);
    camera->eye = vec3_add(camera->eye, vec3_scale(movement, camera->move_speed * dt));
    camera_update_view(camera);
}

static void camera_apply_mouse_drag(Camera *camera, double mouse_x, double mouse_y) {
    if (!camera->dragging) {
        camera->dragging = true;
        camera->last_mouse_x = mouse_x;
        camera->last_mouse_y = mouse_y;
        return;
    }
    double dx = mouse_x - camera->last_mouse_x;
    double dy = mouse_y - camera->last_mouse_y;
    camera->yaw_deg += (float) dx * camera->mouse_sensitivity;
    camera->pitch_deg -= (float) dy * camera->mouse_sensitivity;
    if (camera->pitch_deg > 89.0f) {
        camera->pitch_deg = 89.0f;
    }
    if (camera->pitch_deg < -89.0f) {
        camera->pitch_deg = -89.0f;
    }
    camera->last_mouse_x = mouse_x;
    camera->last_mouse_y = mouse_y;
    camera_update_view(camera);
}

static void camera_apply_zoom(Camera *camera, float zoom_delta) {
    if (fabsf(zoom_delta) < 1e-6f) {
        return;
    }
    camera->eye = vec3_add(camera->eye, vec3_scale(camera->forward, zoom_delta * 2.0f));
    camera_update_view(camera);
}

static void planet_init(
    Planet *planet,
    const char *name,
    float orbit_radius,
    float orbit_eccentricity,
    float orbit_inclination_deg,
    float orbit_ascending_node_deg,
    float orbit_periapsis_deg,
    float size,
    Vec3 color,
    float orbit_days,
    float orbit_phase_deg,
    float axial_tilt_deg,
    float rotation_period_days,
    bool has_ring,
    Vec3 ring_color,
    float ring_inner,
    float ring_outer,
    const char *texture_name
) {
    memset((void *) planet, 0, sizeof(*planet));
    snprintf(planet->name, sizeof(planet->name), "%s", name);
    planet->orbit_radius = orbit_radius;
    planet->orbit_eccentricity = orbit_eccentricity;
    planet->orbit_inclination_deg = orbit_inclination_deg;
    planet->orbit_ascending_node_deg = orbit_ascending_node_deg;
    planet->orbit_periapsis_deg = orbit_periapsis_deg;
    planet->size = size;
    planet->color = color;
    planet->orbit_days = orbit_days;
    planet->orbit_mean_anomaly_deg = orbit_phase_deg;
    planet->axial_tilt_deg = axial_tilt_deg;
    planet->rotation_period_days = rotation_period_days;
    planet->rotation_angle_deg = randf_range(0.0f, 360.0f);
    planet->has_ring = has_ring;
    planet->ring_color = ring_color;
    planet->ring_inner = ring_inner;
    planet->ring_outer = ring_outer;
    planet->orbit_frame = orbit_frame_from_elements(orbit_ascending_node_deg, orbit_inclination_deg, orbit_periapsis_deg);
    snprintf(planet->texture_name, sizeof(planet->texture_name), "%s", texture_name);
}

static Vec3 planet_position(const Planet *planet) {
    float mean_anomaly = deg_to_rad(planet->orbit_mean_anomaly_deg);
    float eccentric_anomaly = solve_eccentric_anomaly(mean_anomaly, planet->orbit_eccentricity);
    float cos_e = cosf(eccentric_anomaly);
    float sin_e = sinf(eccentric_anomaly);
    float radius = planet->orbit_radius * (1.0f - planet->orbit_eccentricity * cos_e);
    float true_anomaly = atan2f(
        sqrtf(1.0f - planet->orbit_eccentricity * planet->orbit_eccentricity) * sin_e,
        cos_e - planet->orbit_eccentricity
    );
    return orbit_to_world(planet, radius, true_anomaly);
}

static Mat4 planet_model_matrix(const Planet *planet) {
    Vec3 pos = planet_position(planet);
    Mat4 scale = mat4_scale(planet->size, planet->size, planet->size);
    Mat4 spin = mat4_rotate_y(planet->rotation_angle_deg);
    Mat4 tilt = mat4_rotate_z(planet->axial_tilt_deg);
    return mat4_mul(mat4_translate(pos), mat4_mul(tilt, mat4_mul(spin, scale)));
}

static void solar_system_init(MetalApp *app) {
    SolarSystem *ss = &app->solar_system;
    memset((void *) ss, 0, sizeof(*ss));
    srand(42);

    planet_init(&ss->planets[0], "Mercury", 4.0f, 0.2056f, 7.00f, 48.33f, 29.12f, 0.15f, vec3(0.7f, 0.7f, 0.7f), 88.0f, 10.0f, 0.03f, 58.65f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "mercury.jpg");
    planet_init(&ss->planets[1], "Venus", 5.8f, 0.0068f, 3.39f, 76.68f, 54.88f, 0.30f, vec3(0.9f, 0.7f, 0.3f), 225.0f, 75.0f, 177.36f, -243.02f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "venus.jpg");
    planet_init(&ss->planets[2], "Earth", 8.0f, 0.0167f, 0.00f, -11.26f, 114.21f, 0.32f, vec3(0.2f, 0.5f, 1.0f), 365.0f, 135.0f, 23.44f, 0.997f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "earth.jpg");
    planet_init(&ss->planets[3], "Mars", 10.5f, 0.0934f, 1.85f, 49.58f, 286.50f, 0.20f, vec3(0.9f, 0.3f, 0.2f), 687.0f, 195.0f, 25.19f, 1.03f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "mars.jpg");
    planet_init(&ss->planets[4], "Jupiter", 14.5f, 0.0489f, 1.30f, 100.46f, 273.87f, 0.90f, vec3(0.8f, 0.6f, 0.4f), 4333.0f, 250.0f, 3.13f, 0.41f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "jupiter.jpg");
    planet_init(&ss->planets[5], "Saturn", 19.0f, 0.0565f, 2.49f, 113.67f, 339.39f, 0.80f, vec3(0.9f, 0.8f, 0.5f), 10759.0f, 305.0f, 26.73f, 0.45f, true, vec3(0.85f, 0.75f, 0.55f), 1.0f, 1.6f, "saturn.jpg");
    planet_init(&ss->planets[6], "Uranus", 23.0f, 0.0472f, 0.77f, 74.01f, 96.73f, 0.50f, vec3(0.5f, 0.8f, 0.9f), 30687.0f, 15.0f, 97.77f, -0.72f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "uranus.jpg");
    planet_init(&ss->planets[7], "Neptune", 27.5f, 0.0086f, 1.77f, 131.78f, 273.19f, 0.50f, vec3(0.2f, 0.3f, 0.9f), 60190.0f, 100.0f, 28.32f, 0.67f, false, vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, "neptune.jpg");

    ss->sun_radius = 1.5f;
    ss->sun_color = vec3(1.0f, 0.85f, 0.2f);
    ss->sun_rotation_period_days = 24.47f;
    ss->sun_rotation_angle_deg = randf_range(0.0f, 360.0f);

    create_sphere_mesh(app->device, 1.0f, 40, 28, &ss->sphere_mesh);
    create_sphere_mesh(app->device, 1.0f, 40, 28, &ss->sun_mesh);
    create_ring_mesh(app->device, 1.0f, 1.6f, 96, &ss->ring_mesh);
    generate_stars(app->device, 1800, 220.0f, &ss->star_mesh);
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        create_orbit_path(app->device, &ss->planets[i], 192, &ss->orbit_meshes[i]);
    }

    char texture_dir[MAX_PATH_LEN];
    join_path(texture_dir, sizeof(texture_dir), app->base_dir, "assets/textures");
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        char texture_path[MAX_PATH_LEN];
        join_path(texture_path, sizeof(texture_path), texture_dir, ss->planets[i].texture_name);
        ss->planets[i].texture = load_texture_2d(app->device, texture_path, true);
    }

    char sun_path[MAX_PATH_LEN];
    join_path(sun_path, sizeof(sun_path), texture_dir, "sun.jpg");
    ss->sun_texture = load_texture_2d(app->device, sun_path, true);
    char ring_alpha_path[MAX_PATH_LEN];
    join_path(ring_alpha_path, sizeof(ring_alpha_path), texture_dir, "saturn_ring_alpha.png");
    ss->ring_alpha_texture = load_texture_2d(app->device, ring_alpha_path, false);
}

static void solar_system_update(SolarSystem *ss, float dt, float speed) {
    const float orbit_day_scale = 60.0f;
    const float rotation_time_scale = 8.0f;
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        Planet *p = &ss->planets[i];
        if (p->orbit_days > 0.0f) {
            float angular_speed = 360.0f / p->orbit_days;
            p->orbit_mean_anomaly_deg = fmodf(p->orbit_mean_anomaly_deg + angular_speed * dt * speed * orbit_day_scale, 360.0f);
        }
        if (fabsf(p->rotation_period_days) > 1e-6f) {
            float rotation_speed = 360.0f / (fabsf(p->rotation_period_days) * rotation_time_scale);
            if (p->rotation_period_days < 0.0f) {
                rotation_speed = -rotation_speed;
            }
            p->rotation_angle_deg = fmodf(p->rotation_angle_deg + rotation_speed * dt * speed, 360.0f);
        }
    }
    float sun_rotation_speed = 360.0f / (ss->sun_rotation_period_days * rotation_time_scale);
    ss->sun_rotation_angle_deg = fmodf(ss->sun_rotation_angle_deg + sun_rotation_speed * dt * speed, 360.0f);
}

static float speed_step_up(float speed) {
    if (speed < 1.0f) {
        float stepped = roundf((speed + 0.1f) * 10.0f) / 10.0f;
        return stepped > 1.0f ? 1.0f : stepped;
    }
    if (speed < 10.0f) {
        float stepped = speed + 0.5f;
        return stepped > 10.0f ? 10.0f : stepped;
    }
    float stepped = speed + 1.0f;
    return stepped > 50.0f ? 50.0f : stepped;
}

static float speed_step_down(float speed) {
    if (speed <= 0.1f) {
        return 0.1f;
    }
    if (speed <= 1.0f) {
        float stepped = roundf((speed - 0.1f) * 10.0f) / 10.0f;
        return stepped < 0.1f ? 0.1f : stepped;
    }
    if (speed <= 10.0f) {
        float stepped = speed - 0.5f;
        return stepped < 1.0f ? 1.0f : stepped;
    }
    float stepped = speed - 1.0f;
    return stepped < 10.0f ? 10.0f : stepped;
}

static Vec3 target_position(const SolarSystem *ss, int target_index) {
    if (target_index == 0) {
        return vec3(0.0f, 0.0f, 0.0f);
    }
    if (target_index >= 1 && target_index <= (int) ARRAY_LEN(ss->planets)) {
        return planet_position(&ss->planets[target_index - 1]);
    }
    return vec3(0.0f, 0.0f, 0.0f);
}

static const char *target_name(const SolarSystem *ss, int target_index) {
    if (target_index == 0) {
        return "Sun";
    }
    if (target_index >= 1 && target_index <= (int) ARRAY_LEN(ss->planets)) {
        return ss->planets[target_index - 1].name;
    }
    return "None";
}

static Args parse_args(int argc, char **argv) {
    Args args = {0};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frame-output") == 0 && i + 1 < argc) {
            args.frame_output = argv[++i];
        } else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            args.max_frames = atoi(argv[++i]);
            args.has_max_frames = true;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    return args;
}

@interface SolarMetalView : NSView
@end

@interface SolarWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation SolarMetalView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
}

- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers.lowercaseString ?: @"";
    if ([chars length] == 0) {
        return;
    }
    unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 27: g_app->input.escape = true; break;
        case 'w': g_app->input.w = true; break;
        case 'a': g_app->input.a = true; break;
        case 's': g_app->input.s = true; break;
        case 'd': g_app->input.d = true; break;
        case ' ': g_app->input.space = true; break;
        case '=':
        case '+': g_app->input.plus = true; break;
        case '-':
        case '_': g_app->input.minus = true; break;
        case 'l': g_app->input.lock_toggle = true; break;
        default:
            if (ch >= '0' && ch <= '9') {
                g_app->input.digits[ch - '0'] = true;
            }
            break;
    }
}

- (void)keyUp:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers.lowercaseString ?: @"";
    if ([chars length] == 0) {
        return;
    }
    unichar ch = [chars characterAtIndex:0];
    switch (ch) {
        case 'w': g_app->input.w = false; break;
        case 'a': g_app->input.a = false; break;
        case 's': g_app->input.s = false; break;
        case 'd': g_app->input.d = false; break;
        case ' ': g_app->input.space = false; break;
        case '=':
        case '+': g_app->input.plus = false; break;
        case '-':
        case '_': g_app->input.minus = false; break;
        case 'l': g_app->input.lock_toggle = false; break;
        default:
            if (ch >= '0' && ch <= '9') {
                g_app->input.digits[ch - '0'] = false;
            }
            break;
    }
}

- (void)mouseDown:(NSEvent *)event {
    g_app->input.mouse_down = true;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_app->input.mouse_x = p.x;
    g_app->input.mouse_y = p.y;
    camera_apply_mouse_drag(&g_app->camera, p.x, p.y);
}

- (void)mouseUp:(NSEvent *)event {
    (void) event;
    g_app->input.mouse_down = false;
    g_app->camera.dragging = false;
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_app->input.mouse_x = p.x;
    g_app->input.mouse_y = p.y;
    camera_apply_mouse_drag(&g_app->camera, p.x, p.y);
}

- (void)scrollWheel:(NSEvent *)event {
    g_app->input.scroll_delta += (float) event.scrollingDeltaY;
}

@end

@implementation SolarWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    (void) sender;
    g_app->should_close = true;
    return YES;
}

@end

static void process_app_events(void) {
    for (;;) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (!event) {
            break;
        }
        [NSApp sendEvent:event];
    }
    [NSApp updateWindows];
}

static void ensure_depth_texture(MetalApp *app) {
    CGSize size = app->metal_layer.drawableSize;
    if (size.width < 1.0 || size.height < 1.0) {
        return;
    }
    if (app->depth_texture && app->drawable_size.width == size.width && app->drawable_size.height == size.height) {
        return;
    }
    app->drawable_size = size;
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:(NSUInteger) size.width height:(NSUInteger) size.height mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    app->depth_texture = [app->device newTextureWithDescriptor:desc];
}

static id<MTLLibrary> load_shader_library(MetalApp *app) {
    char shader_path[MAX_PATH_LEN];
    char shader_dir[MAX_PATH_LEN];
    join_path(shader_dir, sizeof(shader_dir), app->base_dir, "shaders");
    join_path(shader_path, sizeof(shader_path), shader_dir, "solar_system_metal.metal");
    char *source = read_text_file(shader_path);
    if (!source) {
        fail("Failed to load Metal shader source.");
    }
    NSString *source_string = [[NSString alloc] initWithUTF8String:source];
    free(source);
    NSError *error = nil;
    id<MTLLibrary> library = [app->device newLibraryWithSource:source_string options:nil error:&error];
    if (!library) {
        fprintf(stderr, "%s\n", error.localizedDescription.UTF8String);
        fail("Failed to compile Metal shader library.");
    }
    return library;
}

static MTLVertexDescriptor *make_planet_vertex_descriptor(void) {
    MTLVertexDescriptor *descriptor = [[MTLVertexDescriptor alloc] init];
    descriptor.attributes[0].format = MTLVertexFormatFloat3;
    descriptor.attributes[0].offset = 0;
    descriptor.attributes[0].bufferIndex = 0;
    descriptor.attributes[1].format = MTLVertexFormatFloat3;
    descriptor.attributes[1].offset = sizeof(float) * 3;
    descriptor.attributes[1].bufferIndex = 0;
    descriptor.attributes[2].format = MTLVertexFormatFloat2;
    descriptor.attributes[2].offset = sizeof(float) * 6;
    descriptor.attributes[2].bufferIndex = 0;
    descriptor.layouts[0].stride = sizeof(Vertex);
    descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return descriptor;
}

static MTLVertexDescriptor *make_star_vertex_descriptor(void) {
    MTLVertexDescriptor *descriptor = [[MTLVertexDescriptor alloc] init];
    descriptor.attributes[0].format = MTLVertexFormatFloat3;
    descriptor.attributes[0].offset = 0;
    descriptor.attributes[0].bufferIndex = 0;
    descriptor.attributes[1].format = MTLVertexFormatFloat;
    descriptor.attributes[1].offset = sizeof(float) * 3;
    descriptor.attributes[1].bufferIndex = 0;
    descriptor.layouts[0].stride = sizeof(StarVertex);
    descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return descriptor;
}

static MTLVertexDescriptor *make_orbit_vertex_descriptor(void) {
    MTLVertexDescriptor *descriptor = [[MTLVertexDescriptor alloc] init];
    descriptor.attributes[0].format = MTLVertexFormatFloat3;
    descriptor.attributes[0].offset = 0;
    descriptor.attributes[0].bufferIndex = 0;
    descriptor.layouts[0].stride = sizeof(simd_float3);
    descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return descriptor;
}

static id<MTLRenderPipelineState> build_pipeline(MetalApp *app, NSString *vertex_name, NSString *fragment_name, MTLVertexDescriptor *vertex_descriptor, bool blending, MTLCullMode cull_mode) {
    (void) cull_mode;
    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = [app->shader_library newFunctionWithName:vertex_name];
    desc.fragmentFunction = fragment_name ? [app->shader_library newFunctionWithName:fragment_name] : nil;
    desc.vertexDescriptor = vertex_descriptor;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    desc.inputPrimitiveTopology = [vertex_name hasPrefix:@"orbit"] ? MTLPrimitiveTopologyClassLine : ([vertex_name hasPrefix:@"star"] ? MTLPrimitiveTopologyClassPoint : MTLPrimitiveTopologyClassTriangle);
    if (blending) {
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline = [app->device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipeline) {
        fprintf(stderr, "%s\n", error.localizedDescription.UTF8String);
        fail("Failed to create Metal pipeline.");
    }
    return pipeline;
}

static void setup_pipelines(MetalApp *app) {
    app->shader_library = load_shader_library(app);
    app->planet_pipeline = build_pipeline(app, @"planet_vertex", @"planet_fragment", make_planet_vertex_descriptor(), false, MTLCullModeBack);
    app->ring_pipeline = build_pipeline(app, @"planet_vertex", @"ring_fragment", make_planet_vertex_descriptor(), true, MTLCullModeNone);
    app->star_pipeline = build_pipeline(app, @"star_vertex", @"star_fragment", make_star_vertex_descriptor(), false, MTLCullModeNone);
    app->orbit_pipeline = build_pipeline(app, @"orbit_vertex", @"orbit_fragment", make_orbit_vertex_descriptor(), false, MTLCullModeNone);

    MTLDepthStencilDescriptor *depth_desc = [[MTLDepthStencilDescriptor alloc] init];
    depth_desc.depthCompareFunction = MTLCompareFunctionLess;
    depth_desc.depthWriteEnabled = YES;
    app->depth_state = [app->device newDepthStencilStateWithDescriptor:depth_desc];

    MTLDepthStencilDescriptor *depth_no_write_desc = [[MTLDepthStencilDescriptor alloc] init];
    depth_no_write_desc.depthCompareFunction = MTLCompareFunctionAlways;
    depth_no_write_desc.depthWriteEnabled = NO;
    app->depth_no_write_state = [app->device newDepthStencilStateWithDescriptor:depth_no_write_desc];

    MTLSamplerDescriptor *repeat_desc = [[MTLSamplerDescriptor alloc] init];
    repeat_desc.minFilter = MTLSamplerMinMagFilterLinear;
    repeat_desc.magFilter = MTLSamplerMinMagFilterLinear;
    repeat_desc.sAddressMode = MTLSamplerAddressModeRepeat;
    repeat_desc.tAddressMode = MTLSamplerAddressModeRepeat;
    app->repeat_sampler = [app->device newSamplerStateWithDescriptor:repeat_desc];

    MTLSamplerDescriptor *clamp_desc = [[MTLSamplerDescriptor alloc] init];
    clamp_desc.minFilter = MTLSamplerMinMagFilterLinear;
    clamp_desc.magFilter = MTLSamplerMinMagFilterLinear;
    clamp_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    clamp_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    app->clamp_sampler = [app->device newSamplerStateWithDescriptor:clamp_desc];
}

static void update_window_title(MetalApp *app) {
    char title[256];
    snprintf(
        title,
        sizeof(title),
        "Solar System Metal | FPS %.1f | Speed %.1fx%s | Camera %s -> %s | Mouse rotate | Scroll zoom | WASD move | L lock target | 0 Sun | 1-8 planets",
        app->fps,
        app->speed,
        app->paused ? " | PAUSED" : "",
        app->camera_locked ? "LOCKED" : "FREE",
        target_name(&app->solar_system, app->target_index)
    );
    [app->window setTitle:[NSString stringWithUTF8String:title]];
}

static void handle_input(MetalApp *app, float dt) {
    InputState *input = &app->input;
    if (input->escape) {
        app->should_close = true;
    }

    if (input->space && !app->prev_space) {
        app->paused = !app->paused;
    }
    if (input->lock_toggle && !app->prev_lock) {
        app->camera_locked = !app->camera_locked;
        app->camera.dragging = false;
    }
    if (input->plus && !app->prev_plus) {
        app->speed = speed_step_up(app->speed);
    }
    if (input->minus && !app->prev_minus) {
        app->speed = speed_step_down(app->speed);
    }

    for (int i = 0; i <= 9; ++i) {
        if (input->digits[i] && !app->prev_digits[i] && (i == 0 || i <= (int) ARRAY_LEN(app->solar_system.planets))) {
            app->target_index = i;
        }
        app->prev_digits[i] = input->digits[i];
    }

    app->prev_space = input->space;
    app->prev_plus = input->plus;
    app->prev_minus = input->minus;
    app->prev_lock = input->lock_toggle;

    float move_forward = 0.0f;
    float move_right = 0.0f;
    if (input->w) {
        move_forward += 1.0f;
    }
    if (input->s) {
        move_forward -= 1.0f;
    }
    if (input->d) {
        move_right += 1.0f;
    }
    if (input->a) {
        move_right -= 1.0f;
    }
    if (move_forward != 0.0f || move_right != 0.0f) {
        camera_move(&app->camera, move_forward, move_right, dt);
    }
    if (fabsf(input->scroll_delta) > 1e-6f) {
        camera_apply_zoom(&app->camera, input->scroll_delta);
        input->scroll_delta = 0.0f;
    }
}

static void fill_vertex_uniforms(VertexUniforms *uniforms, Mat4 model, Mat4 view, Mat4 projection, Mat3 normal, float point_size) {
    memset(uniforms, 0, sizeof(*uniforms));
    copy_mat4_uniform(uniforms->model, &model);
    copy_mat4_uniform(uniforms->view, &view);
    copy_mat4_uniform(uniforms->projection, &projection);
    copy_mat3_uniform(uniforms->normal_matrix_col0, uniforms->normal_matrix_col1, uniforms->normal_matrix_col2, &normal);
    uniforms->point_params[0] = point_size;
}

static void draw_mesh(id<MTLRenderCommandEncoder> encoder, const Mesh *mesh) {
    [encoder setVertexBuffer:mesh->vertex_buffer offset:0 atIndex:0];
    if (mesh->index_count > 0) {
        [encoder drawIndexedPrimitives:mesh->primitive indexCount:mesh->index_count indexType:MTLIndexTypeUInt32 indexBuffer:mesh->index_buffer indexBufferOffset:0];
    } else {
        [encoder drawPrimitives:mesh->primitive vertexStart:0 vertexCount:mesh->vertex_count];
    }
}

static void render_frame(MetalApp *app) {
    ensure_depth_texture(app);
    if (!app->depth_texture) {
        return;
    }

    id<CAMetalDrawable> drawable = [app->metal_layer nextDrawable];
    if (!drawable) {
        return;
    }

    Vec3 light_pos = vec3(0.0f, 0.0f, 0.0f);
    Vec3 light_ambient = vec3(0.02f, 0.02f, 0.02f);
    Vec3 light_diffuse = vec3(1.34f, 1.26f, 1.16f);
    Vec3 light_specular = vec3(0.32f, 0.32f, 0.32f);
    float aspect = app->drawable_size.height > 0.0 ? (float) app->drawable_size.width / (float) app->drawable_size.height : (float) WINDOW_WIDTH / (float) WINDOW_HEIGHT;
    Mat4 projection = mat4_perspective(45.0f, aspect, 0.5f, 500.0f);
    Mat4 view = app->camera.view_matrix;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.01, 1.0);
    pass.depthAttachment.texture = app->depth_texture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

    id<MTLCommandBuffer> command_buffer = [app->command_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];

    VertexUniforms vertex_uniforms;
    PlanetFragmentUniforms planet_uniforms;
    RingFragmentUniforms ring_uniforms;
    OrbitFragmentUniforms orbit_uniforms;
    Mat4 identity = mat4_identity();
    Mat3 identity3 = mat3_from_mat4_inverse_transpose(identity);

    [encoder setCullMode:MTLCullModeNone];
    [encoder setDepthStencilState:app->depth_state];
    [encoder setRenderPipelineState:app->star_pipeline];
    fill_vertex_uniforms(&vertex_uniforms, identity, view, projection, identity3, 2.0f);
    [encoder setVertexBytes:&vertex_uniforms length:sizeof(vertex_uniforms) atIndex:1];
    draw_mesh(encoder, &app->solar_system.star_mesh);

    [encoder setCullMode:MTLCullModeNone];
    [encoder setDepthStencilState:app->depth_no_write_state];
    [encoder setRenderPipelineState:app->orbit_pipeline];
    fill_vertex_uniforms(&vertex_uniforms, identity, view, projection, identity3, 1.0f);
    [encoder setVertexBytes:&vertex_uniforms length:sizeof(vertex_uniforms) atIndex:1];
    memset(&orbit_uniforms, 0, sizeof(orbit_uniforms));
    orbit_uniforms.color[0] = 0.25f;
    orbit_uniforms.color[1] = 0.25f;
    orbit_uniforms.color[2] = 0.35f;
    orbit_uniforms.color[3] = 1.0f;
    [encoder setFragmentBytes:&orbit_uniforms length:sizeof(orbit_uniforms) atIndex:0];
    for (size_t i = 0; i < ARRAY_LEN(app->solar_system.orbit_meshes); ++i) {
        draw_mesh(encoder, &app->solar_system.orbit_meshes[i]);
    }

    [encoder setDepthStencilState:app->depth_state];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setRenderPipelineState:app->planet_pipeline];
    memset(&planet_uniforms, 0, sizeof(planet_uniforms));
    planet_uniforms.light_pos[0] = light_pos.x;
    planet_uniforms.light_pos[1] = light_pos.y;
    planet_uniforms.light_pos[2] = light_pos.z;
    planet_uniforms.light_pos[3] = 1.0f;
    planet_uniforms.camera_pos[0] = app->camera.eye.x;
    planet_uniforms.camera_pos[1] = app->camera.eye.y;
    planet_uniforms.camera_pos[2] = app->camera.eye.z;
    planet_uniforms.camera_pos[3] = 1.0f;

    Mat4 sun_model = mat4_mul(mat4_rotate_y(app->solar_system.sun_rotation_angle_deg), mat4_scale(app->solar_system.sun_radius, app->solar_system.sun_radius, app->solar_system.sun_radius));
    Mat3 sun_normal = mat3_from_mat4_inverse_transpose(sun_model);
    fill_vertex_uniforms(&vertex_uniforms, sun_model, view, projection, sun_normal, 1.0f);
    [encoder setVertexBytes:&vertex_uniforms length:sizeof(vertex_uniforms) atIndex:1];
    planet_uniforms.material_diffuse[0] = app->solar_system.sun_color.x;
    planet_uniforms.material_diffuse[1] = app->solar_system.sun_color.y;
    planet_uniforms.material_diffuse[2] = app->solar_system.sun_color.z;
    planet_uniforms.material_diffuse[3] = 1.0f;
    planet_uniforms.params[0] = 1.0f;
    planet_uniforms.params[1] = 1.0f;
    planet_uniforms.params[2] = app->solar_system.sun_texture ? 1.0f : 0.0f;
    [encoder setFragmentBytes:&planet_uniforms length:sizeof(planet_uniforms) atIndex:0];
    [encoder setFragmentTexture:app->solar_system.sun_texture atIndex:0];
    [encoder setFragmentSamplerState:app->repeat_sampler atIndex:0];
    draw_mesh(encoder, &app->solar_system.sun_mesh);

    memset(&planet_uniforms, 0, sizeof(planet_uniforms));
    planet_uniforms.light_pos[0] = light_pos.x;
    planet_uniforms.light_pos[1] = light_pos.y;
    planet_uniforms.light_pos[2] = light_pos.z;
    planet_uniforms.light_pos[3] = 1.0f;
    planet_uniforms.camera_pos[0] = app->camera.eye.x;
    planet_uniforms.camera_pos[1] = app->camera.eye.y;
    planet_uniforms.camera_pos[2] = app->camera.eye.z;
    planet_uniforms.camera_pos[3] = 1.0f;
    planet_uniforms.light_ambient[0] = light_ambient.x;
    planet_uniforms.light_ambient[1] = light_ambient.y;
    planet_uniforms.light_ambient[2] = light_ambient.z;
    planet_uniforms.light_ambient[3] = 1.0f;
    planet_uniforms.light_diffuse[0] = light_diffuse.x;
    planet_uniforms.light_diffuse[1] = light_diffuse.y;
    planet_uniforms.light_diffuse[2] = light_diffuse.z;
    planet_uniforms.light_diffuse[3] = 1.0f;
    planet_uniforms.light_specular[0] = light_specular.x;
    planet_uniforms.light_specular[1] = light_specular.y;
    planet_uniforms.light_specular[2] = light_specular.z;
    planet_uniforms.light_specular[3] = 1.0f;

    for (size_t i = 0; i < ARRAY_LEN(app->solar_system.planets); ++i) {
        Planet *planet = &app->solar_system.planets[i];
        Mat4 model = planet_model_matrix(planet);
        Mat3 normal = mat3_from_mat4_inverse_transpose(model);
        fill_vertex_uniforms(&vertex_uniforms, model, view, projection, normal, 1.0f);
        [encoder setVertexBytes:&vertex_uniforms length:sizeof(vertex_uniforms) atIndex:1];

        planet_uniforms.material_ambient[0] = planet->color.x;
        planet_uniforms.material_ambient[1] = planet->color.y;
        planet_uniforms.material_ambient[2] = planet->color.z;
        planet_uniforms.material_ambient[3] = 1.0f;
        planet_uniforms.material_diffuse[0] = planet->color.x;
        planet_uniforms.material_diffuse[1] = planet->color.y;
        planet_uniforms.material_diffuse[2] = planet->color.z;
        planet_uniforms.material_diffuse[3] = 1.0f;
        planet_uniforms.material_specular[0] = 0.01f;
        planet_uniforms.material_specular[1] = 0.01f;
        planet_uniforms.material_specular[2] = 0.01f;
        planet_uniforms.material_specular[3] = 1.0f;
        planet_uniforms.params[0] = 10.0f;
        planet_uniforms.params[1] = 0.0f;
        planet_uniforms.params[2] = planet->texture ? 1.0f : 0.0f;
        [encoder setFragmentBytes:&planet_uniforms length:sizeof(planet_uniforms) atIndex:0];
        [encoder setFragmentTexture:planet->texture atIndex:0];
        [encoder setFragmentSamplerState:app->repeat_sampler atIndex:0];
        draw_mesh(encoder, &app->solar_system.sphere_mesh);

        if (planet->has_ring) {
            [encoder setCullMode:MTLCullModeNone];
            [encoder setRenderPipelineState:app->ring_pipeline];
            Mat4 ring_model = mat4_mul(mat4_translate(planet_position(planet)), mat4_rotate_z(planet->axial_tilt_deg));
            Mat3 ring_normal = mat3_from_mat4_inverse_transpose(ring_model);
            fill_vertex_uniforms(&vertex_uniforms, ring_model, view, projection, ring_normal, 1.0f);
            [encoder setVertexBytes:&vertex_uniforms length:sizeof(vertex_uniforms) atIndex:1];
            memset(&ring_uniforms, 0, sizeof(ring_uniforms));
            ring_uniforms.light_pos[0] = light_pos.x;
            ring_uniforms.light_pos[1] = light_pos.y;
            ring_uniforms.light_pos[2] = light_pos.z;
            ring_uniforms.light_pos[3] = 1.0f;
            ring_uniforms.light_diffuse[0] = light_diffuse.x;
            ring_uniforms.light_diffuse[1] = light_diffuse.y;
            ring_uniforms.light_diffuse[2] = light_diffuse.z;
            ring_uniforms.light_diffuse[3] = 1.0f;
            ring_uniforms.material_color_alpha[0] = planet->ring_color.x;
            ring_uniforms.material_color_alpha[1] = planet->ring_color.y;
            ring_uniforms.material_color_alpha[2] = planet->ring_color.z;
            ring_uniforms.material_color_alpha[3] = 0.8f;
            ring_uniforms.params[0] = app->solar_system.ring_alpha_texture ? 1.0f : 0.0f;
            [encoder setFragmentBytes:&ring_uniforms length:sizeof(ring_uniforms) atIndex:0];
            [encoder setFragmentTexture:app->solar_system.ring_alpha_texture atIndex:0];
            [encoder setFragmentSamplerState:app->clamp_sampler atIndex:0];
            draw_mesh(encoder, &app->solar_system.ring_mesh);
            [encoder setRenderPipelineState:app->planet_pipeline];
            [encoder setCullMode:MTLCullModeBack];
        }
    }

    [encoder endEncoding];

    [command_buffer presentDrawable:drawable];
    [command_buffer commit];

    ++app->fps_frames;
    ++app->frame_count;
    double now = app->last_time;
    if (now - app->fps_timer >= 0.25) {
        app->fps = (float) app->fps_frames / (float) (now - app->fps_timer);
        app->fps_frames = 0;
        app->fps_timer = now;
        update_window_title(app);
    }

    if (app->args.has_max_frames && app->frame_count >= app->args.max_frames) {
        if (app->args.frame_output) {
            [command_buffer waitUntilCompleted];
            if (!save_texture_png(drawable.texture, app->args.frame_output)) {
                fprintf(stderr, "Failed to write PNG: %s\n", app->args.frame_output);
            }
        }
        app->should_close = true;
    }
}

static void app_init_window(MetalApp *app) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect frame = NSMakeRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    app->window = [[NSWindow alloc] initWithContentRect:frame styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];
    app->view = [[SolarMetalView alloc] initWithFrame:frame];
    SolarWindowDelegate *delegate = [[SolarWindowDelegate alloc] init];
    app->window.delegate = delegate;
    [app->window setContentView:app->view];
    [app->window makeFirstResponder:app->view];
    [app->window center];
    [app->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    app->device = MTLCreateSystemDefaultDevice();
    if (!app->device) {
        fail("Metal device unavailable.");
    }
    app->command_queue = [app->device newCommandQueue];
    app->metal_layer = [CAMetalLayer layer];
    app->metal_layer.device = app->device;
    app->metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    app->metal_layer.framebufferOnly = NO;
    app->metal_layer.contentsScale = app->window.backingScaleFactor;
    app->view.wantsLayer = YES;
    app->view.layer = app->metal_layer;
    app->backing_scale = app->window.backingScaleFactor;

    NSRect backing = [app->view convertRectToBacking:app->view.bounds];
    app->metal_layer.drawableSize = backing.size;
    app->drawable_size = backing.size;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        MetalApp app;
        memset((void *) &app, 0, sizeof(app));
        g_app = &app;
        app.args = parse_args(argc, argv);
        init_base_dir(app.base_dir, argv[0]);
        app.speed = 1.0f;
        camera_init(&app.camera);

        app_init_window(&app);
        setup_pipelines(&app);
        solar_system_init(&app);
        update_window_title(&app);

        printf("Metal Device: %s\n", app.device.name.UTF8String);

        app.last_time = CFAbsoluteTimeGetCurrent();
        app.fps_timer = app.last_time;

        while (!app.should_close) {
            @autoreleasepool {
                process_app_events();
                NSRect backing = [app.view convertRectToBacking:app.view.bounds];
                if (backing.size.width > 0.0 && backing.size.height > 0.0) {
                    app.metal_layer.frame = app.view.bounds;
                    app.metal_layer.drawableSize = backing.size;
                }

                double now = CFAbsoluteTimeGetCurrent();
                float dt = (float) (now - app.last_time);
                app.last_time = now;

                handle_input(&app, dt);
                if (!app.paused) {
                    solar_system_update(&app.solar_system, dt, app.speed);
                }
                if (app.camera_locked) {
                    camera_look_at_target(&app.camera, target_position(&app.solar_system, app.target_index));
                }

                render_frame(&app);
            }
        }
    }
    return 0;
}
