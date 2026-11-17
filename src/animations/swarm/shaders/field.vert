#version 300 es
precision highp float;
layout(location = 0) in vec2 quad;
layout(location = 1) in vec4 body;
layout(location = 2) in float cornerRadius;
uniform vec2 fieldSize;
uniform float k;
flat out vec4 box;
flat out float corner;
void main() {
    box = body;
    corner = cornerRadius;
    vec2 p = body.xy + quad * (body.zw + vec2(8.0 * k));
    gl_Position = vec4(p / fieldSize * 2.0 - 1.0, 0.0, 1.0);
}
