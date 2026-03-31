#define _XOPEN_SOURCE 700

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define MSAA_SAMPLES 4
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
    GLuint program;
    GLint u_model;
    GLint u_view;
    GLint u_projection;
    GLint u_normal_matrix;
    GLint u_light_pos;
    GLint u_camera_pos;
    GLint u_light_ambient;
    GLint u_light_diffuse;
    GLint u_light_specular;
    GLint u_material_ambient;
    GLint u_material_diffuse;
    GLint u_material_specular;
    GLint u_material_shininess;
    GLint u_emission;
    GLint u_texture;
    GLint u_texture_mix;
    GLint u_color;
    GLint u_size;
    GLint u_material_color;
    GLint u_alpha;
    GLint u_alpha_texture;
    GLint u_use_alpha_texture;
} Shader;

typedef struct {
    GLuint vao;
    GLuint vbo_positions;
    GLuint vbo_normals;
    GLuint vbo_texcoords;
    GLuint vbo_scalar;
    GLuint ebo;
    GLsizei index_count;
    GLsizei vertex_count;
    GLenum primitive;
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
    GLuint texture;
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
    GLuint sun_texture;
    GLuint ring_alpha_texture;
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

static char g_base_dir[MAX_PATH_LEN];
static Camera *g_scroll_camera = NULL;

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
    m.m[10] = (far_z + near_z) / (near_z - far_z);
    m.m[11] = (2.0f * far_z * near_z) / (near_z - far_z);
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
    float I = a * e - b * d;

    float det = a * A + b * B + c * C;
    if (fabsf(det) < 1e-8f) {
        Mat3 ident = { .m = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f} };
        return ident;
    }

    float inv_det = 1.0f / det;
    Mat3 inv = {
        .m = {
            A * inv_det, D * inv_det, G * inv_det,
            B * inv_det, E * inv_det, H * inv_det,
            C * inv_det, F * inv_det, I * inv_det
        }
    };

    Mat3 out = {0};
    out.m[0] = inv.m[0]; out.m[1] = inv.m[3]; out.m[2] = inv.m[6];
    out.m[3] = inv.m[1]; out.m[4] = inv.m[4]; out.m[5] = inv.m[7];
    out.m[6] = inv.m[2]; out.m[7] = inv.m[5]; out.m[8] = inv.m[8];
    return out;
}

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
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

static GLuint compile_shader(GLenum type, const char *path) {
    char *source = read_text_file(path);
    if (!source) {
        fail("Shader source load failed.");
    }

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, (const GLchar * const *) &source, NULL);
    glCompileShader(shader);
    free(source);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *) malloc((size_t) log_len + 1);
        glGetShaderInfoLog(shader, log_len, NULL, log);
        fprintf(stderr, "Shader compile failed: %s\n%s\n", path, log);
        free(log);
        exit(EXIT_FAILURE);
    }
    return shader;
}

