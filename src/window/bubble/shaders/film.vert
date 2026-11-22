#version 300 es
precision highp float;
// One run of a wake strand as a triangle strip: two vertices per node, spread across the path.
uniform vec2 canvas, origin, sourcePos, sourceSize, size;
uniform vec4 nodes[48];  // position.xy, half width, arc length (logical px)
uniform vec4 nodeState[48];  // tangent.xy, laid-down thickness nm, evaporated nm
out vec2 film;  // arc length (px), signed distance across in half widths
out float thickness, evaporated, halfWidth;
void main() {
    int i=gl_VertexID/2;
    float side=(gl_VertexID%2)==0 ? -1.0 : 1.0;
    vec4 n=nodes[i], st=nodeState[i];
    // Wider than the film so its ragged border has room, and never thinner than a pixel and a half.
    float scale=max(sourceSize.x/size.x,1e-3);
    float reach=max(n.z*1.15,1.5/scale);
    vec2 across=vec2(-st.y,st.x);
    vec2 position=n.xy+across*side*reach;
    film=vec2(n.w,side*reach/max(n.z,1e-3));
    thickness=st.z;
    evaporated=st.w;
    halfWidth=n.z;
    vec2 pixel=sourcePos+sourceSize*.5+position*(sourceSize/size)-origin;
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
}
