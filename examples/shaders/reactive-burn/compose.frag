uniform highp sampler2D heatState;
layout(location = 0) out vec4 color;

void main() {
    vec4 wallpaper = hypr_sample_wallpaper_original(hypr_uv);
    if (wallpaper.a <= 0.0) {
        color = vec4(0.0);
        return;
    }
    vec2 state = decodeHeat(textureLod(heatState, hypr_uv, 0.0));
    float scorch = smoothstep(0.015, 0.65, state.y);
    vec3 charred = wallpaper.rgb * mix(vec3(1.0), vec3(0.38, 0.22, 0.15), scorch);
    float glow = smoothstep(0.025, 0.75, state.x);
    vec3 ember = mix(vec3(0.85, 0.14, 0.025), vec3(1.0, 0.67, 0.15), glow);
    color = vec4(mix(charred, wallpaper.a * ember, 0.58 * glow), wallpaper.a);
}
