layout(location = 0) out vec4 color;

void main() {
    float progress = clamp(hypr_progress, 0.0, 1.0);
    float pulse = hypr_event < 2 ? 4.0 * progress * (1.0 - progress) : 1.0 - progress;
    vec2 size = max(hypr_geometry.zw, vec2(1.0));
    vec4 displacement = (hypr_geometry - hypr_previous_geometry) / size.xyxy;
    float motion = clamp(4.0 * length(displacement) + 0.12 * length(hypr_velocity / size.xyxy), 0.0, 1.0);
    float warmth = dot(tint, vec3(0.2126, 0.7152, 0.0722));

    // The burn example consumes R; this channel defines the other components.
    float weight = clamp((0.22 + 0.40 * pulse + 0.50 * motion) * mix(0.45, 1.0, warmth), 0.0, 1.0);
    color = vec4(weight, pulse, warmth, 1.0);
}
