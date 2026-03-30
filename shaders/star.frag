#version 330 core
in float v_brightness;
out vec4 frag_color;

void main() {
    frag_color = vec4(v_brightness, v_brightness, v_brightness, 1.0);
}