static Shader load_shader_program(const char *vertex_name, const char *fragment_name) {
    char vs_path[MAX_PATH_LEN];
    char fs_path[MAX_PATH_LEN];
    char shader_dir[MAX_PATH_LEN];
    join_path(shader_dir, sizeof(shader_dir), g_base_dir, "shaders");
    join_path(vs_path, sizeof(vs_path), shader_dir, vertex_name);
    join_path(fs_path, sizeof(fs_path), shader_dir, fragment_name);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_path);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_path);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *) malloc((size_t) log_len + 1);
        glGetProgramInfoLog(program, log_len, NULL, log);
        fprintf(stderr, "Program link failed:\n%s\n", log);
        free(log);
        exit(EXIT_FAILURE);
    }

    Shader s = {0};
    s.program = program;
    s.u_model = glGetUniformLocation(program, "u_model");
    s.u_view = glGetUniformLocation(program, "u_view");
    s.u_projection = glGetUniformLocation(program, "u_projection");
    s.u_normal_matrix = glGetUniformLocation(program, "u_normal_matrix");
    s.u_light_pos = glGetUniformLocation(program, "u_light_pos");
    s.u_camera_pos = glGetUniformLocation(program, "u_camera_pos");
    s.u_light_ambient = glGetUniformLocation(program, "u_light_ambient");
    s.u_light_diffuse = glGetUniformLocation(program, "u_light_diffuse");
    s.u_light_specular = glGetUniformLocation(program, "u_light_specular");
    s.u_material_ambient = glGetUniformLocation(program, "u_material_ambient");
    s.u_material_diffuse = glGetUniformLocation(program, "u_material_diffuse");
    s.u_material_specular = glGetUniformLocation(program, "u_material_specular");
    s.u_material_shininess = glGetUniformLocation(program, "u_material_shininess");
    s.u_emission = glGetUniformLocation(program, "u_emission");
    s.u_texture = glGetUniformLocation(program, "u_texture");
    s.u_texture_mix = glGetUniformLocation(program, "u_texture_mix");
    s.u_color = glGetUniformLocation(program, "u_color");
    s.u_size = glGetUniformLocation(program, "u_size");
    s.u_material_color = glGetUniformLocation(program, "u_material_color");
    s.u_alpha = glGetUniformLocation(program, "u_alpha");
    s.u_alpha_texture = glGetUniformLocation(program, "u_alpha_texture");
    s.u_use_alpha_texture = glGetUniformLocation(program, "u_use_alpha_texture");
    return s;
}

static void shader_use(const Shader *shader) {
    glUseProgram(shader->program);
}

static void shader_set_mat4(GLint loc, const Mat4 *mat) {
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_TRUE, mat->m);
    }
}

static void shader_set_mat3(GLint loc, const Mat3 *mat) {
    if (loc >= 0) {
        glUniformMatrix3fv(loc, 1, GL_TRUE, mat->m);
    }
}

static void shader_set_vec3(GLint loc, Vec3 v) {
    if (loc >= 0) {
        glUniform3f(loc, v.x, v.y, v.z);
    }
}

static void shader_set_float(GLint loc, float value) {
    if (loc >= 0) {
        glUniform1f(loc, value);
    }
}

static void shader_set_int(GLint loc, int value) {
    if (loc >= 0) {
        glUniform1i(loc, value);
    }
}

static void mesh_init(Mesh *mesh, GLenum primitive) {
    memset(mesh, 0, sizeof(*mesh));
    mesh->primitive = primitive;
    glGenVertexArrays(1, &mesh->vao);
}

static void mesh_set_positions(Mesh *mesh, const float *data, size_t count, GLint size) {
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(float), data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, size, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

static void mesh_set_normals(Mesh *mesh, const float *data, size_t count) {
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(float), data, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

static void mesh_set_texcoords(Mesh *mesh, const float *data, size_t count) {
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->vbo_texcoords);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_texcoords);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(float), data, GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

static void mesh_set_scalar_attribute(Mesh *mesh, const float *data, size_t count, GLuint location) {
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->vbo_scalar);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_scalar);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(float), data, GL_STATIC_DRAW);
    glVertexAttribPointer(location, 1, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(location);
    glBindVertexArray(0);
}

static void mesh_set_indices(Mesh *mesh, const uint32_t *indices, size_t count) {
    glBindVertexArray(mesh->vao);
    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    mesh->index_count = (GLsizei) count;
    glBindVertexArray(0);
}

static void mesh_draw(const Mesh *mesh) {
    glBindVertexArray(mesh->vao);
    if (mesh->index_count > 0) {
        glDrawElements(mesh->primitive, mesh->index_count, GL_UNSIGNED_INT, NULL);
    } else {
        glDrawArrays(mesh->primitive, 0, mesh->vertex_count);
    }
    glBindVertexArray(0);
}

static GLuint load_texture_2d(const char *path, bool wrap_repeat, bool srgb) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "Texture load failed: %s\n", path);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return texture;
}

