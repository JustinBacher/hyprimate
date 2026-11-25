out vec2 hypr_uv;
out float glitter;

void main() {
    // One instanced triangle per particle. No CPU position data or vertex buffer.
    vec2 p = hc_read_position(gl_InstanceID);
    vec2 v = hc_read_velocity(gl_InstanceID);
    const vec2 corners[3] = vec2[3](vec2(-2.2, -1.5), vec2(2.2, -1.5), vec2(0.0, 3.0));
    vec2 offset = 2.0 * corners[gl_VertexID] / max(hypr_target_size, vec2(1.0));
    bool marker = all(greaterThan(p, vec2(0.07))) && all(lessThan(p, vec2(0.18, 0.155)));
    gl_Position = marker ? vec4(2.0, 2.0, 0.0, 1.0) : vec4(2.0 * p - 1.0 + offset, 0.0, 1.0);
    hypr_uv = p;
    glitter = clamp(length(v) * 3.5, 0.0, 1.0);
}
