#version 300 es
precision highp float;
precision highp sampler3D;
#include "common.glsl"
// MacCormack predictor: plain semi-Lagrangian transport of the density and its age in the
// scrolled grid, by the flow plus the density-only gathering drift.
uniform sampler2D u_velocity_field, u_dye, u_age;
uniform vec2 u_grid, u_domain, u_shift, u_half, u_carry, u_offset;
uniform float u_dt;
layout(location = 0) out vec4 frag;
layout(location = 1) out vec4 aged;
void main() {
    vec2 uv = gl_FragCoord.xy / u_grid;
    vec2 rel = (uv - .5) * u_domain + u_offset - u_carry;
    vec2 v = texture(u_velocity_field, uv).xy + gatherDrift(rel, u_half);
    vec2 displacement = u_dt * v;
    float retained = 1.0;
    if (u_drag_motion.z > 0.0 && u_mass > 0.0 && dot(u_drag_motion.xy, u_drag_motion.xy) > 0.0) {
        float attached = dragAttachment(rel) * u_drag_motion.z;
        displacement = mix(displacement, u_drag_motion.xy, attached);
        retained = dragRetention(rel, displacement);
    }
    vec2 source = previousUV(uv, u_shift) - displacement / u_previous_domain;
    vec2 inbounds = step(vec2(0.0), source) * step(source, vec2(1.0));
    frag = texture(u_dye, source) * inbounds.x * inbounds.y * retained;
    aged = vec4(texture(u_age, source).r * inbounds.x * inbounds.y * retained, 0.0, 0.0, 1.0);
}
