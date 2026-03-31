"""3D Solar System Simulation using Modern OpenGL 4.6 + Pygame."""

import argparse
import sys
import math
import random
import ctypes
from pathlib import Path
import numpy as np

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GL import shaders
from OpenGL.GL.shaders import compileProgram, compileShader
from OpenGL.raw.GL.VERSION.GL_2_0 import glVertexAttribPointer as raw_glVertexAttribPointer

# ============================================================================
# Matrix utilities
# ============================================================================
def mat4_identity():
    return np.identity(4, dtype=np.float32)

def mat4_perspective(fov_deg, aspect, near, far):
    f = 1.0 / math.tan(math.radians(fov_deg) / 2.0)
    m = mat4_identity()
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = (2.0 * far * near) / (near - far)
    m[3, 2] = -1.0
    m[3, 3] = 0.0
    return m

def mat4_look_at(eye, center, up):
    f = center - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    s = s / np.linalg.norm(s)
    u = np.cross(s, f)

    m = mat4_identity()
    m[0, 0] = s[0]; m[0, 1] = s[1]; m[0, 2] = s[2]
    m[1, 0] = u[0]; m[1, 1] = u[1]; m[1, 2] = u[2]
    m[2, 0] = -f[0]; m[2, 1] = -f[1]; m[2, 2] = -f[2]
    m[0, 3] = -np.dot(s, eye)
    m[1, 3] = -np.dot(u, eye)
    m[2, 3] = np.dot(f, eye)
    return m

def mat4_translate(v):
    m = mat4_identity()
    m[0, 3] = v[0]
    m[1, 3] = v[1]
    m[2, 3] = v[2]
    return m

def mat4_rotate_x(deg):
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    m = mat4_identity()
    m[1, 1] = c; m[1, 2] = -s
    m[2, 1] = s; m[2, 2] = c
    return m

def mat4_rotate_y(deg):
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    m = mat4_identity()
    m[0, 0] = c; m[0, 2] = s
    m[2, 0] = -s; m[2, 2] = c
    return m

def mat4_rotate_z(deg):
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    m = mat4_identity()
    m[0, 0] = c; m[0, 1] = -s
    m[1, 0] = s; m[1, 1] = c
    return m

def mat4_scale(sx, sy, sz):
    m = mat4_identity()
    m[0, 0] = sx
    m[1, 1] = sy
    m[2, 2] = sz
    return m

def mat4_mul(a, b):
    return np.matmul(a, b)

def mat4_to_mat3(mat4):
    return np.linalg.inv(mat4[:3, :3]).T.astype(np.float32)

def mat4_inverse(m):
    return np.linalg.inv(m)

# ============================================================================
# Shader Program
# ============================================================================
SHADER_DIR = Path(__file__).resolve().parent / "shaders"


class Shader:
    def __init__(self, vs_source, fs_source):
        try:
            vs = compileShader(vs_source, GL_VERTEX_SHADER)
            fs = compileShader(fs_source, GL_FRAGMENT_SHADER)
            self.program = compileProgram(vs, fs)
        except Exception as e:
            print(f"Shader compilation failed: {e}")
            raise

        self.uniforms = {}
        self._cache_uniforms()

    @classmethod
    def from_files(cls, vertex_name, fragment_name):
        vs_source = (SHADER_DIR / vertex_name).read_text(encoding="utf-8")
        fs_source = (SHADER_DIR / fragment_name).read_text(encoding="utf-8")
        return cls(vs_source, fs_source)

    def _cache_uniforms(self):
        count = glGetProgramiv(self.program, GL_ACTIVE_UNIFORMS)
        for i in range(count):
            name, size, type = glGetActiveUniform(self.program, i)
            loc = glGetUniformLocation(self.program, name)
            self.uniforms[name.decode()] = loc

    def use(self):
        glUseProgram(self.program)

    def set_float(self, name, value):
        if name in self.uniforms:
            glUniform1f(self.uniforms[name], value)

    def set_int(self, name, value):
        if name in self.uniforms:
            glUniform1i(self.uniforms[name], value)

    def set_vec3(self, name, value):
        if name in self.uniforms:
            glUniform3f(self.uniforms[name], value[0], value[1], value[2])

    def set_mat4(self, name, value):
        if name in self.uniforms:
            loc = self.uniforms[name]
            if loc >= 0:
                glUniformMatrix4fv(loc, 1, GL_TRUE, np.ascontiguousarray(value, dtype=np.float32))

    def set_mat3(self, name, value):
        if name in self.uniforms:
            loc = self.uniforms[name]
            if loc >= 0:
                glUniformMatrix3fv(loc, 1, GL_TRUE, np.ascontiguousarray(value, dtype=np.float32))

    def set_texture(self, name, unit):
        if name in self.uniforms:
            glUniform1i(self.uniforms[name], unit)

