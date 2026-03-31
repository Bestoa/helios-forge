#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_TEXTURES 10

typedef struct {
    float m[16];
} Mat4;

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
    float pos[3];
    float normal[3];
    float uv[2];
} Vertex;

typedef struct {
    Vertex *vertices;
    uint32_t vertex_count;
    uint32_t *indices;
    uint32_t index_count;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
} Geometry;

typedef struct {
    char name[16];
    char texture_name[64];
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
    OrbitFrame orbit_frame;
} Planet;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} Texture;

typedef struct {
    Planet planets[8];
    float sun_radius;
    Vec3 sun_color;
    float sun_rotation_period_days;
    float sun_rotation_angle_deg;
    Geometry sphere;
    Geometry ring;
    Geometry stars;
    Geometry orbits[8];
} SolarSystem;

typedef struct {
    float move_speed;
    float mouse_sensitivity;
    float yaw_deg;
    float pitch_deg;
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
    Mat4 view_proj;
    float light_pos[4];
    float camera_pos[4];
    float light_ambient[4];
    float light_diffuse[4];
    float light_specular[4];
} GlobalUBO;

typedef struct {
    Mat4 model;
    float color[4];
    float material_params1[4];
    float material_params2[4];
    int texture_index;
    int use_texture;
    int emissive;
    int reserved;
} PushConstants;

typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
    bool has_graphics;
    bool has_present;
} QueueFamilyIndices;

typedef struct {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats;
    uint32_t format_count;
    VkPresentModeKHR *present_modes;
    uint32_t present_mode_count;
} SwapchainSupportDetails;

typedef struct {
    GLFWwindow *window;
    bool framebuffer_resized;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_family;
    uint32_t present_family;

    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    VkImage *swapchain_images;
    uint32_t swapchain_image_count;
    VkImageView *swapchain_image_views;
    VkFramebuffer *swapchain_framebuffers;

    VkRenderPass render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout planet_pipeline_layout;
    VkPipelineLayout orbit_pipeline_layout;
    VkPipelineLayout ring_pipeline_layout;
    VkPipelineLayout star_pipeline_layout;
    VkPipeline planet_pipeline;
    VkPipeline orbit_pipeline;
    VkPipeline ring_pipeline;
    VkPipeline star_pipeline;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

    VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory uniform_memories[MAX_FRAMES_IN_FLIGHT];
    void *uniform_mapped[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];
    VkSampler texture_sampler;
    Texture textures[MAX_TEXTURES];

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_image_view;
    VkFormat depth_format;

    VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight[MAX_FRAMES_IN_FLIGHT];
    uint32_t current_frame;
} VulkanApp;

static Camera *g_scroll_camera = NULL;

static const char *k_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

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

static Vec3 vec3_scale(Vec3 v, float s) {
    return vec3(v.x * s, v.y * s, v.z * s);
}

static float deg_to_rad(float degrees) {
    return degrees * (float) M_PI / 180.0f;
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

static float vec3_length(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

static Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len < 1e-6f) {
        return vec3(0.0f, 0.0f, 0.0f);
    }
    return vec3_scale(v, 1.0f / len);
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
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

static Mat4 mat4_translate(Vec3 v) {
    Mat4 m = mat4_identity();
    m.m[12] = v.x;
    m.m[13] = v.y;
    m.m[14] = v.z;
    return m;
}

static Mat4 mat4_scale(float sx, float sy, float sz) {
    Mat4 m = mat4_identity();
    m.m[0] = sx;
    m.m[5] = sy;
    m.m[10] = sz;
    return m;
}

static Mat4 mat4_rotate_y(float deg) {
    float rad = deg * (float) M_PI / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 m = mat4_identity();
    m.m[0] = c;
    m.m[8] = s;
    m.m[2] = -s;
    m.m[10] = c;
    return m;
}

static Mat4 mat4_rotate_z(float deg) {
    float rad = deg * (float) M_PI / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 m = mat4_identity();
    m.m[0] = c;
    m.m[4] = -s;
    m.m[1] = s;
    m.m[5] = c;
    return m;
}

static Mat4 mat4_perspective(float fov_y_rad, float aspect, float near_z, float far_z) {
    float f = 1.0f / tanf(fov_y_rad * 0.5f);
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
    m.m[0] = s.x;
    m.m[1] = u.x;
    m.m[2] = -f.x;
    m.m[4] = s.y;
    m.m[5] = u.y;
    m.m[6] = -f.y;
    m.m[8] = s.z;
    m.m[9] = u.z;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

static void vk_check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "%s failed with VkResult=%d\n", what, (int) result);
        exit(EXIT_FAILURE);
    }
}

static void geometry_free_cpu(Geometry *geometry) {
    free(geometry->vertices);
    free(geometry->indices);
    geometry->vertices = NULL;
    geometry->indices = NULL;
}

static void geometry_destroy_gpu(VulkanApp *app, Geometry *geometry) {
    if (geometry->index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(app->device, geometry->index_buffer, NULL);
        vkFreeMemory(app->device, geometry->index_memory, NULL);
    }
    if (geometry->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(app->device, geometry->vertex_buffer, NULL);
        vkFreeMemory(app->device, geometry->vertex_memory, NULL);
    }
    memset(&geometry->vertex_buffer, 0, sizeof(VkBuffer));
    memset(&geometry->vertex_memory, 0, sizeof(VkDeviceMemory));
    memset(&geometry->index_buffer, 0, sizeof(VkBuffer));
    memset(&geometry->index_memory, 0, sizeof(VkDeviceMemory));
}

static void planet_init(
    Planet *planet,
    const char *name,
    const char *texture_name,
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
    Vec3 ring_color
) {
    memset(planet, 0, sizeof(*planet));
    snprintf(planet->name, sizeof(planet->name), "%s", name);
    snprintf(planet->texture_name, sizeof(planet->texture_name), "%s", texture_name);
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
    {
        float node = deg_to_rad(orbit_ascending_node_deg);
        float inclination = deg_to_rad(orbit_inclination_deg);
        float periapsis = deg_to_rad(orbit_periapsis_deg);
        float cos_node = cosf(node);
        float sin_node = sinf(node);
        float cos_inclination = cosf(inclination);
        float sin_inclination = sinf(inclination);
        float cos_periapsis = cosf(periapsis);
        float sin_periapsis = sinf(periapsis);
        planet->orbit_frame.periapsis_dir = vec3(
            cos_node * cos_periapsis - sin_node * sin_periapsis * cos_inclination,
            sin_periapsis * sin_inclination,
            sin_node * cos_periapsis + cos_node * sin_periapsis * cos_inclination
        );
        planet->orbit_frame.minor_dir = vec3(
            -cos_node * sin_periapsis - sin_node * cos_periapsis * cos_inclination,
            cos_periapsis * sin_inclination,
            -sin_node * sin_periapsis + cos_node * cos_periapsis * cos_inclination
        );
    }
}

static Vec3 planet_position(const Planet *planet) {
    float mean_anomaly = deg_to_rad(planet->orbit_mean_anomaly_deg);
    float eccentricity = planet->orbit_eccentricity;
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

    float cos_e = cosf(eccentric_anomaly);
    float sin_e = sinf(eccentric_anomaly);
    float radius = planet->orbit_radius * (1.0f - eccentricity * cos_e);
    float true_anomaly = atan2f(
        sqrtf(1.0f - eccentricity * eccentricity) * sin_e,
        cos_e - eccentricity
    );
    float cos_v = cosf(true_anomaly);
    float sin_v = sinf(true_anomaly);
    return vec3_add(
        vec3_scale(planet->orbit_frame.periapsis_dir, radius * cos_v),
        vec3_scale(planet->orbit_frame.minor_dir, radius * sin_v)
    );
}

