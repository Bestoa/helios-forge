#version 330 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_view;
uniform mat4 u_projection;
uniform vec3 u_color;

out vec3 v_color;

void main() {
    gl_Position = u_projection * u_view * vec4(a_position, 1.0);
    v_color = u_color;
}
