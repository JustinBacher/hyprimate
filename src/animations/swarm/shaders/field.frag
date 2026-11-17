#version 300 es
precision highp float;
flat in vec4 box;
flat in float corner;
uniform float k;
out vec4 colour;
void main() {
    vec2 q = abs(gl_FragCoord.xy - box.xy) - box.zw + corner;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - corner;
    float value = max(exp(min(-d / k, 4.0)) - exp(-8.0), 0.0);
    colour = vec4(value, 0.0, 0.0, 0.0);
}
