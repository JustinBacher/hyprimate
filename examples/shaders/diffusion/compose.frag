uniform sampler2D heatMap;
layout(location = 0) out vec4 color;
void main() {
    vec2 uv = hypr_window_uv();
    vec4 source = hypr_sample_source(uv);
    float progress = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    float heat = texture(heatMap, clamp(uv, 0.0, 1.0)).r;
    float edge = smoothstep(-0.08, 0.0, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
    float glow = heat * (1.0 - source.a) * edge * 0.35;
    color = source * progress + vec4(vec3(1.0, 0.35, 0.05) * glow, glow) * progress;
}
