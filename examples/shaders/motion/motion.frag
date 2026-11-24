// Original Hyprimate example. Preserve source alpha, including the blur matte.
layout(location = 0) out vec4 color;
void main() {
    vec4 source = hypr_sample_source(hypr_window_uv());
    if (hypr_blur_mask) {
        color = vec4(source.r);
        return;
    }
    bool resizing = hypr_event == 3;
    float activity = hypr_settling ? 1.0 - smoothstep(0.0, 1.0, hypr_progress) : 1.0;
    vec3 tint = resizing ? vec3(0.83, 0.62, 1.0) : vec3(0.64, 1.0, 0.82);
    color = vec4(source.rgb * mix(vec3(1.0), tint, activity), source.a);
}
