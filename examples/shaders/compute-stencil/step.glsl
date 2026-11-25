void kernel(ivec3 id, inout float energy) {
    float c = hc_read_previous(id);
    float left = hc_read_previous(id - ivec3(1, 0, 0));
    float right = hc_read_previous(id + ivec3(1, 0, 0));
    float down = hc_read_previous(id - ivec3(0, 1, 0));
    float up = hc_read_previous(id + ivec3(0, 1, 0));
    energy = c + diffusion * (left + right + down + up - 4.0 * c);
}
