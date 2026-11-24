// Original minimal Hyprimate example. Source and output are premultiplied.
layout(location = 0) out vec4 color;
void main() {
    float coverage = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    vec4 source = hypr_sample_source(hypr_window_uv());
    color = vec4(source.rgb * tint, source.a) * coverage;
}
