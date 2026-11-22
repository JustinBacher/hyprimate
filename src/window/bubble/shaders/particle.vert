#version 300 es
precision highp float;
layout(location=0) in vec4 geometry;
layout(location=1) in vec4 detail;
uniform vec2 canvas, origin, sourcePos, sourceSize, size;
out vec2 pixel, centre, tail;
out vec4 params;
void main() {
    vec2 scale=sourceSize/size;
    centre=sourcePos+sourceSize*.5+geometry.xy*scale-origin;
    tail=sourcePos+sourceSize*.5+geometry.zw*scale-origin;
    float r=max(detail.x,detail.y)*max(scale.x,scale.y)+2.0+detail.w*2.0;
    vec2 lo=min(centre,tail)-r, hi=max(centre,tail)+r;
    vec2 corner=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    pixel=mix(lo,hi,corner);
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
    params=vec4(detail.xy*max(scale.x,scale.y),detail.zw);
}