static Mat4 planet_model_matrix(const Planet *planet) {
    Mat4 scale = mat4_scale(planet->size, planet->size, planet->size);
    Mat4 spin = mat4_rotate_y(planet->rotation_angle_deg);
    Mat4 tilt = mat4_rotate_z(planet->axial_tilt_deg);
    return mat4_mul(mat4_translate(planet_position(planet)), mat4_mul(tilt, mat4_mul(spin, scale)));
}

static void geometry_create_sphere(Geometry *geometry, float radius, int slices, int stacks) {
    memset(geometry, 0, sizeof(*geometry));
    geometry->vertex_count = (uint32_t) ((stacks + 1) * (slices + 1));
    geometry->vertices = (Vertex *) calloc(geometry->vertex_count, sizeof(Vertex));
    geometry->indices = (uint32_t *) malloc((size_t) stacks * (size_t) slices * 6 * sizeof(uint32_t));
    geometry->index_count = 0;

    uint32_t v = 0;
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
            geometry->vertices[v].pos[0] = radius * x;
            geometry->vertices[v].pos[1] = radius * y;
            geometry->vertices[v].pos[2] = radius * z;
            geometry->vertices[v].normal[0] = x;
            geometry->vertices[v].normal[1] = y;
            geometry->vertices[v].normal[2] = z;
            geometry->vertices[v].uv[0] = 1.0f - (float) j / (float) slices;
            geometry->vertices[v].uv[1] = (float) i / (float) stacks;
            ++v;
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t p1 = (uint32_t) (i * (slices + 1) + j);
            uint32_t p2 = p1 + (uint32_t) slices + 1;
            if (i > 0) {
                geometry->indices[geometry->index_count++] = p1;
                geometry->indices[geometry->index_count++] = p1 + 1;
                geometry->indices[geometry->index_count++] = p2;
            }
            if (i < stacks - 1) {
                geometry->indices[geometry->index_count++] = p1 + 1;
                geometry->indices[geometry->index_count++] = p2 + 1;
                geometry->indices[geometry->index_count++] = p2;
            }
        }
    }
}

static void geometry_create_ring(Geometry *geometry, float inner_radius, float outer_radius, int segments) {
    memset(geometry, 0, sizeof(*geometry));
    geometry->vertex_count = (uint32_t) (4 * (segments + 1));
    geometry->vertices = (Vertex *) calloc(geometry->vertex_count, sizeof(Vertex));
    geometry->indices = (uint32_t *) malloc((size_t) segments * 12 * sizeof(uint32_t));
    geometry->index_count = 0;

    uint32_t v = 0;
    for (int side_idx = 0; side_idx < 2; ++side_idx) {
        float side = side_idx == 0 ? -1.0f : 1.0f;
        for (int i = 0; i <= segments; ++i) {
            float theta = 2.0f * (float) M_PI * (float) i / (float) segments;
            float c = cosf(theta);
            float s = sinf(theta);

            geometry->vertices[v].pos[0] = inner_radius * c;
            geometry->vertices[v].pos[1] = 0.0f;
            geometry->vertices[v].pos[2] = inner_radius * s;
            geometry->vertices[v].normal[1] = side;
            geometry->vertices[v].uv[0] = 0.0f;
            geometry->vertices[v].uv[1] = (float) i / (float) segments;
            ++v;

            geometry->vertices[v].pos[0] = outer_radius * c;
            geometry->vertices[v].pos[1] = 0.0f;
            geometry->vertices[v].pos[2] = outer_radius * s;
            geometry->vertices[v].normal[1] = side;
            geometry->vertices[v].uv[0] = 1.0f;
            geometry->vertices[v].uv[1] = (float) i / (float) segments;
            ++v;
        }

        uint32_t base = side_idx == 0 ? 0u : (uint32_t) (2 * (segments + 1));
        for (int i = 0; i < segments; ++i) {
            uint32_t p1 = base + (uint32_t) (2 * i);
            uint32_t p2 = p1 + 1;
            uint32_t p3 = p1 + 2;
            uint32_t p4 = p1 + 3;
            if (side_idx == 1) {
                geometry->indices[geometry->index_count++] = p1;
                geometry->indices[geometry->index_count++] = p2;
                geometry->indices[geometry->index_count++] = p3;
                geometry->indices[geometry->index_count++] = p2;
                geometry->indices[geometry->index_count++] = p4;
                geometry->indices[geometry->index_count++] = p3;
            } else {
                geometry->indices[geometry->index_count++] = p2;
                geometry->indices[geometry->index_count++] = p1;
                geometry->indices[geometry->index_count++] = p4;
                geometry->indices[geometry->index_count++] = p1;
                geometry->indices[geometry->index_count++] = p3;
                geometry->indices[geometry->index_count++] = p4;
            }
        }
    }
}

static void geometry_create_orbit_path(Geometry *geometry, const Planet *planet, int segments) {
    memset(geometry, 0, sizeof(*geometry));
    geometry->vertex_count = (uint32_t) (segments + 1);
    geometry->vertices = (Vertex *) calloc(geometry->vertex_count, sizeof(Vertex));
    float semi_latus_rectum = planet->orbit_radius * (1.0f - planet->orbit_eccentricity * planet->orbit_eccentricity);
    for (int i = 0; i <= segments; ++i) {
        float true_anomaly = 2.0f * (float) M_PI * (float) i / (float) segments;
        float radius = semi_latus_rectum / (1.0f + planet->orbit_eccentricity * cosf(true_anomaly));
        float cos_v = cosf(true_anomaly);
        float sin_v = sinf(true_anomaly);
        Vec3 position = vec3_add(
            vec3_scale(planet->orbit_frame.periapsis_dir, radius * cos_v),
            vec3_scale(planet->orbit_frame.minor_dir, radius * sin_v)
        );
        geometry->vertices[i].pos[0] = position.x;
        geometry->vertices[i].pos[1] = position.y;
        geometry->vertices[i].pos[2] = position.z;
        geometry->vertices[i].normal[1] = 1.0f;
    }
}

static void geometry_create_stars(Geometry *geometry, int count, float radius) {
    memset(geometry, 0, sizeof(*geometry));
    geometry->vertex_count = (uint32_t) count;
    geometry->vertices = (Vertex *) calloc(geometry->vertex_count, sizeof(Vertex));
    srand(7);
    for (int i = 0; i < count; ++i) {
        float theta = randf_range(0.0f, 2.0f * (float) M_PI);
        float phi = randf_range(-(float) M_PI * 0.5f, (float) M_PI * 0.5f);
        float brightness = randf_range(0.35f, 1.0f);
        geometry->vertices[i].pos[0] = radius * cosf(phi) * cosf(theta);
        geometry->vertices[i].pos[1] = radius * sinf(phi);
        geometry->vertices[i].pos[2] = radius * cosf(phi) * sinf(theta);
        geometry->vertices[i].uv[0] = brightness;
        geometry->vertices[i].uv[1] = randf_range(1.5f, 3.8f);
    }
}

