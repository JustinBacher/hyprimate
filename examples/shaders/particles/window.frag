layout(location = 0) out vec4 color;
void main() {
    float amount = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    color = hypr_sample_source(hypr_window_uv()) * amount;
}
