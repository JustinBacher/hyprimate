layout(location = 0) out vec4 color;

void main() {
    vec2 uv = hypr_window_uv();
    vec4 source = hypr_event == 5 ? vec4(0.04, 0.07, 0.12, 1.0) : hypr_sample_source(uv);
    ivec2 cell = clamp(ivec2(uv * vec2(hc_size_heat.xy)), ivec2(0), hc_size_heat.xy - 1);
    float energy = hc_read_heat(ivec3(cell, 0));
    float intensity = clamp(energy * 85.0, 0.0, 1.0);
    vec3 tinted = mix(source.rgb, vec3(1.0, 0.26, 0.06) * source.a, intensity);
    float visibility = hypr_event == 0 ? hypr_progress : hypr_event == 1 ? 1.0 - hypr_progress : 1.0;
    bool marker = all(greaterThan(uv, vec2(0.075))) && all(lessThan(uv, vec2(0.175, 0.15)));
    color = marker && visibility > 0.025 ? vec4(vec3(0.08, 0.92, 0.66) * source.a, source.a)
                                       : vec4(tinted, source.a) * visibility;
}