# ============================================================================
# Geometry generation
# ============================================================================
def create_sphere_mesh(radius, slices, stacks):
    positions = []
    normals = []
    texcoords = []
    indices = []

    for i in range(stacks + 1):
        phi = math.pi * i / stacks
        sin_phi = math.sin(phi)
        cos_phi = math.cos(phi)

        for j in range(slices + 1):
            theta = 2.0 * math.pi * j / slices
            sin_theta = math.sin(theta)
            cos_theta = math.cos(theta)

            x = cos_theta * sin_phi
            y = cos_phi
            z = sin_theta * sin_phi

            positions.append([radius * x, radius * y, radius * z])
            normals.append([x, y, z])
            texcoords.append([1.0 - float(j) / slices, 1.0 - float(i) / stacks])

    for i in range(stacks):
        for j in range(slices):
            p1 = i * (slices + 1) + j
            p2 = p1 + slices + 1
            if i > 0:
                indices.extend([p1, p1 + 1, p2])
            if i < stacks - 1:
                indices.extend([p1 + 1, p2 + 1, p2])

    return (
        np.array(positions, dtype=np.float32),
        np.array(normals, dtype=np.float32),
        np.array(texcoords, dtype=np.float32),
        np.array(indices, dtype=np.uint32),
    )

def create_ring_mesh(inner_radius, outer_radius, segments):
    positions = []
    normals = []
    texcoords = []
    indices = []

    # Two sides
    for side in [-1, 1]:
        for i in range(segments + 1):
            theta = 2.0 * math.pi * i / segments
            cos_t = math.cos(theta)
            sin_t = math.sin(theta)

            # Inner edge
            positions.append([inner_radius * cos_t, 0, inner_radius * sin_t])
            normals.append([0, side, 0])
            texcoords.append([0.0, float(i) / segments])

            # Outer edge
            positions.append([outer_radius * cos_t, 0, outer_radius * sin_t])
            normals.append([0, side, 0])
            texcoords.append([1.0, float(i) / segments])

        base_idx = 0 if side == -1 else 2 * (segments + 1)
        for i in range(segments):
            p1 = base_idx + 2 * i
            p2 = base_idx + 2 * i + 1
            p3 = base_idx + 2 * i + 2
            p4 = base_idx + 2 * i + 3
            if side == 1:
                indices.extend([p1, p2, p3, p2, p4, p3])
            else:
                indices.extend([p2, p1, p4, p1, p3, p4])

    return np.array(positions, dtype=np.float32), np.array(normals, dtype=np.float32), np.array(texcoords, dtype=np.float32), np.array(indices, dtype=np.uint32)

def deg_to_rad(degrees):
    return math.radians(degrees)


def solve_eccentric_anomaly(mean_anomaly, eccentricity):
    eccentric_anomaly = mean_anomaly if eccentricity < 0.8 else math.pi
    for _ in range(6):
        sin_e = math.sin(eccentric_anomaly)
        cos_e = math.cos(eccentric_anomaly)
        f = eccentric_anomaly - eccentricity * sin_e - mean_anomaly
        fp = 1.0 - eccentricity * cos_e
        if abs(fp) < 1e-4:
            fp = -1e-4 if fp < 0.0 else 1e-4
        eccentric_anomaly -= f / fp
    return eccentric_anomaly


def build_orbit_frame(ascending_node_deg, inclination_deg, periapsis_deg):
    node = deg_to_rad(ascending_node_deg)
    inclination = deg_to_rad(inclination_deg)
    periapsis = deg_to_rad(periapsis_deg)

    cos_node = math.cos(node)
    sin_node = math.sin(node)
    cos_inclination = math.cos(inclination)
    sin_inclination = math.sin(inclination)
    cos_periapsis = math.cos(periapsis)
    sin_periapsis = math.sin(periapsis)

    periapsis_dir = np.array([
        cos_node * cos_periapsis - sin_node * sin_periapsis * cos_inclination,
        sin_periapsis * sin_inclination,
        sin_node * cos_periapsis + cos_node * sin_periapsis * cos_inclination,
    ], dtype=np.float32)
    minor_dir = np.array([
        -cos_node * sin_periapsis - sin_node * cos_periapsis * cos_inclination,
        cos_periapsis * sin_inclination,
        -sin_node * sin_periapsis + cos_node * cos_periapsis * cos_inclination,
    ], dtype=np.float32)
    return periapsis_dir, minor_dir


def orbit_to_world(orbit_frame, radius, true_anomaly):
    periapsis_dir, minor_dir = orbit_frame
    return (
        periapsis_dir * (radius * math.cos(true_anomaly)) +
        minor_dir * (radius * math.sin(true_anomaly))
    ).astype(np.float32)


def create_orbit_path(planet, segments=192):
    positions = []
    semi_latus_rectum = planet.orbit_radius * (1.0 - planet.orbit_eccentricity * planet.orbit_eccentricity)
    for i in range(segments + 1):
        true_anomaly = 2.0 * math.pi * i / segments
        radius = semi_latus_rectum / (1.0 + planet.orbit_eccentricity * math.cos(true_anomaly))
        positions.append(orbit_to_world(planet.orbit_frame, radius, true_anomaly))
    return np.array(positions, dtype=np.float32)

