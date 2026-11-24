in vec2 contentUV;
layout(location = 0) out vec4 color;
void main() {
    float coverage = 1.0 - smoothstep(0.5, 1.0, length(hypr_uv * 2.0 - 1.0));
    coverage *= sin(hypr_progress * 3.14159265);
    color = hypr_sample_source(contentUV) * coverage;
}
