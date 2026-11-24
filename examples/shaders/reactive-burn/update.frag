uniform highp sampler2D previousHeat;
layout(location = 0) out vec4 color;

float boxDistance(vec2 point, vec4 rect) {
    vec2 q = abs(point - rect.xy - 0.5 * rect.zw) - 0.5 * rect.zw;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sweptDistance(vec2 point, vec4 previous, vec4 current) {
    // Each side's signed separation is linear during the sweep. Intersect the
    // times when the point is inside all four sides, including changing size.
    vec4 start = vec4(previous.xy - point, point - (previous.xy + previous.zw));
    vec4 end = vec4(current.xy - point, point - (current.xy + current.zw));
    vec4 travel = end - start;
    vec4 crossing = -start * sign(travel) / max(abs(travel), 0.0001);
    vec4 enter = mix(vec4(0.0), crossing, lessThan(travel, vec4(0.0)));
    vec4 exit = mix(vec4(1.0), crossing, greaterThan(travel, vec4(0.0)));
    float first = max(max(enter.x, enter.y), max(enter.z, enter.w));
    float last = min(min(exit.x, exit.y), min(exit.z, exit.w));
    // For points just outside the sweep, the same sample gives a soft fringe.
    float along = 0.5 * (clamp(first, 0.0, 1.0) + clamp(last, 0.0, 1.0));
    return boxDistance(point, mix(previous, current, along));
}

float windowHeat(vec2 point, vec2 outputSize) {
    float heat = 0.0;
    for (int i = 0; i < hypr_interaction_count; ++i) {
        int record = 4 * i;
        vec4 event = hypr_interactions[record + 3];
        bool moving = event.x >= 2.0;
        vec4 current = hypr_interactions[record] * outputSize.xyxy;
        vec4 previous = moving ? hypr_interactions[record + 1] * outputSize.xyxy : current;
        vec2 lo = min(current.xy, previous.xy) - 24.0;
        vec2 hi = max(current.xy + current.zw, previous.xy + previous.zw) + 24.0;
        if (any(lessThan(point, lo)) || any(greaterThan(point, hi))) continue;

        float distanceNow = boxDistance(point, current);
        float stamp = 1.0 - smoothstep(2.0, 20.0, abs(distanceNow));
        if (moving) {
            // Expose the swept trail where it is no longer under the window.
            float swept = sweptDistance(point, previous, current);
            float wake = (1.0 - smoothstep(-3.0, 18.0, swept)) * smoothstep(-11.0, 5.0, distanceNow);
            vec4 velocity = hypr_interactions[record + 2] * outputSize.xyxy;
            float speed = length(velocity.xy) + 0.5 * length(velocity.zw);
            stamp = max(stamp, 0.9 * wake) * mix(0.60, 1.0, smoothstep(0.0, 950.0, speed));
        }

        // The emitter adds to the native interaction; it is never required.
        float weight = hypr_disturbances_available ? texelFetch(hypr_disturbances, ivec2(i, 0), 0).r : 0.0;
        heat = max(heat, stamp * event.y * (0.85 + 0.90 * weight));
    }
    return heat;
}

void main() {
    ivec2 cell = ivec2(hypr_uv * hypr_target_size);
    vec2 stored = decodeHeat(texelFetch(previousHeat, cell, 0));
    vec2 outputSize = max(hypr_output_size, vec2(1.0));
    float contact = windowHeat(hypr_uv * outputSize, outputSize);
    // Embers cool quickly; scorch survives the settle interval and continues
    // healing on later updates, without scheduling idle wallpaper frames.
    vec2 cooled = max(stored - max(hypr_delta, 0.0) * vec2(0.92, 0.14), 0.0);
    color = encodeHeat(max(cooled, vec2(contact, 0.68 * contact)));
}