static void solar_system_init(SolarSystem *ss) {
    memset(ss, 0, sizeof(*ss));
    srand(42);
    planet_init(&ss->planets[0], "Mercury", "assets/textures/mercury.jpg", 4.0f, 0.2056f, 7.00f, 48.33f, 29.12f, 0.15f, vec3(0.70f, 0.70f, 0.70f), 88.0f, 10.0f, 0.03f, 58.65f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[1], "Venus", "assets/textures/venus.jpg", 5.8f, 0.0068f, 3.39f, 76.68f, 54.88f, 0.30f, vec3(0.90f, 0.70f, 0.30f), 225.0f, 75.0f, 177.36f, -243.02f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[2], "Earth", "assets/textures/earth.jpg", 8.0f, 0.0167f, 0.00f, -11.26f, 114.21f, 0.32f, vec3(0.20f, 0.50f, 1.00f), 365.0f, 135.0f, 23.44f, 0.997f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[3], "Mars", "assets/textures/mars.jpg", 10.5f, 0.0934f, 1.85f, 49.58f, 286.50f, 0.20f, vec3(0.90f, 0.30f, 0.20f), 687.0f, 195.0f, 25.19f, 1.03f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[4], "Jupiter", "assets/textures/jupiter.jpg", 14.5f, 0.0489f, 1.30f, 100.46f, 273.87f, 0.90f, vec3(0.80f, 0.60f, 0.40f), 4333.0f, 250.0f, 3.13f, 0.41f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[5], "Saturn", "assets/textures/saturn.jpg", 19.0f, 0.0565f, 2.49f, 113.67f, 339.39f, 0.80f, vec3(0.90f, 0.80f, 0.50f), 10759.0f, 305.0f, 26.73f, 0.45f, true, vec3(0.85f, 0.75f, 0.55f));
    planet_init(&ss->planets[6], "Uranus", "assets/textures/uranus.jpg", 23.0f, 0.0472f, 0.77f, 74.01f, 96.73f, 0.50f, vec3(0.50f, 0.80f, 0.90f), 30687.0f, 15.0f, 97.77f, -0.72f, false, vec3(0, 0, 0));
    planet_init(&ss->planets[7], "Neptune", "assets/textures/neptune.jpg", 27.5f, 0.0086f, 1.77f, 131.78f, 273.19f, 0.50f, vec3(0.20f, 0.30f, 0.90f), 60190.0f, 100.0f, 28.32f, 0.67f, false, vec3(0, 0, 0));
    ss->sun_radius = 1.5f;
    ss->sun_color = vec3(1.00f, 0.85f, 0.20f);
    ss->sun_rotation_period_days = 24.47f;
    ss->sun_rotation_angle_deg = randf_range(0.0f, 360.0f);

    geometry_create_sphere(&ss->sphere, 1.0f, 40, 28);
    geometry_create_ring(&ss->ring, 1.0f, 1.6f, 96);
    geometry_create_stars(&ss->stars, 1800, 220.0f);
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        geometry_create_orbit_path(&ss->orbits[i], &ss->planets[i], 192);
    }
}

static void solar_system_update(SolarSystem *ss, float dt, float speed) {
    const float orbit_day_scale = 60.0f;
    const float rotation_time_scale = 8.0f;
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        Planet *p = &ss->planets[i];
        if (p->orbit_days > 0.0f) {
            float orbit_speed = 360.0f / p->orbit_days;
            p->orbit_mean_anomaly_deg = fmodf(p->orbit_mean_anomaly_deg + orbit_speed * dt * speed * orbit_day_scale, 360.0f);
        }
        if (fabsf(p->rotation_period_days) > 1e-6f) {
            float spin_speed = 360.0f / (fabsf(p->rotation_period_days) * rotation_time_scale);
            if (p->rotation_period_days < 0.0f) {
                spin_speed = -spin_speed;
            }
            p->rotation_angle_deg = fmodf(p->rotation_angle_deg + spin_speed * dt * speed, 360.0f);
        }
    }
    float sun_speed = 360.0f / (ss->sun_rotation_period_days * rotation_time_scale);
    ss->sun_rotation_angle_deg = fmodf(ss->sun_rotation_angle_deg + sun_speed * dt * speed, 360.0f);
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

static void framebuffer_resize_callback(GLFWwindow *window, int width, int height) {
    (void) width;
    (void) height;
    VulkanApp *app = (VulkanApp *) glfwGetWindowUserPointer(window);
    if (app) {
        app->framebuffer_resized = true;
    }
}

static void update_window_title(GLFWwindow *window, float fps, float speed, bool paused, bool camera_locked, const char *focus_name) {
    char title[256];
    snprintf(
        title,
        sizeof(title),
        "Solar System Vulkan | FPS %.1f | Speed %.1fx%s | Camera %s -> %s | WASD move | Mouse drag look | Wheel zoom | L lock target | 0 Sun | 1-8 planets",
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

static QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = {0};
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = (VkQueueFamilyProperties *) malloc(count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
            indices.has_graphics = true;
        }
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support) {
            indices.present_family = i;
            indices.has_present = true;
        }
        if (indices.has_graphics && indices.has_present) {
            break;
        }
    }
    free(families);
    return indices;
}

static SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, NULL);
    if (details.format_count > 0) {
        details.formats = (VkSurfaceFormatKHR *) malloc(details.format_count * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, details.formats);
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, NULL);
    if (details.present_mode_count > 0) {
        details.present_modes = (VkPresentModeKHR *) malloc(details.present_mode_count * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, details.present_modes);
    }
    return details;
}

static void free_swapchain_support(SwapchainSupportDetails *details) {
    free(details->formats);
    free(details->present_modes);
    details->formats = NULL;
    details->present_modes = NULL;
}

static bool device_supports_extensions(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    VkExtensionProperties *available = (VkExtensionProperties *) malloc(count * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, available);

    size_t needed = ARRAY_LEN(k_device_extensions);
    for (uint32_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < ARRAY_LEN(k_device_extensions); ++j) {
            if (strcmp(available[i].extensionName, k_device_extensions[j]) == 0) {
                --needed;
                break;
            }
        }
    }
    free(available);
    return needed == 0;
}

static bool is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = find_queue_families(device, surface);
    if (!indices.has_graphics || !indices.has_present || !device_supports_extensions(device)) {
        return false;
    }
    SwapchainSupportDetails support = query_swapchain_support(device, surface);
    bool ok = support.format_count > 0 && support.present_mode_count > 0;
    free_swapchain_support(&support);
    return ok;
}

static uint32_t find_memory_type(VulkanApp *app, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(app->physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "Failed to find suitable Vulkan memory type.\n");
    exit(EXIT_FAILURE);
}

