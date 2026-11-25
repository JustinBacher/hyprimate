#version 300 es
precision highp float;
out vec2 vNDC;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2),float(gl_VertexID & 2));
    vNDC = p*2.0-1.0;
    gl_Position = vec4(vNDC,1.0,1.0);
}