def generate_stars(count, radius=220.0):
    positions = []
    brightness = []
    random.seed(7)
    for _ in range(count):
        theta = random.uniform(0, 2 * math.pi)
        phi = random.uniform(-math.pi / 2, math.pi / 2)
        x = radius * math.cos(phi) * math.cos(theta)
        y = radius * math.sin(phi)
        z = radius * math.cos(phi) * math.sin(theta)
        positions.append([x, y, z])
        b = random.uniform(0.4, 1.0)
        brightness.append(b)
    return np.array(positions, dtype=np.float32), np.array(brightness, dtype=np.float32)


def load_texture(path, wrap_repeat=True, srgb=True):
    surface = pygame.image.load(str(path)).convert_alpha()
    texture_data = pygame.image.tostring(surface, "RGBA", True)
    width, height = surface.get_size()

    texture_id = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, texture_id)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT if wrap_repeat else GL_CLAMP_TO_EDGE)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT if wrap_repeat else GL_CLAMP_TO_EDGE)
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_SRGB8_ALPHA8 if srgb else GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        texture_data,
    )
    glGenerateMipmap(GL_TEXTURE_2D)
    glBindTexture(GL_TEXTURE_2D, 0)
    return texture_id


def save_framebuffer(path, width, height):
    pixels = glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE)
    surface = pygame.image.fromstring(pixels, (width, height), "RGBA", True)
    pygame.image.save(surface, str(path))

# ============================================================================
# VAO/VBO wrapper
# ============================================================================
class Mesh:
    def __init__(self):
        self.vao = glGenVertexArrays(1)
        self.vbos = []
        self.ebo = None
        self.index_count = 0
        self.vertex_count = 0
        self.primitive = GL_TRIANGLES

    def bind(self):
        glBindVertexArray(self.vao)

    def unbind(self):
        glBindVertexArray(0)

    def set_vertex_buffer(self, index, data, size=3, stride=0, offset=0):
        glBindVertexArray(self.vao)
        vbo = glGenBuffers(1)
        self.vbos.append(vbo)
        glBindBuffer(GL_ARRAY_BUFFER, vbo)
        glBufferData(GL_ARRAY_BUFFER, data.nbytes, data, GL_STATIC_DRAW)
        raw_glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride, ctypes.c_void_p(offset))
        glEnableVertexAttribArray(index)
        glBindVertexArray(0)

    def set_index_buffer(self, data):
        glBindVertexArray(self.vao)
        self.ebo = glGenBuffers(1)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.ebo)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.nbytes, data, GL_STATIC_DRAW)
        self.index_count = len(data)
        glBindVertexArray(0)

    def draw(self):
        glBindVertexArray(self.vao)
        if self.index_count > 0:
            glDrawElements(self.primitive, self.index_count, GL_UNSIGNED_INT, None)
        else:
            glDrawArrays(self.primitive, 0, self.vertex_count)
        glBindVertexArray(0)

    def draw_instanced(self, count):
        glBindVertexArray(self.vao)
        if self.index_count > 0:
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self.ebo)
            glDrawElementsInstanced(self.primitive, self.index_count, GL_UNSIGNED_INT, None, count)
        else:
            glDrawArraysInstanced(self.primitive, 0, self.vertex_count, count)
        glBindVertexArray(0)

# ============================================================================
# Camera
# ============================================================================
class Camera:
    def __init__(self):
        self.move_speed = 24.0
        self.mouse_sensitivity = 0.2
        self.pitch = -20.0
        self.yaw = -90.0
        self.dragging = False
        self.last_pos = (0, 0)
        self.view_matrix = mat4_identity()
        self.eye = np.array([0.0, 12.0, 60.0], dtype=np.float32)
        self.forward = np.array([0.0, 0.0, -1.0], dtype=np.float32)
        self.right = np.array([1.0, 0.0, 0.0], dtype=np.float32)
        self.up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
        self._update_view()

    def _set_forward(self, forward):
        world_up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
        forward_norm = np.linalg.norm(forward)
        if forward_norm <= 1e-6:
            return

        self.forward = (forward / forward_norm).astype(np.float32)
        self.yaw = math.degrees(math.atan2(self.forward[2], self.forward[0]))
        self.pitch = math.degrees(math.asin(max(-1.0, min(1.0, float(self.forward[1])))))

        right = np.cross(self.forward, world_up)
        right_norm = np.linalg.norm(right)
        if right_norm > 1e-6:
            self.right = (right / right_norm).astype(np.float32)
        else:
            self.right = np.array([1.0, 0.0, 0.0], dtype=np.float32)

        up = np.cross(self.right, self.forward)
        up_norm = np.linalg.norm(up)
        if up_norm > 1e-6:
            self.up = (up / up_norm).astype(np.float32)
        else:
            self.up = world_up

        self.view_matrix = mat4_look_at(self.eye, self.eye + self.forward, self.up)

    def _update_view(self):
        pitch_rad = math.radians(self.pitch)
        yaw_rad = math.radians(self.yaw)
        forward = np.array([
            math.cos(pitch_rad) * math.cos(yaw_rad),
            math.sin(pitch_rad),
            math.cos(pitch_rad) * math.sin(yaw_rad),
        ], dtype=np.float32)
        self._set_forward(forward)

    def get_view_matrix(self):
        return self.view_matrix

    def look_at(self, target):
        self._set_forward(np.array(target, dtype=np.float32) - self.eye)

    def move(self, forward_amount, right_amount, dt):
        movement = self.forward * forward_amount + self.right * right_amount
        movement_norm = np.linalg.norm(movement)
        if movement_norm > 1e-6:
            movement = movement / movement_norm
            delta = movement * self.move_speed * dt
            self.eye = (self.eye + delta).astype(np.float32)
            self._update_view()

    def handle_event(self, event):
        if event.type == MOUSEBUTTONDOWN:
            if event.button == 1:
                self.dragging = True
                self.last_pos = event.pos
            elif event.button == 4:
                self.eye = (self.eye + self.forward * 2.0).astype(np.float32)
                self._update_view()
            elif event.button == 5:
                self.eye = (self.eye - self.forward * 2.0).astype(np.float32)
                self._update_view()
        elif event.type == MOUSEBUTTONUP:
            if event.button == 1:
                self.dragging = False
        elif event.type == MOUSEMOTION and self.dragging:
            dx = event.pos[0] - self.last_pos[0]
            dy = event.pos[1] - self.last_pos[1]
            self.yaw += dx * self.mouse_sensitivity
            self.pitch -= dy * self.mouse_sensitivity
            self.pitch = max(-89.0, min(89.0, self.pitch))
            self.last_pos = event.pos
            self._update_view()

