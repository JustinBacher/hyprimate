layout(location = 0) in vec4 state;
layout(std140) uniform Physics { vec4 forces; };
out vec4 nextState;
void main() {
    float dt = min(hypr_delta, 0.05);
    vec2 velocity = state.zw + forces.xy * dt;
    vec2 position = state.xy + velocity * dt;
    nextState = vec4(position, velocity * exp(-forces.z * dt));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
