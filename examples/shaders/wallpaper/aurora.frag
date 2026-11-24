layout(location = 0) out vec4 color;

void main() {
    vec2 uv = hypr_window_uv();
    float aspect = hypr_size.x / max(hypr_size.y, 1.0);
    float x = (uv.x - 0.5) * aspect;
    float phase = speed * hypr_elapsed + 6.2831853 * hypr_seed.x;
    float ridge = 0.42 + 0.11 * sin(2.4 * x + phase)
                      + 0.035 * sin(6.1 * x - 1.4 * phase);
    float distanceToRidge = abs(uv.y - ridge);
    float glow = exp(-12.0 * distanceToRidge);
    float curtain = 0.72 + 0.28 * sin(17.0 * x + 5.0 * uv.y - phase);
    vec3 night = mix(vec3(0.025, 0.042, 0.10), vec3(0.008, 0.014, 0.035), uv.y);
    color = vec4(night + glow * curtain * tint, 1.0);
}
