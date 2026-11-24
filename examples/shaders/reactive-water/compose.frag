uniform highp sampler2D waveState;
layout(location = 0) out vec4 color;

float waveHeight(vec2 uv) {
    return decodeWave(textureLod(waveState, uv, 0.0).rg).x;
}

void main() {
    if (!hypr_wallpaper_original_available || hypr_progress >= 1.0) {
        // The native background is already below this pass; restore it exactly.
        color = vec4(0.0);
        return;
    }
    vec2 uv = hypr_uv;
    vec2 texel = 1.0 / vec2(textureSize(waveState, 0));
    vec2 dx = vec2(texel.x, 0.0);
    vec2 dy = vec2(0.0, texel.y);
    vec2 slope = vec2(waveHeight(uv + dx) - waveHeight(uv - dx),
                      waveHeight(uv + dy) - waveHeight(uv - dy));
    float settle = 1.0 - smoothstep(0.62, 1.0, hypr_progress);
    float coverage = settle * smoothstep(0.012, 0.15, length(slope));
    vec2 offset = 42.0 * settle * slope / max(hypr_output_size, vec2(1.0));
    color = hypr_sample_wallpaper_original(clamp(uv + offset, 0.0, 1.0)) * coverage;
}
