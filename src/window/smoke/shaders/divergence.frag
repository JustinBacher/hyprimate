#version 300 es
precision highp float;
uniform sampler2D u_velocity_field;
uniform vec2 u_grid;
out vec4 frag;
void main() {
    vec2 texel = 1.0 / u_grid;
    vec2 uv = gl_FragCoord.xy * texel;
    float r = texture(u_velocity_field, uv + vec2(texel.x, 0)).x;
    float l = texture(u_velocity_field, uv - vec2(texel.x, 0)).x;
    float t = texture(u_velocity_field, uv + vec2(0, texel.y)).y;
    float b = texture(u_velocity_field, uv - vec2(0, texel.y)).y;
    frag = vec4(.5 * ((r - l) + (t - b)), 0.0, 0.0, 1.0);
}
