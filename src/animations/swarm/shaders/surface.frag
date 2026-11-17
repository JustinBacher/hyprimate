#version 300 es
precision highp float;
precision highp int;

uniform sampler2D tex;
uniform sampler2D field;
uniform ivec2 fieldSize;
uniform int standingAt;
uniform float k;
uniform float canvasRadius;
out vec4 colour;

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - ivec2(standingAt, 0);
    // Evaluate every lane before branching: the split can fall inside a derivative quad.
    float value = texelFetch(field, clamp(local, ivec2(0), fieldSize - 1), 0).r;
    // Saturate distance outside the support, not at log(0): a derivative spike there
    // would otherwise antialias the invisible support rectangle back into view.
    float d = -k * log(max(value, exp(-8.0)));
    vec2 halfSize = vec2(fieldSize) * 0.5;
    float r = min(canvasRadius, min(halfSize.x, halfSize.y));
    float boundary = roundedBox(vec2(local) + 0.5 - halfSize, halfSize, r);
    d = max(d, boundary);
    float width = max(fwidth(d), 1.0);
    float a = 1.0 - smoothstep(-0.5 * width, 0.5 * width, d);
    colour = p.x < standingAt ? texelFetch(tex, p, 0) : vec4(a);
}
