layout(location = 0) out vec4 color;

void main() {
    vec2 uv = hypr_window_uv();
    float x = hypr_direction < 0.0 ? 1.0 - uv.x : uv.x;
    // Move a soft edge from fully outside one side to fully outside the other.
    // The original source and destination remain bound when progress runs backward.
    const float feather = 0.02;
    float edge = mix(-feather, 1.0 + feather, hypr_progress);
    float incoming = 1.0 - smoothstep(edge - feather, edge + feather, x);
    color = mix(hypr_sample_source(uv), hypr_sample_destination(uv), incoming);
}
