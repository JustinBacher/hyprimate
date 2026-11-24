// R and G are signed height and velocity; both are exactly byte 128 when still.
vec4 encodeWave(vec2 state) {
    return vec4((128.0 + round(127.0 * clamp(state, -1.0, 1.0))) / 255.0, 0.0, 1.0);
}

vec2 decodeWave(vec2 encoded) {
    return (255.0 * encoded - 128.0) / 127.0;
}