static void create_buffer(
    VulkanApp *app,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer *buffer,
    VkDeviceMemory *memory
) {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vk_check(vkCreateBuffer(app->device, &buffer_info, NULL, buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(app->device, *buffer, &requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = find_memory_type(app, requirements.memoryTypeBits, properties),
    };
    vk_check(vkAllocateMemory(app->device, &alloc_info, NULL, memory), "vkAllocateMemory(buffer)");
    vk_check(vkBindBufferMemory(app->device, *buffer, *memory, 0), "vkBindBufferMemory");
}

static VkCommandBuffer begin_one_time_commands(VulkanApp *app) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vk_check(vkAllocateCommandBuffers(app->device, &alloc_info, &cmd), "vkAllocateCommandBuffers(one_time)");
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_check(vkBeginCommandBuffer(cmd, &begin_info), "vkBeginCommandBuffer(one_time)");
    return cmd;
}

static void end_one_time_commands(VulkanApp *app, VkCommandBuffer cmd) {
    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(one_time)");
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vk_check(vkQueueSubmit(app->graphics_queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit(one_time)");
    vk_check(vkQueueWaitIdle(app->graphics_queue), "vkQueueWaitIdle(one_time)");
    vkFreeCommandBuffers(app->device, app->command_pool, 1, &cmd);
}

static void transition_image_layout(
    VulkanApp *app,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout
) {
    VkCommandBuffer cmd = begin_one_time_commands(app);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        fprintf(stderr, "Unsupported image layout transition.\n");
        exit(EXIT_FAILURE);
    }

    vkCmdPipelineBarrier(
        cmd,
        src_stage,
        dst_stage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );
    end_one_time_commands(app, cmd);
}

static void copy_buffer_to_image(VulkanApp *app, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer cmd = begin_one_time_commands(app);
    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    end_one_time_commands(app, cmd);
}

static void upload_geometry(VulkanApp *app, Geometry *geometry) {
    VkDeviceSize vertex_bytes = sizeof(Vertex) * geometry->vertex_count;
    create_buffer(
        app,
        vertex_bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &geometry->vertex_buffer,
        &geometry->vertex_memory
    );
    void *mapped = NULL;
    vk_check(vkMapMemory(app->device, geometry->vertex_memory, 0, vertex_bytes, 0, &mapped), "vkMapMemory(vertex)");
    memcpy(mapped, geometry->vertices, (size_t) vertex_bytes);
    vkUnmapMemory(app->device, geometry->vertex_memory);

    if (geometry->index_count > 0) {
        VkDeviceSize index_bytes = sizeof(uint32_t) * geometry->index_count;
        create_buffer(
            app,
            index_bytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &geometry->index_buffer,
            &geometry->index_memory
        );
        vk_check(vkMapMemory(app->device, geometry->index_memory, 0, index_bytes, 0, &mapped), "vkMapMemory(index)");
        memcpy(mapped, geometry->indices, (size_t) index_bytes);
        vkUnmapMemory(app->device, geometry->index_memory);
    }
}

static VkSurfaceFormatKHR choose_swap_surface_format(const SwapchainSupportDetails *support) {
    for (uint32_t i = 0; i < support->format_count; ++i) {
        if (support->formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            support->formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return support->formats[i];
        }
    }
    for (uint32_t i = 0; i < support->format_count; ++i) {
        if (support->formats[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
            support->formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return support->formats[i];
        }
    }
    return support->formats[0];
}

static VkPresentModeKHR choose_present_mode(const SwapchainSupportDetails *support) {
    for (uint32_t i = 0; i < support->present_mode_count; ++i) {
        if (support->present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(const SwapchainSupportDetails *support, GLFWwindow *window) {
    if (support->capabilities.currentExtent.width != UINT32_MAX) {
        return support->capabilities.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D extent = {
        .width = (uint32_t) width,
        .height = (uint32_t) height
    };
    if (extent.width < support->capabilities.minImageExtent.width) {
        extent.width = support->capabilities.minImageExtent.width;
    }
    if (extent.width > support->capabilities.maxImageExtent.width) {
        extent.width = support->capabilities.maxImageExtent.width;
    }
    if (extent.height < support->capabilities.minImageExtent.height) {
        extent.height = support->capabilities.minImageExtent.height;
    }
    if (extent.height > support->capabilities.maxImageExtent.height) {
        extent.height = support->capabilities.maxImageExtent.height;
    }
    return extent;
}

static VkFormat find_supported_format(
    VulkanApp *app,
    const VkFormat *candidates,
    size_t candidate_count,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
) {
    for (size_t i = 0; i < candidate_count; ++i) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(app->physical_device, candidates[i], &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return candidates[i];
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return candidates[i];
        }
    }
    fprintf(stderr, "Failed to find supported Vulkan format.\n");
    exit(EXIT_FAILURE);
}

static VkFormat find_depth_format(VulkanApp *app) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    return find_supported_format(app, candidates, ARRAY_LEN(candidates), VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static void create_image(
    VulkanApp *app,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage *image,
    VkDeviceMemory *memory
) {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vk_check(vkCreateImage(app->device, &image_info, NULL, image), "vkCreateImage");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(app->device, *image, &requirements);
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = find_memory_type(app, requirements.memoryTypeBits, properties),
    };
    vk_check(vkAllocateMemory(app->device, &alloc_info, NULL, memory), "vkAllocateMemory(image)");
    vk_check(vkBindImageMemory(app->device, *image, *memory, 0), "vkBindImageMemory");
}

static VkImageView create_image_view(VulkanApp *app, VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange.aspectMask = aspect,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };
    VkImageView view = VK_NULL_HANDLE;
    vk_check(vkCreateImageView(app->device, &view_info, NULL, &view), "vkCreateImageView");
    return view;
}

static void create_texture_from_file_with_format(VulkanApp *app, const char *path, VkFormat format, Texture *texture);

static void create_texture_from_file(VulkanApp *app, const char *path, Texture *texture) {
    create_texture_from_file_with_format(app, path, VK_FORMAT_R8G8B8A8_SRGB, texture);
}

static void create_texture_from_file_with_format(VulkanApp *app, const char *path, VkFormat format, Texture *texture) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        fprintf(stderr, "Failed to load texture: %s\n", path);
        exit(EXIT_FAILURE);
    }

    VkDeviceSize image_size = (VkDeviceSize) width * (VkDeviceSize) height * 4;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    create_buffer(
        app,
        image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buffer,
        &staging_memory
    );

    void *mapped = NULL;
    vk_check(vkMapMemory(app->device, staging_memory, 0, image_size, 0, &mapped), "vkMapMemory(texture staging)");
    memcpy(mapped, pixels, (size_t) image_size);
    vkUnmapMemory(app->device, staging_memory);
    stbi_image_free(pixels);

    create_image(
        app,
        (uint32_t) width,
        (uint32_t) height,
        format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &texture->image,
        &texture->memory
    );
    transition_image_layout(app, texture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(app, staging_buffer, texture->image, (uint32_t) width, (uint32_t) height);
    transition_image_layout(app, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture->view = create_image_view(app, texture->image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    vkDestroyBuffer(app->device, staging_buffer, NULL);
    vkFreeMemory(app->device, staging_memory, NULL);
}

static void destroy_texture(VulkanApp *app, Texture *texture) {
    if (texture->view != VK_NULL_HANDLE) {
        vkDestroyImageView(app->device, texture->view, NULL);
    }
    if (texture->image != VK_NULL_HANDLE) {
        vkDestroyImage(app->device, texture->image, NULL);
        vkFreeMemory(app->device, texture->memory, NULL);
    }
    memset(texture, 0, sizeof(*texture));
}

static char *read_binary_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        exit(EXIT_FAILURE);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *data = (char *) malloc((size_t) size);
    if (!data) {
        fclose(file);
        fprintf(stderr, "Failed to allocate file buffer: %s\n", path);
        exit(EXIT_FAILURE);
    }
    if (fread(data, 1, (size_t) size, file) != (size_t) size) {
        fclose(file);
        free(data);
        fprintf(stderr, "Failed to read file: %s\n", path);
        exit(EXIT_FAILURE);
    }
    fclose(file);
    *size_out = (size_t) size;
    return data;
}

static VkShaderModule load_shader_module(VulkanApp *app, const char *path) {
    size_t size = 0;
    char *code = read_binary_file(path, &size);
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const uint32_t *) code,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(app->device, &create_info, NULL, &module), "vkCreateShaderModule");
    free(code);
    return module;
}

static void create_instance(VulkanApp *app) {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Solar System Vulkan",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "None",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    uint32_t extension_count = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&extension_count);

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    vk_check(vkCreateInstance(&create_info, NULL, &app->instance), "vkCreateInstance");
}

static void pick_physical_device(VulkanApp *app) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(app->instance, &device_count, NULL);
    if (device_count == 0) {
        fprintf(stderr, "No Vulkan-compatible GPU found.\n");
        exit(EXIT_FAILURE);
    }
    VkPhysicalDevice *devices = (VkPhysicalDevice *) malloc(device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(app->instance, &device_count, devices);
    for (uint32_t i = 0; i < device_count; ++i) {
        if (is_device_suitable(devices[i], app->surface)) {
            app->physical_device = devices[i];
            break;
        }
    }
    free(devices);
    if (app->physical_device == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to find a suitable Vulkan physical device.\n");
        exit(EXIT_FAILURE);
    }
}

static void create_logical_device(VulkanApp *app) {
    QueueFamilyIndices indices = find_queue_families(app->physical_device, app->surface);
    app->graphics_family = indices.graphics_family;
    app->present_family = indices.present_family;

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 0;

    queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.graphics_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };
    if (indices.present_family != indices.graphics_family) {
        queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.present_family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
    }

    VkPhysicalDeviceFeatures features = {0};
    features.samplerAnisotropy = VK_FALSE;

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = (uint32_t) ARRAY_LEN(k_device_extensions),
        .ppEnabledExtensionNames = k_device_extensions,
    };

    vk_check(vkCreateDevice(app->physical_device, &create_info, NULL, &app->device), "vkCreateDevice");
    vkGetDeviceQueue(app->device, app->graphics_family, 0, &app->graphics_queue);
    vkGetDeviceQueue(app->device, app->present_family, 0, &app->present_queue);
}

static void create_render_pass(VulkanApp *app) {
    VkAttachmentDescription color_attachment = {
        .format = app->swapchain_image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentDescription depth_attachment = {
        .format = app->depth_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = (uint32_t) ARRAY_LEN(attachments),
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    vk_check(vkCreateRenderPass(app->device, &render_pass_info, NULL, &app->render_pass), "vkCreateRenderPass");
}

static void create_descriptor_set_layout(VulkanApp *app) {
    VkDescriptorSetLayoutBinding ubo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutBinding texture_binding = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_TEXTURES,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutBinding bindings[] = {ubo_binding, texture_binding};
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t) ARRAY_LEN(bindings),
        .pBindings = bindings,
    };
    vk_check(vkCreateDescriptorSetLayout(app->device, &layout_info, NULL, &app->descriptor_set_layout), "vkCreateDescriptorSetLayout");
}

static VkVertexInputBindingDescription vertex_binding_description(void) {
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    return binding;
}

static void vertex_attribute_descriptions(VkVertexInputAttributeDescription attrs[3]) {
    attrs[0] = (VkVertexInputAttributeDescription) {
        .binding = 0,
        .location = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, pos),
    };
    attrs[1] = (VkVertexInputAttributeDescription) {
        .binding = 0,
        .location = 1,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, normal),
    };
    attrs[2] = (VkVertexInputAttributeDescription) {
        .binding = 0,
        .location = 2,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(Vertex, uv),
    };
}

