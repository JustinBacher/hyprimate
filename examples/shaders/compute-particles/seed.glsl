uint hash32(uint v) {
    v ^= v >> 16u;
    v *= 0x7feb352du;
    v ^= v >> 15u;
    v *= 0x846ca68bu;
    return v ^ (v >> 16u);
}

float particle_random(uint value) {
    return float(hash32(value) >> 8u) * (1.0 / 16777216.0);
}

void kernel(ivec3 id, inout vec2 pos, inout vec2 vel) {
    uint k = uint(hc_index(id)) ^ uint(hypr_seed.x * 16777216.0);
    pos = vec2(particle_random(k), particle_random(k ^ 0x9e3779b9u));
    vel = (vec2(particle_random(k ^ 0x85ebca6bu), particle_random(k ^ 0xc2b2ae35u)) - 0.5) * 0.2;
}