# ============================================================================
# Planet
# ============================================================================
class Planet:
    ROTATION_TIME_SCALE_DAYS_PER_SECOND = 8.0
    ORBIT_DAY_SCALE = 60.0

    def __init__(self, name, orbit_radius, orbit_eccentricity, orbit_inclination_deg,
                 orbit_ascending_node_deg, orbit_periapsis_deg, size, color, orbit_days,
                 orbit_phase_deg,
                 axial_tilt_deg=0.0, rotation_period_days=1.0,
                 has_ring=False, ring_color=None, ring_inner=0, ring_outer=0,
                 texture_name=None):
        self.name = name
        self.orbit_radius = orbit_radius
        self.orbit_eccentricity = orbit_eccentricity
        self.orbit_inclination_deg = orbit_inclination_deg
        self.orbit_ascending_node_deg = orbit_ascending_node_deg
        self.orbit_periapsis_deg = orbit_periapsis_deg
        self.size = size
        self.color = color
        self.orbit_days = orbit_days
        self.orbit_mean_anomaly_deg = orbit_phase_deg
        self.axial_tilt_deg = axial_tilt_deg
        self.rotation_period_days = rotation_period_days
        self.rotation_angle = random.uniform(0, 360)
        self.has_ring = has_ring
        self.ring_color = ring_color or (1, 1, 1)
        self.ring_inner = ring_inner
        self.ring_outer = ring_outer
        self.texture_name = texture_name or f"{name.lower()}.jpg"
        self.orbit_frame = build_orbit_frame(
            orbit_ascending_node_deg,
            orbit_inclination_deg,
            orbit_periapsis_deg,
        )

    def update(self, dt, speed):
        if self.orbit_days > 0:
            angular_speed = 360.0 / self.orbit_days
            self.orbit_mean_anomaly_deg = (
                self.orbit_mean_anomaly_deg + angular_speed * dt * speed * self.ORBIT_DAY_SCALE
            ) % 360.0

        if abs(self.rotation_period_days) > 1e-6:
            rotation_speed = 360.0 / (abs(self.rotation_period_days) * self.ROTATION_TIME_SCALE_DAYS_PER_SECOND)
            if self.rotation_period_days < 0:
                rotation_speed = -rotation_speed
            self.rotation_angle = (self.rotation_angle + rotation_speed * dt * speed) % 360.0

    def get_position(self):
        mean_anomaly = deg_to_rad(self.orbit_mean_anomaly_deg)
        eccentric_anomaly = solve_eccentric_anomaly(mean_anomaly, self.orbit_eccentricity)
        cos_e = math.cos(eccentric_anomaly)
        sin_e = math.sin(eccentric_anomaly)
        radius = self.orbit_radius * (1.0 - self.orbit_eccentricity * cos_e)
        true_anomaly = math.atan2(
            math.sqrt(1.0 - self.orbit_eccentricity * self.orbit_eccentricity) * sin_e,
            cos_e - self.orbit_eccentricity,
        )
        return orbit_to_world(self.orbit_frame, radius, true_anomaly)

    def get_model_matrix(self):
        pos = self.get_position()
        scale = mat4_scale(self.size, self.size, self.size)
        spin = mat4_rotate_y(self.rotation_angle)
        tilt = mat4_rotate_z(self.axial_tilt_deg)
        return mat4_mul(mat4_translate(pos), mat4_mul(tilt, mat4_mul(spin, scale)))

