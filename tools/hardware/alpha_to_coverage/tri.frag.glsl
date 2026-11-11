#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    float alpha = uv.x < 0.25 ? 0.0 : (uv.x > 0.75 ? 1.0 : 0.5);
    color = vec4(0.0, 0.4, 0.8, alpha);
}
