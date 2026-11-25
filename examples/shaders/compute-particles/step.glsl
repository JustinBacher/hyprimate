void kernel(ivec3 id, inout vec2 pos, inout vec2 vel) {
    vec2 p = hc_read_position(id);
    float dt = clamp(hypr_delta, 0.0, 0.05);
    vel = clamp(hc_read_velocity(id) + (vec2(0.38, 0.61) - p) * acceleration * dt, -0.45, 0.45);
    pos = fract(p + vel * dt);
}
