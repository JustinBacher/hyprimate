layout(location = 0) out vec4 color;

void main() {
    vec2 uv = hypr_window_uv();
    vec4 source = hypr_sample_source(uv);
    float visibility = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    bool marker = all(greaterThan(uv, vec2(0.075))) && all(lessThan(uv, vec2(0.175, 0.15)));
    color = marker && visibility > 0.025 ? vec4(vec3(0.95, 0.30, 0.75) * source.a, source.a)
                                       : source * visibility;
}