static void save_framebuffer_png(const char *path, int width, int height) {
    size_t pixel_bytes = (size_t) width * (size_t) height * 4;
    unsigned char *pixels = (unsigned char *) malloc(pixel_bytes);
    unsigned char *flipped = (unsigned char *) malloc(pixel_bytes);
    if (!pixels || !flipped) {
        free(pixels);
        free(flipped);
        fprintf(stderr, "Failed to allocate framebuffer capture memory.\n");
        return;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < height; ++y) {
        memcpy(
            flipped + (size_t) y * (size_t) width * 4,
            pixels + (size_t) (height - 1 - y) * (size_t) width * 4,
            (size_t) width * 4
        );
    }

    if (!stbi_write_png(path, width, height, 4, flipped, width * 4)) {
        fprintf(stderr, "Failed to write PNG: %s\n", path);
    }
    free(pixels);
    free(flipped);
}

static void create_sphere_mesh(float radius, int slices, int stacks, Mesh *mesh) {
    int vertex_count = (stacks + 1) * (slices + 1);
    float *positions = (float *) malloc((size_t) vertex_count * 3 * sizeof(float));
    float *normals = (float *) malloc((size_t) vertex_count * 3 * sizeof(float));
    float *texcoords = (float *) malloc((size_t) vertex_count * 2 * sizeof(float));
    size_t max_indices = (size_t) stacks * (size_t) slices * 6;
    uint32_t *indices = (uint32_t *) malloc(max_indices * sizeof(uint32_t));
    size_t index_count = 0;

    int v = 0;
    int t = 0;
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

            positions[v] = radius * x;
            normals[v++] = x;
            positions[v] = radius * y;
            normals[v++] = y;
            positions[v] = radius * z;
            normals[v++] = z;

            texcoords[t++] = 1.0f - (float) j / (float) slices;
            texcoords[t++] = 1.0f - (float) i / (float) stacks;
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

    mesh_init(mesh, GL_TRIANGLES);
    mesh_set_positions(mesh, positions, (size_t) vertex_count * 3, 3);
    mesh_set_normals(mesh, normals, (size_t) vertex_count * 3);
    mesh_set_texcoords(mesh, texcoords, (size_t) vertex_count * 2);
    mesh_set_indices(mesh, indices, index_count);

    free(positions);
    free(normals);
    free(texcoords);
    free(indices);
}

