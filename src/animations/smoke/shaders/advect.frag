#version 300 es
precision highp float;
precision highp sampler3D;
#include "common.glsl"
// MacCormack predictor: plain semi-Lagrangian transport of the density in the scrolled grid,
// by the flow plus the density-only gathering drift.
uniform sampler2D u_velocity_field, u_dye;
uniform vec2 u_grid, u_domain, u_shift, u_half, u_carry, u_offset;
uniform float u_dt;
out vec4 frag;
void main() {
    vec2 uv = gl_FragCoord.xy / u_grid;
    vec2 rel = (uv - .5) * u_domain + u_offset - u_carry;
    vec2 v = texture(u_velocity_field, uv).xy + gatherDrift(rel, u_half);
    vec2 source = previousUV(uv, u_shift) - u_dt * v / u_previous_domain;
    vec2 inbounds = step(vec2(0.0), source) * step(source, vec2(1.0));
    frag = texture(u_dye, source) * inbounds.x * inbounds.y;
}