# ============================================================================
# Solar System
# ============================================================================
class SolarSystem:
    TEXTURE_DIR = Path(__file__).resolve().parent / "assets" / "textures"

    def __init__(self):
        random.seed(42)
        self.planets = [
            Planet("Mercury",  4.0, 0.2056, 7.00, 48.33, 29.12, 0.15, (0.7, 0.7, 0.7), 88.0, 10.0,
                   axial_tilt_deg=0.03, rotation_period_days=58.65),
            Planet("Venus",    5.8, 0.0068, 3.39, 76.68, 54.88, 0.30, (0.9, 0.7, 0.3), 225.0, 75.0,
                   axial_tilt_deg=177.36, rotation_period_days=-243.02),
            Planet("Earth",    8.0, 0.0167, 0.00, -11.26, 114.21, 0.32, (0.2, 0.5, 1.0), 365.0, 135.0,
                   axial_tilt_deg=23.44, rotation_period_days=0.997),
            Planet("Mars",    10.5, 0.0934, 1.85, 49.58, 286.50, 0.20, (0.9, 0.3, 0.2), 687.0, 195.0,
                   axial_tilt_deg=25.19, rotation_period_days=1.03),
            Planet("Jupiter", 14.5, 0.0489, 1.30, 100.46, 273.87, 0.90, (0.8, 0.6, 0.4), 4333.0, 250.0,
                   axial_tilt_deg=3.13, rotation_period_days=0.41),
            Planet("Saturn",  19.0, 0.0565, 2.49, 113.67, 339.39, 0.80, (0.9, 0.8, 0.5), 10759.0, 305.0,
                   axial_tilt_deg=26.73, rotation_period_days=0.45,
                   has_ring=True, ring_color=(0.85, 0.75, 0.55),
                   ring_inner=1.0, ring_outer=1.6),
            Planet("Uranus",  23.0, 0.0472, 0.77, 74.01, 96.73, 0.50, (0.5, 0.8, 0.9), 30687.0, 15.0,
                   axial_tilt_deg=97.77, rotation_period_days=-0.72),
            Planet("Neptune", 27.5, 0.0086, 1.77, 131.78, 273.19, 0.50, (0.2, 0.3, 0.9), 60190.0, 100.0,
                   axial_tilt_deg=28.32, rotation_period_days=0.67),
        ]

        # Sun data
        self.sun_radius = 1.5
        self.sun_color = (1.0, 0.85, 0.2)
        self.sun_rotation_period_days = 24.47
        self.sun_rotation_angle = random.uniform(0, 360)
        self.sun_texture = None
        self.ring_alpha_texture = None

        # Generate meshes
        self._init_meshes()
        self._load_textures()

    def _init_meshes(self):
        # Sphere mesh for planets
        pos, norm, tex, idx = create_sphere_mesh(1.0, 40, 28)
        self.sphere_mesh = Mesh()
        self.sphere_mesh.primitive = GL_TRIANGLES
        self.sphere_mesh.set_vertex_buffer(0, pos, 3)
        self.sphere_mesh.set_vertex_buffer(1, norm, 3)
        self.sphere_mesh.set_vertex_buffer(2, tex, 2)
        self.sphere_mesh.set_index_buffer(idx)

        # Sun sphere (higher quality)
        pos, norm, tex, idx = create_sphere_mesh(1.0, 40, 28)
        self.sun_mesh = Mesh()
        self.sun_mesh.primitive = GL_TRIANGLES
        self.sun_mesh.set_vertex_buffer(0, pos, 3)
        self.sun_mesh.set_vertex_buffer(1, norm, 3)
        self.sun_mesh.set_vertex_buffer(2, tex, 2)
        self.sun_mesh.set_index_buffer(idx)

        # Ring mesh
        pos, norm, tex, idx = create_ring_mesh(1.0, 1.6, 96)
        self.ring_mesh = Mesh()
        self.ring_mesh.primitive = GL_TRIANGLES
        self.ring_mesh.set_vertex_buffer(0, pos, 3)
        self.ring_mesh.set_vertex_buffer(1, norm, 3)
        self.ring_mesh.set_vertex_buffer(2, tex, 2)
        self.ring_mesh.set_index_buffer(idx)

        # Stars
        self.star_positions, self.star_brightness = generate_stars(1800)
        self.star_mesh = Mesh()
        self.star_mesh.primitive = GL_POINTS
        self.star_mesh.vertex_count = len(self.star_positions)
        self.star_mesh.set_vertex_buffer(0, self.star_positions, 3)
        self.star_mesh.set_vertex_buffer(1, self.star_brightness, 1)

        # Orbit lines
        self.orbit_meshes = []
        for p in self.planets:
            orbit_pos = create_orbit_path(p, 192)
            mesh = Mesh()
            mesh.primitive = GL_LINE_STRIP
            mesh.vertex_count = len(orbit_pos)
            mesh.set_vertex_buffer(0, orbit_pos)
            self.orbit_meshes.append(mesh)

    def _load_textures(self):
        for p in self.planets:
            texture_path = self.TEXTURE_DIR / p.texture_name
            p.texture = load_texture(texture_path, srgb=True) if texture_path.exists() else None

        sun_path = self.TEXTURE_DIR / "sun.jpg"
        if sun_path.exists():
            self.sun_texture = load_texture(sun_path, srgb=True)

        ring_alpha_path = self.TEXTURE_DIR / "saturn_ring_alpha.png"
        if ring_alpha_path.exists():
            self.ring_alpha_texture = load_texture(ring_alpha_path, wrap_repeat=False, srgb=False)

    def update(self, dt, speed):
        for p in self.planets:
            p.update(dt, speed)
        sun_rotation_speed = 360.0 / (self.sun_rotation_period_days * Planet.ROTATION_TIME_SCALE_DAYS_PER_SECOND)
        self.sun_rotation_angle = (self.sun_rotation_angle + sun_rotation_speed * dt * speed) % 360.0

    def get_target_position(self, target_index):
        if target_index == 0:
            return np.array([0.0, 0.0, 0.0], dtype=np.float32)
        if 1 <= target_index <= len(self.planets):
            return self.planets[target_index - 1].get_position()
        return None

    def get_target_name(self, target_index):
        if target_index == 0:
            return "Sun"
        if 1 <= target_index <= len(self.planets):
            return self.planets[target_index - 1].name
        return "None"