static void create_pipeline_common(
    VulkanApp *app,
    const char *vert_path,
    const char *frag_path,
    VkPrimitiveTopology topology,
    VkBool32 enable_depth,
    VkBool32 write_depth,
    VkBool32 enable_blend,
    VkCullModeFlags cull_mode,
    VkFrontFace front_face,
    VkPolygonMode polygon_mode,
    VkPipelineLayout *pipeline_layout,
    VkPipeline *pipeline
) {
    VkShaderModule vert = load_shader_module(app, vert_path);
    VkShaderModule frag = load_shader_module(app, frag_path);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        }
    };

    VkVertexInputBindingDescription binding = vertex_binding_description();
    VkVertexInputAttributeDescription attrs[3];
    vertex_attribute_descriptions(attrs);

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = polygon_mode,
        .lineWidth = 1.0f,
        .cullMode = cull_mode,
        .frontFace = front_face,
        .depthBiasEnable = VK_FALSE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = enable_depth,
        .depthWriteEnable = write_depth,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = enable_blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (uint32_t) ARRAY_LEN(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &app->descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    vk_check(vkCreatePipelineLayout(app->device, &layout_info, NULL, pipeline_layout), "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = *pipeline_layout,
        .renderPass = app->render_pass,
        .subpass = 0,
    };
    vk_check(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline), "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(app->device, frag, NULL);
    vkDestroyShaderModule(app->device, vert, NULL);
}

static void create_pipelines(VulkanApp *app) {
    create_pipeline_common(
        app,
        "shaders/vulkan_planet.vert.spv",
        "shaders/vulkan_planet.frag.spv",
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_TRUE,
        VK_TRUE,
        VK_FALSE,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_POLYGON_MODE_FILL,
        &app->planet_pipeline_layout,
        &app->planet_pipeline
    );
    create_pipeline_common(
        app,
        "shaders/vulkan_orbit.vert.spv",
        "shaders/vulkan_orbit.frag.spv",
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        VK_FALSE,
        VK_FALSE,
        VK_FALSE,
        VK_CULL_MODE_NONE,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_POLYGON_MODE_FILL,
        &app->orbit_pipeline_layout,
        &app->orbit_pipeline
    );
    create_pipeline_common(
        app,
        "shaders/vulkan_ring.vert.spv",
        "shaders/vulkan_ring.frag.spv",
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_TRUE,
        VK_FALSE,
        VK_TRUE,
        VK_CULL_MODE_NONE,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_POLYGON_MODE_FILL,
        &app->ring_pipeline_layout,
        &app->ring_pipeline
    );
    create_pipeline_common(
        app,
        "shaders/vulkan_star.vert.spv",
        "shaders/vulkan_star.frag.spv",
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_FALSE,
        VK_FALSE,
        VK_TRUE,
        VK_CULL_MODE_NONE,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_POLYGON_MODE_FILL,
        &app->star_pipeline_layout,
        &app->star_pipeline
    );
}

