#version 300 es
precision highp float;
// One Jacobi iteration of the pressure Poisson equation, in cell units.
uniform sampler2D u_pressure, u_divergence;
uniform vec2 u_grid;
out vec4 frag;
void main() {
    vec2 texel = 1.0 / u_grid;
    vec2 uv = gl_FragCoord.xy * texel;
    float r = texture(u_pressure, uv + vec2(texel.x, 0)).x;
    float l = texture(u_pressure, uv - vec2(texel.x, 0)).x;
    float t = texture(u_pressure, uv + vec2(0, texel.y)).x;
    float b = texture(u_pressure, uv - vec2(0, texel.y)).x;
    frag = vec4(.25 * (r + l + t + b - texture(u_divergence, uv).x), 0.0, 0.0, 1.0);
}
