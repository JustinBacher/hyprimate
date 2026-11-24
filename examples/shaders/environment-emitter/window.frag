layout(location = 0) out vec4 color;

void main() {
    vec4 source = hypr_sample_source(hypr_window_uv());
    float progress = smoothstep(0.0, 1.0, hypr_progress);
    float coverage = hypr_event == 0 ? progress : hypr_event == 1 ? 1.0 - progress : 1.0;
    if (hypr_blur_mask) {
        color = vec4(source.r * coverage);
        return;
    }
    float activity = hypr_event < 2 ? 4.0 * progress * (1.0 - progress) : 1.0 - progress;
    vec2 uv = hypr_environment_uv(hypr_window_uv());
    float heat = hypr_environment_state_available && hypr_inside_output(uv)
        ? textureLod(hypr_environment_state, uv, 0.0).r : 0.0;
    color = vec4(source.rgb * mix(vec3(1.0), tint, 0.45 * activity + .25 * heat), source.a) * coverage;
}