static void cleanup_swapchain(VulkanApp *app) {
    if (app->depth_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(app->device, app->depth_image_view, NULL);
        app->depth_image_view = VK_NULL_HANDLE;
    }
    if (app->depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(app->device, app->depth_image, NULL);
        vkFreeMemory(app->device, app->depth_memory, NULL);
        app->depth_image = VK_NULL_HANDLE;
        app->depth_memory = VK_NULL_HANDLE;
    }

    if (app->swapchain_framebuffers) {
        for (uint32_t i = 0; i < app->swapchain_image_count; ++i) {
            if (app->swapchain_framebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(app->device, app->swapchain_framebuffers[i], NULL);
            }
        }
        free(app->swapchain_framebuffers);
        app->swapchain_framebuffers = NULL;
    }
    if (app->swapchain_image_views) {
        for (uint32_t i = 0; i < app->swapchain_image_count; ++i) {
            if (app->swapchain_image_views[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(app->device, app->swapchain_image_views[i], NULL);
            }
        }
        free(app->swapchain_image_views);
        app->swapchain_image_views = NULL;
    }
    free(app->swapchain_images);
    app->swapchain_images = NULL;

    if (app->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
        app->swapchain = VK_NULL_HANDLE;
    }
}

static void create_swapchain(VulkanApp *app) {
    SwapchainSupportDetails support = query_swapchain_support(app->physical_device, app->surface);
    VkSurfaceFormatKHR format = choose_swap_surface_format(&support);
    VkPresentModeKHR present_mode = choose_present_mode(&support);
    VkExtent2D extent = choose_extent(&support, app->window);

    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    uint32_t queue_family_indices[] = {app->graphics_family, app->present_family};
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = support.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
    };

    if (app->graphics_family != app->present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    vk_check(vkCreateSwapchainKHR(app->device, &create_info, NULL, &app->swapchain), "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(app->device, app->swapchain, &image_count, NULL);
    app->swapchain_images = (VkImage *) malloc(image_count * sizeof(VkImage));
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &image_count, app->swapchain_images);
    app->swapchain_image_count = image_count;
    app->swapchain_image_format = format.format;
    app->swapchain_extent = extent;

    app->swapchain_image_views = (VkImageView *) calloc(image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < image_count; ++i) {
        app->swapchain_image_views[i] = create_image_view(app, app->swapchain_images[i], app->swapchain_image_format, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    free_swapchain_support(&support);
}

static void create_depth_resources(VulkanApp *app) {
    app->depth_format = find_depth_format(app);
    create_image(
        app,
        app->swapchain_extent.width,
        app->swapchain_extent.height,
        app->depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &app->depth_image,
        &app->depth_memory
    );
    app->depth_image_view = create_image_view(app, app->depth_image, app->depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
}

static void create_framebuffers(VulkanApp *app) {
    app->swapchain_framebuffers = (VkFramebuffer *) calloc(app->swapchain_image_count, sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < app->swapchain_image_count; ++i) {
        VkImageView attachments[] = {
            app->swapchain_image_views[i],
            app->depth_image_view
        };
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = app->render_pass,
            .attachmentCount = (uint32_t) ARRAY_LEN(attachments),
            .pAttachments = attachments,
            .width = app->swapchain_extent.width,
            .height = app->swapchain_extent.height,
            .layers = 1,
        };
        vk_check(vkCreateFramebuffer(app->device, &framebuffer_info, NULL, &app->swapchain_framebuffers[i]), "vkCreateFramebuffer");
    }
}

static void recreate_swapchain(VulkanApp *app) {
    int width = 0;
    int height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(app->window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(app->device);
    cleanup_swapchain(app);
    create_swapchain(app);
    create_depth_resources(app);
    create_framebuffers(app);
}

static void create_command_pool(VulkanApp *app) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = app->graphics_family,
    };
    vk_check(vkCreateCommandPool(app->device, &pool_info, NULL, &app->command_pool), "vkCreateCommandPool");
}

static void create_texture_sampler(VulkanApp *app) {
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    vk_check(vkCreateSampler(app->device, &sampler_info, NULL, &app->texture_sampler), "vkCreateSampler");
}

static void create_uniform_buffers(VulkanApp *app) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(
            app,
            sizeof(GlobalUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &app->uniform_buffers[i],
            &app->uniform_memories[i]
        );
        vk_check(vkMapMemory(app->device, app->uniform_memories[i], 0, sizeof(GlobalUBO), 0, &app->uniform_mapped[i]), "vkMapMemory(ubo)");
    }
}

static void load_scene_textures(VulkanApp *app, const SolarSystem *ss) {
    create_texture_from_file(app, "assets/textures/sun.jpg", &app->textures[0]);
    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        create_texture_from_file(app, ss->planets[i].texture_name, &app->textures[i + 1]);
    }
    create_texture_from_file_with_format(app, "assets/textures/saturn_ring_alpha.png", VK_FORMAT_R8G8B8A8_UNORM, &app->textures[9]);
}

static void create_descriptor_pool_and_sets(VulkanApp *app) {
    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES,
        }
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = (uint32_t) ARRAY_LEN(pool_sizes),
        .pPoolSizes = pool_sizes,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
    };
    vk_check(vkCreateDescriptorPool(app->device, &pool_info, NULL, &app->descriptor_pool), "vkCreateDescriptorPool");

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        layouts[i] = app->descriptor_set_layout;
    }
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = app->descriptor_pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = layouts,
    };
    vk_check(vkAllocateDescriptorSets(app->device, &alloc_info, app->descriptor_sets), "vkAllocateDescriptorSets");

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo buffer_info = {
            .buffer = app->uniform_buffers[i],
            .offset = 0,
            .range = sizeof(GlobalUBO),
        };
        VkDescriptorImageInfo image_infos[MAX_TEXTURES];
        for (size_t j = 0; j < MAX_TEXTURES; ++j) {
            image_infos[j] = (VkDescriptorImageInfo) {
                .sampler = app->texture_sampler,
                .imageView = app->textures[j].view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        }
        VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = app->descriptor_sets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &buffer_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = app->descriptor_sets[i],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MAX_TEXTURES,
            .pImageInfo = image_infos,
        }
        };
        vkUpdateDescriptorSets(app->device, (uint32_t) ARRAY_LEN(writes), writes, 0, NULL);
    }
}

static void create_command_buffers(VulkanApp *app) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    vk_check(vkAllocateCommandBuffers(app->device, &alloc_info, app->command_buffers), "vkAllocateCommandBuffers");
}

static void create_sync_objects(VulkanApp *app) {
    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_check(vkCreateSemaphore(app->device, &sem_info, NULL, &app->image_available[i]), "vkCreateSemaphore(image_available)");
        vk_check(vkCreateSemaphore(app->device, &sem_info, NULL, &app->render_finished[i]), "vkCreateSemaphore(render_finished)");
        vk_check(vkCreateFence(app->device, &fence_info, NULL, &app->in_flight[i]), "vkCreateFence");
    }
}

static void record_push_constants(
    VkCommandBuffer cmd,
    VkPipelineLayout layout,
    Mat4 model,
    Vec3 color,
    Vec3 material_ambient,
    Vec3 material_specular,
    float material_shininess,
    float alpha,
    int texture_index,
    int use_texture,
    int emissive
) {
    PushConstants push = {0};
    push.model = model;
    push.color[0] = color.x;
    push.color[1] = color.y;
    push.color[2] = color.z;
    push.color[3] = alpha;
    push.material_params1[0] = material_ambient.x;
    push.material_params1[1] = material_ambient.y;
    push.material_params1[2] = material_ambient.z;
    push.material_params1[3] = material_shininess;
    push.material_params2[0] = material_specular.x;
    push.material_params2[1] = material_specular.y;
    push.material_params2[2] = material_specular.z;
    push.material_params2[3] = 0.0f;
    push.texture_index = texture_index;
    push.use_texture = use_texture;
    push.emissive = emissive;
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
}