# ============================================================================
# HUD
# ============================================================================
class HUD:
    def __init__(self, width, height):
        self.w = width
        self.h = height
        pygame.font.init()
        self.font = pygame.font.SysFont("monospace", 18, bold=True)
        self.texture = glGenTextures(1)

        positions = np.array([
            -1.0, 1.0,
            -1.0, 0.8,
            -0.2, 1.0,
            -0.2, 0.8,
        ], dtype=np.float32)
        texcoords = np.array([
            0.0, 1.0,
            0.0, 0.0,
            1.0, 1.0,
            1.0, 0.0,
        ], dtype=np.float32)

        self.quad_mesh = Mesh()
        self.quad_mesh.primitive = GL_TRIANGLE_STRIP
        self.quad_mesh.vertex_count = 4
        self.quad_mesh.set_vertex_buffer(0, positions, 2)
        self.quad_mesh.set_vertex_buffer(1, texcoords, 2)

        glBindTexture(GL_TEXTURE_2D, self.texture)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
        glBindTexture(GL_TEXTURE_2D, 0)

    def _upload_surface(self, surface):
        data = pygame.image.tostring(surface, "RGBA", True)
        width, height = surface.get_size()
        glBindTexture(GL_TEXTURE_2D, self.texture)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1)
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data
        )
        glBindTexture(GL_TEXTURE_2D, 0)
        return width, height

    def _update_quad(self, width_px, height_px):
        left = -1.0
        right = -1.0 + 2.0 * (width_px / self.w)
        top = 1.0
        bottom = 1.0 - 2.0 * (height_px / self.h)
        positions = np.array([
            left, top,
            left, bottom,
            right, top,
            right, bottom,
        ], dtype=np.float32)
        glBindBuffer(GL_ARRAY_BUFFER, self.quad_mesh.vbos[0])
        glBufferData(GL_ARRAY_BUFFER, positions.nbytes, positions, GL_DYNAMIC_DRAW)

    def render(self, shader, speed, paused, fps, camera_locked, target_name):
        lines = [
            f"FPS: {fps:.1f}  Speed: {speed:.1f}x" + ("  [PAUSED]" if paused else ""),
            f"Camera: {'LOCKED' if camera_locked else 'FREE'}  Target: {target_name}",
            "Mouse: rotate  Scroll: zoom  WASD: move  L: lock target  0: Sun  1-8: planets  +/-: speed  Space: pause  Esc: quit",
        ]
        line_h = self.font.get_linesize()
        text_w = max(self.font.size(line)[0] for line in lines)
        text_h = line_h * len(lines)
        surface = pygame.Surface((text_w + 24, text_h + 16), pygame.SRCALPHA)
        surface.fill((8, 10, 18, 180))

        for i, line in enumerate(lines):
            text_surf = self.font.render(line, True, (235, 235, 235))
            surface.blit(text_surf, (12, 8 + i * line_h))

        surf_w, surf_h = self._upload_surface(surface)
        self._update_quad(surf_w, surf_h)

        glDisable(GL_DEPTH_TEST)
        glDisable(GL_CULL_FACE)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

        shader.use()
        shader.set_texture("u_texture", 0)
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, self.texture)
        self.quad_mesh.draw()
        glBindTexture(GL_TEXTURE_2D, 0)

        glDisable(GL_BLEND)
        glEnable(GL_CULL_FACE)
        glEnable(GL_DEPTH_TEST)

# ============================================================================
# Window / context creation
# ============================================================================
def create_opengl_window(width, height):
    attempts = [
        ("OpenGL 3.3 Core", 3, 3, pygame.GL_CONTEXT_PROFILE_CORE),
        ("OpenGL 3.0", 3, 0, 0),
        ("OpenGL Default", None, None, None),
    ]

    last_error = None
    for caption, major, minor, profile in attempts:
        for vsync in (1, 0):
            try:
                pygame.display.gl_set_attribute(pygame.GL_DOUBLEBUFFER, 1)
                pygame.display.gl_set_attribute(pygame.GL_MULTISAMPLEBUFFERS, 1)
                pygame.display.gl_set_attribute(pygame.GL_MULTISAMPLESAMPLES, 4)
                if major is not None:
                    pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MAJOR_VERSION, major)
                    pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MINOR_VERSION, minor)
                if profile is not None:
                    pygame.display.gl_set_attribute(pygame.GL_CONTEXT_PROFILE_MASK, profile)

                pygame.display.set_mode((width, height), DOUBLEBUF | OPENGL, vsync=vsync)
                vsync_label = "VSync On" if vsync else "VSync Off"
                pygame.display.set_caption(f"Solar System 3D Simulation - {caption} - {vsync_label}")
                return vsync == 1
            except pygame.error as exc:
                last_error = exc

    raise pygame.error(f"Could not create an OpenGL context: {last_error}")

