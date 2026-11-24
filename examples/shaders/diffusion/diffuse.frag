uniform sampler2D previous;
layout(location = 0) out vec4 color;
void main() {
    vec2 px = 1.0 / vec2(textureSize(previous, 0));
    float center = texture(previous, hypr_uv).r;
    float neighbors = texture(previous, hypr_uv + vec2(px.x, 0.0)).r
                    + texture(previous, hypr_uv - vec2(px.x, 0.0)).r
                    + texture(previous, hypr_uv + vec2(0.0, px.y)).r
                    + texture(previous, hypr_uv - vec2(0.0, px.y)).r;
    float heat = clamp(center + diffusion * (neighbors - 4.0 * center), 0.0, 1.0);
    color = vec4(vec3(heat), 1.0);
}
