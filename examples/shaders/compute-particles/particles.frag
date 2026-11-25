in float glitter;
layout(location = 0) out vec4 color;

void main() {
    float visibility = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    float opacity = visibility * mix(0.26, 0.72, glitter);
    vec3 rgb = mix(vec3(0.16, 0.72, 0.98), vec3(1.0, 0.84, 0.18), glitter);
    color = vec4(rgb * opacity, opacity);
}
