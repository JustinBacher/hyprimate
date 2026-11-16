#version 300 es
precision highp float;
// One instanced quad per microbubble, covering the bubble and its neck to the parent film.
layout(location=0) in vec4 shape;  // centre.xy, radius, growth (logical px)
layout(location=1) in vec4 neck;  // anchor.xy, attach, collapse
layout(location=2) in vec4 filmInfo;  // thickness nm, seed, -, -
uniform vec2 canvas, origin, sourcePos, sourceSize, size;
out vec2 pixel, centre, anchor;
out vec4 params;  // radius px, growth, attach, collapse
out vec2 filmData;
void main() {
    vec2 scale=sourceSize/size;
    centre=sourcePos+sourceSize*.5+shape.xy*scale-origin;
    anchor=sourcePos+sourceSize*.5+neck.xy*scale-origin;
    float r=shape.z*max(scale.x,scale.y);
    float pad=r*1.4+3.0;
    vec2 lo=min(centre,anchor)-pad, hi=max(centre,anchor)+pad;
    vec2 corner=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    pixel=mix(lo,hi,corner);
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
    params=vec4(r,shape.w,neck.z,neck.w);
    filmData=filmInfo.xy;
}