# ============================================================================
# Main
# ============================================================================
def parse_args():
    parser = argparse.ArgumentParser(description="Render the solar system with optional frame capture.")
    parser.add_argument("--frame-output", type=Path, help="Write the final rendered frame to a PNG file.")
    parser.add_argument("--max-frames", type=int, help="Render this many frames and then exit.")
    return parser.parse_args()


def main():
    args = parse_args()

    # Initialize only the subsystems we need to avoid unrelated audio errors.
    pygame.display.init()

    width, height = 1200, 800
    vsync_enabled = create_opengl_window(width, height)

    # Print OpenGL version
    print(f"OpenGL Version: {glGetString(GL_VERSION).decode()}")
    print(f"GLSL Version: {glGetString(GL_SHADING_LANGUAGE_VERSION).decode()}")
    print(f"VSync: {'enabled' if vsync_enabled else 'unavailable'}")

    # Setup OpenGL state
    glClearColor(0.0, 0.0, 0.01, 1.0)
    glEnable(GL_DEPTH_TEST)
    glEnable(GL_MULTISAMPLE)
    glEnable(GL_CULL_FACE)
    glCullFace(GL_BACK)
    glFrontFace(GL_CCW)
    glEnable(GL_PROGRAM_POINT_SIZE)
    glEnable(GL_FRAMEBUFFER_SRGB)

    # Create shaders
    planet_shader = Shader.from_files("planet.vert", "planet.frag")
    ring_shader = Shader.from_files("ring.vert", "ring.frag")
    star_shader = Shader.from_files("star.vert", "star.frag")
    orbit_shader = Shader.from_files("orbit.vert", "orbit.frag")
    hud_shader = Shader.from_files("hud.vert", "hud.frag")

    # Create systems
    camera = Camera()
    solar_system = SolarSystem()
    hud = HUD(width, height)

    # Light settings
    light_pos = np.array([0.0, 0.0, 0.0], dtype=np.float32)
    light_ambient = np.array([0.02, 0.02, 0.02], dtype=np.float32)
    light_diffuse = np.array([1.34, 1.26, 1.16], dtype=np.float32)
    light_specular = np.array([0.32, 0.32, 0.32], dtype=np.float32)

    planet_shader.use()
    planet_shader.set_texture("u_texture", 0)
    ring_shader.use()
    ring_shader.set_texture("u_alpha_texture", 1)

    # Projection matrix
    projection = mat4_perspective(45, width / height, 0.5, 500.0)

    clock = pygame.time.Clock()
    speed = 1.0
    paused = False
    camera_locked = False
    target_index = 0
    frame_count = 0

    while True:
        dt = clock.tick(60) / 1000.0

        for event in pygame.event.get():
            if event.type == QUIT:
                pygame.quit()
                sys.exit()
            elif event.type == KEYDOWN:
                if event.key == K_ESCAPE:
                    pygame.quit()
                    sys.exit()
                elif event.key == K_SPACE:
                    paused = not paused
                elif event.key == K_l:
                    camera_locked = not camera_locked
                    camera.dragging = False
                elif event.key == K_0:
                    target_index = 0
                elif K_1 <= event.key <= K_8:
                    target_index = event.key - K_0
                elif event.key in (K_PLUS, K_EQUALS, K_KP_PLUS):
                    speed = min(50.0, speed + 0.5)
                elif event.key in (K_MINUS, K_KP_MINUS):
                    speed = max(0.1, speed - 0.5)
            camera.handle_event(event)

        keys = pygame.key.get_pressed()
        move_forward = float(keys[K_w]) - float(keys[K_s])
        move_right = float(keys[K_d]) - float(keys[K_a])
        if move_forward != 0.0 or move_right != 0.0:
            camera.move(move_forward, move_right, dt)

        if not paused:
            solar_system.update(dt, speed)
        if camera_locked:
            target_pos = solar_system.get_target_position(target_index)
            if target_pos is not None:
                camera.look_at(target_pos)

        # Clear
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

        view = camera.get_view_matrix()

        # --- Draw stars (no lighting) ---
        glDisable(GL_CULL_FACE)
        star_shader.use()
        star_shader.set_mat4("u_view", view)
        star_shader.set_mat4("u_projection", projection)
        star_shader.set_float("u_size", 2.0)
        solar_system.star_mesh.draw()
        glEnable(GL_CULL_FACE)

        # --- Draw orbits ---
        glDisable(GL_DEPTH_TEST)
        orbit_shader.use()
        orbit_shader.set_mat4("u_view", view)
        orbit_shader.set_mat4("u_projection", projection)
        orbit_shader.set_vec3("u_color", [0.25, 0.25, 0.35])
        for mesh in solar_system.orbit_meshes:
            mesh.draw()
        glEnable(GL_DEPTH_TEST)

        # --- Draw sun (emissive) ---
        planet_shader.use()
        planet_shader.set_mat4("u_view", view)
        planet_shader.set_mat4("u_projection", projection)
        planet_shader.set_vec3("u_camera_pos", camera.eye)

        # Sun uses emission without lighting
        planet_shader.set_vec3("u_light_pos", light_pos)
        planet_shader.set_vec3("u_light_ambient", [0, 0, 0])
        planet_shader.set_vec3("u_light_diffuse", [0, 0, 0])
        planet_shader.set_vec3("u_light_specular", [0, 0, 0])

        sun_model = mat4_mul(
            mat4_rotate_y(solar_system.sun_rotation_angle),
            mat4_scale(solar_system.sun_radius, solar_system.sun_radius, solar_system.sun_radius)
        )
        planet_shader.set_mat4("u_model", sun_model)
        planet_shader.set_mat3("u_normal_matrix", mat4_to_mat3(sun_model))
        planet_shader.set_vec3("u_material_ambient", [0, 0, 0])
        planet_shader.set_vec3("u_material_diffuse", solar_system.sun_color)
        planet_shader.set_vec3("u_material_specular", [0, 0, 0])
        planet_shader.set_float("u_material_shininess", 1.0)
        planet_shader.set_float("u_emission", 1.0)  # 1.0 = full material color
        planet_shader.set_float("u_texture_mix", 1.0 if solar_system.sun_texture else 0.0)
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, solar_system.sun_texture or 0)
        solar_system.sun_mesh.draw()

        # --- Draw planets ---
        planet_shader.use()
        planet_shader.set_mat4("u_view", view)
        planet_shader.set_mat4("u_projection", projection)
        planet_shader.set_vec3("u_camera_pos", camera.eye)
        planet_shader.set_vec3("u_light_pos", light_pos)
        planet_shader.set_vec3("u_light_ambient", light_ambient)
        planet_shader.set_vec3("u_light_diffuse", light_diffuse)
        planet_shader.set_vec3("u_light_specular", light_specular)
        planet_shader.set_float("u_emission", 0.0)
        for p in solar_system.planets:
            model = p.get_model_matrix()
            planet_shader.set_mat4("u_model", model)
            planet_shader.set_mat3("u_normal_matrix", mat4_to_mat3(model))
            planet_shader.set_vec3("u_material_ambient", p.color)
            planet_shader.set_vec3("u_material_diffuse", p.color)
            planet_shader.set_vec3("u_material_specular", [0.01, 0.01, 0.01])
            planet_shader.set_float("u_material_shininess", 10.0)
            planet_shader.set_float("u_texture_mix", 1.0 if p.texture else 0.0)
            glActiveTexture(GL_TEXTURE0)
            glBindTexture(GL_TEXTURE_2D, p.texture or 0)
            solar_system.sphere_mesh.draw()

            # Draw ring if exists
            if p.has_ring:
                glDisable(GL_CULL_FACE)
                glEnable(GL_BLEND)
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

                ring_shader.use()
                ring_shader.set_mat4("u_view", view)
                ring_shader.set_mat4("u_projection", projection)
                ring_shader.set_vec3("u_light_pos", light_pos)
                ring_shader.set_vec3("u_light_diffuse", light_diffuse)
                ring_shader.set_vec3("u_material_color", p.ring_color)
                ring_shader.set_float("u_alpha", 0.8)
                ring_shader.set_float("u_use_alpha_texture", 1.0 if solar_system.ring_alpha_texture else 0.0)
                glActiveTexture(GL_TEXTURE1)
                glBindTexture(GL_TEXTURE_2D, solar_system.ring_alpha_texture or 0)

                # The ring mesh is authored in the XZ plane already, so it is
                # centered on Saturn and lies on the planet's equatorial plane.
                # Match Saturn's ring plane to the planet's tilted spin axis.
                ring_orientation = mat4_rotate_z(p.axial_tilt_deg)

                ring_model = mat4_mul(
                    mat4_translate(p.get_position()),
                    ring_orientation
                )
                ring_shader.set_mat4("u_model", ring_model)
                ring_shader.set_mat3("u_normal_matrix", mat4_to_mat3(ring_model))
                solar_system.ring_mesh.draw()

                glActiveTexture(GL_TEXTURE1)
                glBindTexture(GL_TEXTURE_2D, 0)
                glDisable(GL_BLEND)
                glEnable(GL_CULL_FACE)

                # Switch back to planet shader
                planet_shader.use()

        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, 0)

        # --- Draw HUD ---
        hud_shader.use()
        hud.render(
            hud_shader,
            speed,
            paused,
            clock.get_fps(),
            camera_locked,
            solar_system.get_target_name(target_index),
        )

        pygame.display.flip()
        frame_count += 1

        should_exit = args.max_frames is not None and frame_count >= args.max_frames
        if should_exit:
            if args.frame_output:
                save_framebuffer(args.frame_output, width, height)
            pygame.quit()
            return


if __name__ == "__main__":
    main()
