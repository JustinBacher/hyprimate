uniform highp sampler2D previousWave;
layout(location = 0) out vec4 color;

float boxDistance(vec2 point, vec4 rect) {
    vec2 q = abs(point - rect.xy - 0.5 * rect.zw) - 0.5 * rect.zw;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float windowImpulse(vec2 point, vec2 outputSize) {
    float impulse = 0.0;
    for (int i = 0; i < hypr_interaction_count; ++i) {
        int record = 4 * i;
        vec4 event = hypr_interactions[record + 3];
        float current = boxDistance(point, hypr_interactions[record] * outputSize.xyxy);
        float band = 1.0 - smoothstep(1.0, 19.0, abs(current));
        float direction = event.x == 0.0 ? 1.0 : -1.0;
        if (event.x >= 2.0) {
            float previous = boxDistance(point, hypr_interactions[record + 1] * outputSize.xyxy);
            band = max(band, 1.0 - smoothstep(1.0, 19.0, abs(previous)));
            direction = clamp((previous - current) / 16.0, -1.0, 1.0);
        }
        impulse += band * direction * event.y;
    }
    return clamp(impulse, -1.0, 1.0);
}

float heightAt(ivec2 cell, ivec2 lastCell) {
    return decodeWave(texelFetch(previousWave, clamp(cell, ivec2(0), lastCell), 0).rg).x;
}

void main() {
    if (hypr_progress >= 1.0) {
        color = encodeWave(vec2(0.0));
        return;
    }

    ivec2 cell = ivec2(hypr_uv * hypr_target_size);
    ivec2 lastCell = ivec2(hypr_target_size) - 1;
    vec2 state = decodeWave(texelFetch(previousWave, cell, 0).rg);
    float neighbors = heightAt(cell + ivec2(1, 0), lastCell) + heightAt(cell - ivec2(1, 0), lastCell)
                    + heightAt(cell + ivec2(0, 1), lastCell) + heightAt(cell - ivec2(0, 1), lastCell);
    // Two substeps, each at most 1.5 nominal 60 Hz ticks, keep the stencil stable after a stall.
    float stepSize = min(hypr_delta * 60.0, 3.0) / float(hypr_iterations);
    float velocity = (state.y + 0.19 * (neighbors - 4.0 * state.x) * stepSize) * exp(-0.08 * stepSize);
    float height = (state.x + velocity * stepSize) * exp(-0.025 * stepSize);
    if (hypr_iteration == 0 && hypr_interaction_count > 0) {
        vec2 outputSize = max(hypr_output_size, vec2(1.0));
        float impulse = windowImpulse(hypr_uv * outputSize, outputSize);
        height += 0.28 * impulse;
        velocity += 0.48 * impulse;
    }
    color = encodeWave(vec2(height, velocity));
}
