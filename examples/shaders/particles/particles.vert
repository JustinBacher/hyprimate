layout(location = 0) in vec4 state;
out vec2 hypr_uv;
out vec2 contentUV;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(-1,1),
                                    vec2(-1,1), vec2(1,-1), vec2(1,1));
    vec2 corner = corners[gl_VertexID];
    vec2 windowUV = state.xy + corner * 4.0 / hypr_size;
    vec2 targetUV = (windowUV - hypr_domain.xy) / hypr_domain.zw;
    hypr_uv = corner * 0.5 + 0.5;
    contentUV = state.xy;
    gl_Position = vec4(targetUV * 2.0 - 1.0, 0.0, 1.0);
}