static void record_geometry_draw(
    VkCommandBuffer cmd,
    Geometry *geometry
) {
    VkBuffer vertex_buffers[] = {geometry->vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
    if (geometry->index_count > 0) {
        vkCmdBindIndexBuffer(cmd, geometry->index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, geometry->index_count, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, geometry->vertex_count, 1, 0, 0);
    }
}

static void record_command_buffer(VulkanApp *app, VkCommandBuffer cmd, uint32_t image_index, SolarSystem *ss) {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vk_check(vkBeginCommandBuffer(cmd, &begin_info), "vkBeginCommandBuffer");

    VkClearValue clears[2];
    clears[0].color = (VkClearColorValue) {{0.0f, 0.0f, 0.01f, 1.0f}};
    clears[1].depthStencil = (VkClearDepthStencilValue) {1.0f, 0};

    VkRenderPassBeginInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = app->render_pass,
        .framebuffer = app->swapchain_framebuffers[image_index],
        .renderArea.offset = {0, 0},
        .renderArea.extent = app->swapchain_extent,
        .clearValueCount = 2,
        .pClearValues = clears,
    };
    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float) app->swapchain_extent.height,
        .width = (float) app->swapchain_extent.width,
        .height = -(float) app->swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = app->swapchain_extent,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->star_pipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->star_pipeline_layout,
        0,
        1,
        &app->descriptor_sets[app->current_frame],
        0,
        NULL
    );
    record_push_constants(
        cmd,
        app->star_pipeline_layout,
        mat4_identity(),
        vec3(0.92f, 0.96f, 1.0f),
        vec3(0.0f, 0.0f, 0.0f),
        vec3(0.0f, 0.0f, 0.0f),
        1.0f,
        1.0f,
        0,
        0,
        0
    );
    record_geometry_draw(cmd, &ss->stars);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->planet_pipeline_layout,
        0,
        1,
        &app->descriptor_sets[app->current_frame],
        0,
        NULL
    );

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->orbit_pipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->orbit_pipeline_layout,
        0,
        1,
        &app->descriptor_sets[app->current_frame],
        0,
        NULL
    );
    for (size_t i = 0; i < ARRAY_LEN(ss->orbits); ++i) {
        record_push_constants(
            cmd,
            app->orbit_pipeline_layout,
            mat4_identity(),
            vec3(0.25f, 0.25f, 0.35f),
            vec3(0.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.0f, 0.0f),
            1.0f,
            1.0f,
            0,
            0,
            0
        );
        record_geometry_draw(cmd, &ss->orbits[i]);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->planet_pipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->planet_pipeline_layout,
        0,
        1,
        &app->descriptor_sets[app->current_frame],
        0,
        NULL
    );

    Mat4 sun_model = mat4_mul(mat4_rotate_y(ss->sun_rotation_angle_deg), mat4_scale(ss->sun_radius, ss->sun_radius, ss->sun_radius));
    record_push_constants(
        cmd,
        app->planet_pipeline_layout,
        sun_model,
        ss->sun_color,
        vec3(0.0f, 0.0f, 0.0f),
        vec3(0.0f, 0.0f, 0.0f),
        1.0f,
        1.0f,
        0,
        1,
        1
    );
    record_geometry_draw(cmd, &ss->sphere);

    for (size_t i = 0; i < ARRAY_LEN(ss->planets); ++i) {
        Planet *planet = &ss->planets[i];
        record_push_constants(
            cmd,
            app->planet_pipeline_layout,
            planet_model_matrix(planet),
            planet->color,
            planet->color,
            vec3(0.01f, 0.01f, 0.01f),
            10.0f,
            0.0f,
            (int) i + 1,
            1,
            0
        );
        record_geometry_draw(cmd, &ss->sphere);
        if (planet->has_ring) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->ring_pipeline);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                app->ring_pipeline_layout,
                0,
                1,
                &app->descriptor_sets[app->current_frame],
                0,
                NULL
            );
            Mat4 ring_model = mat4_mul(mat4_translate(planet_position(planet)), mat4_rotate_z(planet->axial_tilt_deg));
            record_push_constants(
                cmd,
                app->ring_pipeline_layout,
                ring_model,
                planet->ring_color,
                vec3(0.0f, 0.0f, 0.0f),
                vec3(0.0f, 0.0f, 0.0f),
                1.0f,
                0.8f,
                9,
                1,
                0
            );
            record_geometry_draw(cmd, &ss->ring);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->planet_pipeline);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                app->planet_pipeline_layout,
                0,
                1,
                &app->descriptor_sets[app->current_frame],
                0,
                NULL
            );
        }
    }

    vkCmdEndRenderPass(cmd);
    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
}

static void update_uniform_buffer(VulkanApp *app, const Camera *camera) {
    GlobalUBO ubo = {0};
    float aspect = (float) app->swapchain_extent.width / (float) app->swapchain_extent.height;
    Mat4 proj = mat4_perspective(45.0f * (float) M_PI / 180.0f, aspect, 0.5f, 500.0f);
    ubo.view_proj = mat4_mul(proj, camera->view_matrix);
    ubo.light_pos[0] = 0.0f;
    ubo.light_pos[1] = 0.0f;
    ubo.light_pos[2] = 0.0f;
    ubo.light_pos[3] = 1.0f;
    ubo.camera_pos[0] = camera->eye.x;
    ubo.camera_pos[1] = camera->eye.y;
    ubo.camera_pos[2] = camera->eye.z;
    ubo.camera_pos[3] = 1.0f;
    ubo.light_ambient[0] = 0.02f;
    ubo.light_ambient[1] = 0.02f;
    ubo.light_ambient[2] = 0.02f;
    ubo.light_ambient[3] = 0.0f;
    ubo.light_diffuse[0] = 1.34f;
    ubo.light_diffuse[1] = 1.26f;
    ubo.light_diffuse[2] = 1.16f;
    ubo.light_diffuse[3] = 0.0f;
    ubo.light_specular[0] = 0.32f;
    ubo.light_specular[1] = 0.32f;
    ubo.light_specular[2] = 0.32f;
    ubo.light_specular[3] = 0.0f;
    memcpy(app->uniform_mapped[app->current_frame], &ubo, sizeof(ubo));
}

