void kernel(ivec3 id, inout float energy) {
    if (id.x == hc_domain.x / 3 && id.y == hc_domain.y / 4) energy += 1.0;
    if (id.x == 2 * hc_domain.x / 3 && id.y == 3 * hc_domain.y / 4) energy += 0.6;
}
