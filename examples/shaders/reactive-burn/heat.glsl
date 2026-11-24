// Store heat in RG and soot in BA, with the high byte first in both pairs.
// Two bytes keep slow healing from rounding away at high refresh rates. R alone
// is also a normalized coarse heat sample for consumers of the published state.
vec4 encodeHeat(vec2 state) {
    vec2 value = round(clamp(state, 0.0, 1.0) * 65535.0);
    vec2 highByte = floor(value / 256.0);
    vec2 lowByte = value - 256.0 * highByte;
    return vec4(highByte.x, lowByte.x, highByte.y, lowByte.y) / 255.0;
}

vec2 decodeHeat(vec4 encoded) {
    return (256.0 * encoded.rb + encoded.ga) / 257.0;
}