static void draw_frame(VulkanApp *app, SolarSystem *ss, const Camera *camera) {
    vk_check(vkWaitForFences(app->device, 1, &app->in_flight[app->current_frame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

    uint32_t image_index = 0;
    VkResult acquire = vkAcquireNextImageKHR(
        app->device,
        app->swapchain,
        UINT64_MAX,
        app->image_available[app->current_frame],
        VK_NULL_HANDLE,
        &image_index
    );
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(app);
        return;
    }
    vk_check(acquire, "vkAcquireNextImageKHR");

    update_uniform_buffer(app, camera);

    vk_check(vkResetFences(app->device, 1, &app->in_flight[app->current_frame]), "vkResetFences");
    vk_check(vkResetCommandBuffer(app->command_buffers[app->current_frame], 0), "vkResetCommandBuffer");
    record_command_buffer(app, app->command_buffers[app->current_frame], image_index, ss);

    VkSemaphore wait_semaphores[] = {app->image_available[app->current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {app->render_finished[app->current_frame]};
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_semaphores,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->command_buffers[app->current_frame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_semaphores,
    };
    vk_check(vkQueueSubmit(app->graphics_queue, 1, &submit_info, app->in_flight[app->current_frame]), "vkQueueSubmit");

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signal_semaphores,
        .swapchainCount = 1,
        .pSwapchains = &app->swapchain,
        .pImageIndices = &image_index,
    };
    VkResult present = vkQueuePresentKHR(app->present_queue, &present_info);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || app->framebuffer_resized) {
        app->framebuffer_resized = false;
        recreate_swapchain(app);
    } else {
        vk_check(present, "vkQueuePresentKHR");
    }

    app->current_frame = (app->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

static void init_window(VulkanApp *app) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    app->window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Solar System Vulkan", NULL, NULL);
    if (!app->window) {
        fprintf(stderr, "Failed to create GLFW window.\n");
        exit(EXIT_FAILURE);
    }
    glfwSetWindowUserPointer(app->window, app);
    glfwSetFramebufferSizeCallback(app->window, framebuffer_resize_callback);
}

static void init_vulkan(VulkanApp *app, const SolarSystem *ss) {
    create_instance(app);
    vk_check(glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface), "glfwCreateWindowSurface");
    pick_physical_device(app);
    create_logical_device(app);
    create_swapchain(app);
    create_descriptor_set_layout(app);
    app->depth_format = find_depth_format(app);
    create_render_pass(app);
    create_pipelines(app);
    create_command_pool(app);
    create_texture_sampler(app);
    load_scene_textures(app, ss);
    create_depth_resources(app);
    create_framebuffers(app);
    create_uniform_buffers(app);
    create_descriptor_pool_and_sets(app);
    create_command_buffers(app);
    create_sync_objects(app);
}

static void upload_scene_geometry(VulkanApp *app, SolarSystem *ss) {
    upload_geometry(app, &ss->sphere);
    upload_geometry(app, &ss->ring);
    upload_geometry(app, &ss->stars);
    for (size_t i = 0; i < ARRAY_LEN(ss->orbits); ++i) {
        upload_geometry(app, &ss->orbits[i]);
    }
}

static void free_scene_cpu_geometry(SolarSystem *ss) {
    geometry_free_cpu(&ss->sphere);
    geometry_free_cpu(&ss->ring);
    geometry_free_cpu(&ss->stars);
    for (size_t i = 0; i < ARRAY_LEN(ss->orbits); ++i) {
        geometry_free_cpu(&ss->orbits[i]);
    }
}

static void destroy_scene_gpu_geometry(VulkanApp *app, SolarSystem *ss) {
    geometry_destroy_gpu(app, &ss->sphere);
    geometry_destroy_gpu(app, &ss->ring);
    geometry_destroy_gpu(app, &ss->stars);
    for (size_t i = 0; i < ARRAY_LEN(ss->orbits); ++i) {
        geometry_destroy_gpu(app, &ss->orbits[i]);
    }
}

static void cleanup(VulkanApp *app, SolarSystem *ss) {
    if (app->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(app->device);
    }

    destroy_scene_gpu_geometry(app, ss);
    for (size_t i = 0; i < MAX_TEXTURES; ++i) {
        destroy_texture(app, &app->textures[i]);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (app->uniform_mapped[i]) {
            vkUnmapMemory(app->device, app->uniform_memories[i]);
        }
        if (app->uniform_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(app->device, app->uniform_buffers[i], NULL);
            vkFreeMemory(app->device, app->uniform_memories[i], NULL);
        }
        if (app->image_available[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(app->device, app->image_available[i], NULL);
        }
        if (app->render_finished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(app->device, app->render_finished[i], NULL);
        }
        if (app->in_flight[i] != VK_NULL_HANDLE) {
            vkDestroyFence(app->device, app->in_flight[i], NULL);
        }
    }

    if (app->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app->device, app->descriptor_pool, NULL);
    }
    if (app->texture_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(app->device, app->texture_sampler, NULL);
    }

    cleanup_swapchain(app);

    if (app->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(app->device, app->command_pool, NULL);
    }
    if (app->planet_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->planet_pipeline, NULL);
    }
    if (app->orbit_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->orbit_pipeline, NULL);
    }
    if (app->ring_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->ring_pipeline, NULL);
    }
    if (app->star_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->star_pipeline, NULL);
    }
    if (app->planet_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->planet_pipeline_layout, NULL);
    }
    if (app->orbit_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->orbit_pipeline_layout, NULL);
    }
    if (app->ring_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->ring_pipeline_layout, NULL);
    }
    if (app->star_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->star_pipeline_layout, NULL);
    }
    if (app->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(app->device, app->render_pass, NULL);
    }
    if (app->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(app->device, app->descriptor_set_layout, NULL);
    }
    if (app->device != VK_NULL_HANDLE) {
        vkDestroyDevice(app->device, NULL);
    }
    if (app->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(app->instance, app->surface, NULL);
    }
    if (app->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(app->instance, NULL);
    }
    if (app->window) {
        glfwDestroyWindow(app->window);
    }
    glfwTerminate();
}

int main(void) {
    VulkanApp app = {0};
    SolarSystem solar_system;
    Camera camera;
    float speed = 1.0f;
    bool paused = false;
    bool camera_locked = false;

    solar_system_init(&solar_system);
    camera_init(&camera);
    init_window(&app);
    g_scroll_camera = &camera;
    glfwSetScrollCallback(app.window, scroll_callback);
    init_vulkan(&app, &solar_system);
    upload_scene_geometry(&app, &solar_system);
    free_scene_cpu_geometry(&solar_system);

    double last_time = glfwGetTime();
    double fps_timer = last_time;
    int fps_frames = 0;
    float fps = 0.0f;
    bool prev_space = false;
    bool prev_plus = false;
    bool prev_minus = false;
    bool prev_l = false;
    bool prev_digits[10] = {false};
    int target_index = 0;

    while (!glfwWindowShouldClose(app.window)) {
        double now = glfwGetTime();
        float dt = (float) (now - last_time);
        last_time = now;
        glfwPollEvents();

        if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        bool space_pressed = glfwGetKey(app.window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool plus_pressed = glfwGetKey(app.window, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(app.window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
        bool minus_pressed = glfwGetKey(app.window, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(app.window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;
        bool l_pressed = glfwGetKey(app.window, GLFW_KEY_L) == GLFW_PRESS;
        if (space_pressed && !prev_space) {
            paused = !paused;
        }
        if (l_pressed && !prev_l) {
            camera_locked = !camera_locked;
            camera.dragging = false;
            camera.zoom_delta = 0.0f;
        }
        if (plus_pressed && !prev_plus) {
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
            bool pressed = glfwGetKey(app.window, key) == GLFW_PRESS;
            if (pressed && !prev_digits[i] && (i == 0 || i <= (int) ARRAY_LEN(solar_system.planets))) {
                target_index = i;
            }
            prev_digits[i] = pressed;
        }
        prev_space = space_pressed;
        prev_plus = plus_pressed;
        prev_minus = minus_pressed;
        prev_l = l_pressed;

        float move_forward = 0.0f;
        float move_right = 0.0f;
        if (glfwGetKey(app.window, GLFW_KEY_W) == GLFW_PRESS) {
            move_forward += 1.0f;
        }
        if (glfwGetKey(app.window, GLFW_KEY_S) == GLFW_PRESS) {
            move_forward -= 1.0f;
        }
        if (glfwGetKey(app.window, GLFW_KEY_D) == GLFW_PRESS) {
            move_right += 1.0f;
        }
        if (glfwGetKey(app.window, GLFW_KEY_A) == GLFW_PRESS) {
            move_right -= 1.0f;
        }

        camera_handle_mouse(&camera, app.window);
        if (move_forward != 0.0f || move_right != 0.0f) {
            camera_move(&camera, move_forward, move_right, dt);
        }

        if (!paused) {
            solar_system_update(&solar_system, dt, speed);
        }
        if (camera_locked) {
            camera_look_at_target(&camera, target_position(&solar_system, target_index));
        }

        draw_frame(&app, &solar_system, &camera);

        ++fps_frames;
        if (now - fps_timer >= 0.25) {
            fps = (float) fps_frames / (float) (now - fps_timer);
            fps_frames = 0;
            fps_timer = now;
            update_window_title(app.window, fps, speed, paused, camera_locked, target_name(&solar_system, target_index));
        }
    }

    cleanup(&app, &solar_system);
    return 0;
}
