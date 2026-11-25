#version 300 es
precision highp float;
uniform mat4 viewProjection;
uniform mat4 model;
uniform vec4 imageRect;
out vec2 vUV;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1),float((gl_VertexID >> 1) & 1));
    vUV = imageRect.xy + vec2(p.x,1.0-p.y)*imageRect.zw;
    gl_Position = viewProjection*model*vec4(p-0.5,0.0,1.0);
}
