#version 300 es
precision highp float;
// Subtract the pressure gradient, leaving an incompressible flow that rolls into eddies.
uniform sampler2D u_velocity_field, u_pressure;
uniform vec2 u_grid;
out vec4 frag;
void main() {
    vec2 texel = 1.0 / u_grid;
    vec2 uv = gl_FragCoord.xy * texel;
    float r = texture(u_pressure, uv + vec2(texel.x, 0)).x;
    float l = texture(u_pressure, uv - vec2(texel.x, 0)).x;
    float t = texture(u_pressure, uv + vec2(0, texel.y)).x;
    float b = texture(u_pressure, uv - vec2(0, texel.y)).x;
    frag = vec4(texture(u_velocity_field, uv).xy - .5 * vec2(r - l, t - b), 0.0, 1.0);
}
