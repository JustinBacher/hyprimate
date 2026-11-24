layout(location = 0) out vec4 color;
void main() {
    float radius = length((hypr_uv - hypr_pointer) * vec2(hypr_size.x / hypr_size.y, 1.0));
    color = vec4(vec3(1.0 - smoothstep(0.02, 0.12, radius)), 1.0);
}