static void create_ring_mesh(float inner_radius, float outer_radius, int segments, Mesh *mesh) {
    int vertex_count = 4 * (segments + 1);
    float *positions = (float *) malloc((size_t) vertex_count * 3 * sizeof(float));
    float *normals = (float *) malloc((size_t) vertex_count * 3 * sizeof(float));
    float *texcoords = (float *) malloc((size_t) vertex_count * 2 * sizeof(float));
    uint32_t *indices = (uint32_t *) malloc((size_t) segments * 12 * sizeof(uint32_t));
    size_t index_count = 0;
    int p = 0;
    int n = 0;
    int t = 0;

    for (int side_idx = 0; side_idx < 2; ++side_idx) {
        float side = side_idx == 0 ? -1.0f : 1.0f;
        for (int i = 0; i <= segments; ++i) {
            float theta = 2.0f * (float) M_PI * (float) i / (float) segments;
            float c = cosf(theta);
            float s = sinf(theta);

            positions[p++] = inner_radius * c;
            positions[p++] = 0.0f;
            positions[p++] = inner_radius * s;
            normals[n++] = 0.0f;
            normals[n++] = side;
            normals[n++] = 0.0f;
            texcoords[t++] = 0.0f;
            texcoords[t++] = (float) i / (float) segments;

            positions[p++] = outer_radius * c;
            positions[p++] = 0.0f;
            positions[p++] = outer_radius * s;
            normals[n++] = 0.0f;
            normals[n++] = side;
            normals[n++] = 0.0f;
            texcoords[t++] = 1.0f;
            texcoords[t++] = (float) i / (float) segments;
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

    mesh_init(mesh, GL_TRIANGLES);
    mesh_set_positions(mesh, positions, (size_t) vertex_count * 3, 3);
    mesh_set_normals(mesh, normals, (size_t) vertex_count * 3);
    mesh_set_texcoords(mesh, texcoords, (size_t) vertex_count * 2);
    mesh_set_indices(mesh, indices, index_count);

    free(positions);
    free(normals);
    free(texcoords);
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

static void create_orbit_path(const Planet *planet, int segments, Mesh *mesh) {
    int vertex_count = segments + 1;
    float *positions = (float *) malloc((size_t) vertex_count * 3 * sizeof(float));
    int p = 0;
    float semi_latus_rectum = planet->orbit_radius * (1.0f - planet->orbit_eccentricity * planet->orbit_eccentricity);
    for (int i = 0; i <= segments; ++i) {
        float true_anomaly = 2.0f * (float) M_PI * (float) i / (float) segments;
        float radius = semi_latus_rectum / (1.0f + planet->orbit_eccentricity * cosf(true_anomaly));
        Vec3 position = orbit_to_world(planet, radius, true_anomaly);
        positions[p++] = position.x;
        positions[p++] = position.y;
        positions[p++] = position.z;
    }

    mesh_init(mesh, GL_LINE_STRIP);
    mesh_set_positions(mesh, positions, (size_t) vertex_count * 3, 3);
    mesh->vertex_count = vertex_count;
    free(positions);
}

static void generate_stars(int count, float radius, Mesh *mesh) {
    float *positions = (float *) malloc((size_t) count * 3 * sizeof(float));
    float *brightness = (float *) malloc((size_t) count * sizeof(float));
    int p = 0;
    srand(7);
    for (int i = 0; i < count; ++i) {
        float theta = randf_range(0.0f, 2.0f * (float) M_PI);
        float phi = randf_range(-(float) M_PI / 2.0f, (float) M_PI / 2.0f);
        positions[p++] = radius * cosf(phi) * cosf(theta);
        positions[p++] = radius * sinf(phi);
        positions[p++] = radius * cosf(phi) * sinf(theta);
        brightness[i] = randf_range(0.4f, 1.0f);
    }

    mesh_init(mesh, GL_POINTS);
    mesh_set_positions(mesh, positions, (size_t) count * 3, 3);
    mesh_set_scalar_attribute(mesh, brightness, (size_t) count, 1);
    mesh->vertex_count = count;

    free(positions);
    free(brightness);
}

static void camera_update_view(Camera *camera) {
    Vec3 world_up = vec3(0.0f, 1.0f, 0.0f);
    float pitch = camera->pitch_deg * (float) M_PI / 180.0f;
    float yaw = camera->yaw_deg * (float) M_PI / 180.0f;
    Vec3 forward = vec3(
        cosf(pitch) * cosf(yaw),
        sinf(pitch),
        cosf(pitch) * sinf(yaw)
    );
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

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void) window;
    (void) xoffset;
    if (g_scroll_camera) {
        g_scroll_camera->zoom_delta += (float) yoffset;
    }
}

static void camera_handle_mouse(Camera *camera, GLFWwindow *window) {
    double mouse_x = 0.0;
    double mouse_y = 0.0;
    glfwGetCursorPos(window, &mouse_x, &mouse_y);
    int pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
    if (pressed == GLFW_PRESS) {
        if (!camera->dragging) {
            camera->dragging = true;
            camera->last_mouse_x = mouse_x;
            camera->last_mouse_y = mouse_y;
        } else {
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
    } else {
        camera->dragging = false;
    }

    if (fabsf(camera->zoom_delta) > 1e-6f) {
        camera->eye = vec3_add(camera->eye, vec3_scale(camera->forward, camera->zoom_delta * 2.0f));
        camera->zoom_delta = 0.0f;
        camera_update_view(camera);
    }
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
    memset(planet, 0, sizeof(*planet));
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
    planet->orbit_frame = orbit_frame_from_elements(
        orbit_ascending_node_deg,
        orbit_inclination_deg,
        orbit_periapsis_deg
    );
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

static void solar_system_init(SolarSystem *ss) {
    memset(ss, 0, sizeof(*ss));
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

    create_sphere_mesh(1.0f, 40, 28, &ss->sphere_mesh);
    create_sphere_mesh(1.0f, 40, 28, &ss->sun_mesh);
    create_ring_mesh(1.0f, 1.6f, 96, &ss->ring_mesh);
    generate_stars(1800, 220.0f, &ss->star_mesh);
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        create_orbit_path(&ss->planets[i], 192, &ss->orbit_meshes[i]);
    }

    char texture_dir[MAX_PATH_LEN];
    join_path(texture_dir, sizeof(texture_dir), g_base_dir, "assets/textures");
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        char texture_path[MAX_PATH_LEN];
        join_path(texture_path, sizeof(texture_path), texture_dir, ss->planets[i].texture_name);
        ss->planets[i].texture = load_texture_2d(texture_path, true, true);
    }

    char sun_path[MAX_PATH_LEN];
    join_path(sun_path, sizeof(sun_path), texture_dir, "sun.jpg");
    ss->sun_texture = load_texture_2d(sun_path, true, true);
    char ring_alpha_path[MAX_PATH_LEN];
    join_path(ring_alpha_path, sizeof(ring_alpha_path), texture_dir, "saturn_ring_alpha.png");
    ss->ring_alpha_texture = load_texture_2d(ring_alpha_path, false, false);
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

static void update_window_title(GLFWwindow *window, float fps, float speed, bool paused, bool camera_locked, const char *focus_name) {
    char title[256];
    snprintf(
        title,
        sizeof(title),
        "Solar System C | FPS %.1f | Speed %.1fx%s | Camera %s -> %s | Mouse rotate | Scroll zoom | WASD move | L lock target | 0 Sun | 1-8 planets",
        fps,
        speed,
        paused ? " | PAUSED" : "",
        camera_locked ? "LOCKED" : "FREE",
        focus_name
    );
    glfwSetWindowTitle(window, title);
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
    args.max_frames = 0;
    args.has_max_frames = false;

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

static void init_base_dir(const char *argv0) {
    char resolved[MAX_PATH_LEN];
    if (!realpath(argv0, resolved)) {
        if (!getcwd(resolved, sizeof(resolved))) {
            fail("Failed to determine executable directory.");
        }
    }

    char *slash = strrchr(resolved, '/');
    if (slash) {
        *slash = '\0';
        snprintf(g_base_dir, sizeof(g_base_dir), "%s", resolved);
    } else {
        snprintf(g_base_dir, sizeof(g_base_dir), ".");
    }
}

int main(int argc, char **argv) {
    Args args = parse_args(argc, argv);
    init_base_dir(argv[0]);

    if (!glfwInit()) {
        fail("GLFW initialization failed.");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, MSAA_SAMPLES);

    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Solar System C", NULL, NULL);
    if (!window) {
        glfwTerminate();
        fail("GLFW window creation failed.");
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    GLenum glew_status = glewInit();
    if (glew_status != GLEW_OK) {
        fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(glew_status));
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glGetError();

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glClearColor(0.0f, 0.0f, 0.01f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_FRAMEBUFFER_SRGB);

    Shader planet_shader = load_shader_program("planet.vert", "planet.frag");
    Shader ring_shader = load_shader_program("ring.vert", "ring.frag");
    Shader star_shader = load_shader_program("star.vert", "star.frag");
    Shader orbit_shader = load_shader_program("orbit.vert", "orbit.frag");

    Camera camera;
    camera_init(&camera);
    g_scroll_camera = &camera;
    glfwSetScrollCallback(window, scroll_callback);

    SolarSystem solar_system;
    solar_system_init(&solar_system);

    Vec3 light_pos = vec3(0.0f, 0.0f, 0.0f);
    Vec3 light_ambient = vec3(0.02f, 0.02f, 0.02f);
    Vec3 light_diffuse = vec3(1.34f, 1.26f, 1.16f);
    Vec3 light_specular = vec3(0.32f, 0.32f, 0.32f);
    Mat4 projection = mat4_perspective(45.0f, (float) WINDOW_WIDTH / (float) WINDOW_HEIGHT, 0.5f, 500.0f);

    shader_use(&planet_shader);
    shader_set_int(planet_shader.u_texture, 0);
    shader_use(&ring_shader);
    shader_set_int(ring_shader.u_alpha_texture, 1);

    double last_time = glfwGetTime();
    double fps_timer = last_time;
    int fps_frames = 0;
    float fps = 0.0f;
    float speed = 1.0f;
    bool paused = false;
    bool camera_locked = false;
    bool prev_space = false;
    bool prev_equal = false;
    bool prev_minus = false;
    bool prev_l = false;
    bool prev_digits[10] = {false};
    int target_index = 0;
    int frame_count = 0;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float) (now - last_time);
        last_time = now;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        bool space_pressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool plus_pressed = glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
        bool minus_pressed = glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;
        bool l_pressed = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
        if (space_pressed && !prev_space) {
            paused = !paused;
        }
        if (l_pressed && !prev_l) {
            camera_locked = !camera_locked;
            camera.dragging = false;
            camera.zoom_delta = 0.0f;
        }
        if (plus_pressed && !prev_equal) {
            speed += 0.5f;
            if (speed > 50.0f) {
                speed = 50.0f;
            }
        }
        if (minus_pressed && !prev_minus) {
            speed -= 0.5f;
            if (speed < 0.1f) {
                speed = 0.1f;
            }
        }
        for (int i = 0; i <= 9; ++i) {
            int key = i == 0 ? GLFW_KEY_0 : (GLFW_KEY_0 + i);
            bool pressed = glfwGetKey(window, key) == GLFW_PRESS;
            if (pressed && !prev_digits[i] && (i == 0 || i <= (int) ARRAY_LEN(solar_system.planets))) {
                target_index = i;
            }
            prev_digits[i] = pressed;
        }
        prev_space = space_pressed;
        prev_equal = plus_pressed;
        prev_minus = minus_pressed;
        prev_l = l_pressed;

        float move_forward = 0.0f;
        float move_right = 0.0f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            move_forward += 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            move_forward -= 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            move_right += 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            move_right -= 1.0f;
        }

        camera_handle_mouse(&camera, window);
        if (move_forward != 0.0f || move_right != 0.0f) {
            camera_move(&camera, move_forward, move_right, dt);
        }
        if (!paused) {
            solar_system_update(&solar_system, dt, speed);
        }
        if (camera_locked) {
            camera_look_at_target(&camera, target_position(&solar_system, target_index));
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Mat4 view = camera.view_matrix;

        glDisable(GL_CULL_FACE);
        shader_use(&star_shader);
        shader_set_mat4(star_shader.u_view, &view);
        shader_set_mat4(star_shader.u_projection, &projection);
        shader_set_float(star_shader.u_size, 2.0f);
        mesh_draw(&solar_system.star_mesh);
        glEnable(GL_CULL_FACE);

        glDisable(GL_DEPTH_TEST);
        shader_use(&orbit_shader);
        shader_set_mat4(orbit_shader.u_view, &view);
        shader_set_mat4(orbit_shader.u_projection, &projection);
        shader_set_vec3(orbit_shader.u_color, vec3(0.25f, 0.25f, 0.35f));
        for (size_t i = 0; i < ARRAY_LEN(solar_system.orbit_meshes); ++i) {
            mesh_draw(&solar_system.orbit_meshes[i]);
        }
        glEnable(GL_DEPTH_TEST);

        shader_use(&planet_shader);
        shader_set_mat4(planet_shader.u_view, &view);
        shader_set_mat4(planet_shader.u_projection, &projection);
        shader_set_vec3(planet_shader.u_camera_pos, camera.eye);
        shader_set_vec3(planet_shader.u_light_pos, light_pos);
        shader_set_vec3(planet_shader.u_light_ambient, vec3(0.0f, 0.0f, 0.0f));
        shader_set_vec3(planet_shader.u_light_diffuse, vec3(0.0f, 0.0f, 0.0f));
        shader_set_vec3(planet_shader.u_light_specular, vec3(0.0f, 0.0f, 0.0f));
        Mat4 sun_model = mat4_mul(
            mat4_rotate_y(solar_system.sun_rotation_angle_deg),
            mat4_scale(solar_system.sun_radius, solar_system.sun_radius, solar_system.sun_radius)
        );
        Mat3 sun_normal = mat3_from_mat4_inverse_transpose(sun_model);
        shader_set_mat4(planet_shader.u_model, &sun_model);
        shader_set_mat3(planet_shader.u_normal_matrix, &sun_normal);
        shader_set_vec3(planet_shader.u_material_ambient, vec3(0.0f, 0.0f, 0.0f));
        shader_set_vec3(planet_shader.u_material_diffuse, solar_system.sun_color);
        shader_set_vec3(planet_shader.u_material_specular, vec3(0.0f, 0.0f, 0.0f));
        shader_set_float(planet_shader.u_material_shininess, 1.0f);
        shader_set_float(planet_shader.u_emission, 1.0f);
        shader_set_float(planet_shader.u_texture_mix, solar_system.sun_texture ? 1.0f : 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, solar_system.sun_texture);
        mesh_draw(&solar_system.sun_mesh);

        shader_use(&planet_shader);
        shader_set_mat4(planet_shader.u_view, &view);
        shader_set_mat4(planet_shader.u_projection, &projection);
        shader_set_vec3(planet_shader.u_camera_pos, camera.eye);
        shader_set_vec3(planet_shader.u_light_pos, light_pos);
        shader_set_vec3(planet_shader.u_light_ambient, light_ambient);
        shader_set_vec3(planet_shader.u_light_diffuse, light_diffuse);
        shader_set_vec3(planet_shader.u_light_specular, light_specular);
        shader_set_float(planet_shader.u_emission, 0.0f);

        for (size_t i = 0; i < ARRAY_LEN(solar_system.planets); ++i) {
            Planet *planet = &solar_system.planets[i];
            Mat4 model = planet_model_matrix(planet);
            Mat3 normal = mat3_from_mat4_inverse_transpose(model);
            shader_set_mat4(planet_shader.u_model, &model);
            shader_set_mat3(planet_shader.u_normal_matrix, &normal);
            shader_set_vec3(planet_shader.u_material_ambient, planet->color);
            shader_set_vec3(planet_shader.u_material_diffuse, planet->color);
            shader_set_vec3(planet_shader.u_material_specular, vec3(0.01f, 0.01f, 0.01f));
            shader_set_float(planet_shader.u_material_shininess, 10.0f);
            shader_set_float(planet_shader.u_texture_mix, planet->texture ? 1.0f : 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, planet->texture);
            mesh_draw(&solar_system.sphere_mesh);

            if (planet->has_ring) {
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                shader_use(&ring_shader);
                shader_set_mat4(ring_shader.u_view, &view);
                shader_set_mat4(ring_shader.u_projection, &projection);
                shader_set_vec3(ring_shader.u_light_pos, light_pos);
                shader_set_vec3(ring_shader.u_light_diffuse, light_diffuse);
                shader_set_vec3(ring_shader.u_material_color, planet->ring_color);
                shader_set_float(ring_shader.u_alpha, 0.8f);
                shader_set_float(ring_shader.u_use_alpha_texture, solar_system.ring_alpha_texture ? 1.0f : 0.0f);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, solar_system.ring_alpha_texture);

                Mat4 ring_model = mat4_mul(mat4_translate(planet_position(planet)), mat4_rotate_z(planet->axial_tilt_deg));
                Mat3 ring_normal = mat3_from_mat4_inverse_transpose(ring_model);
                shader_set_mat4(ring_shader.u_model, &ring_model);
                shader_set_mat3(ring_shader.u_normal_matrix, &ring_normal);
                mesh_draw(&solar_system.ring_mesh);

                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_BLEND);
                glEnable(GL_CULL_FACE);

                shader_use(&planet_shader);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glfwSwapBuffers(window);

        ++fps_frames;
        ++frame_count;
        if (now - fps_timer >= 0.25) {
            fps = (float) fps_frames / (float) (now - fps_timer);
            fps_frames = 0;
            fps_timer = now;
            update_window_title(window, fps, speed, paused, camera_locked, target_name(&solar_system, target_index));
        }

        if (args.has_max_frames && frame_count >= args.max_frames) {
            if (args.frame_output) {
                save_framebuffer_png(args.frame_output, WINDOW_WIDTH, WINDOW_HEIGHT);
            }
            break;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
