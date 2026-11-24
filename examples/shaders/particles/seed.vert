out vec4 nextState;
float randomValue(float value) { return fract(sin(value * 12.9898) * 43758.5453); }
void main() {
    float id = float(gl_VertexID) + hypr_seed.x * 100.0;
    vec2 position = vec2(randomValue(id + 1.0), randomValue(id + 17.0));
    vec2 velocity = (vec2(randomValue(id + 31.0), randomValue(id + 51.0)) - 0.5) * 0.2;
    nextState = vec4(position, velocity);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
