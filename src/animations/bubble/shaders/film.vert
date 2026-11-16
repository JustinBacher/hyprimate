#version 300 es
precision highp float;
// One wake ribbon as a triangle strip: two vertices per node, spread across the path.
uniform vec2 canvas, origin, sourcePos, sourceSize, size;
uniform vec4 nodes[48];  // position.xy, half width, arc length (logical px)
uniform vec4 nodeState[48];  // tangent.xy, fade 0..1, density 0..1
out vec2 film;  // arc length (px), signed distance across in half widths
out float fade, density, halfWidth;
void main() {
    int i=gl_VertexID/2;
    float side=(gl_VertexID%2)==0 ? -1.0 : 1.0;
    vec4 n=nodes[i], st=nodeState[i];
    // Wider than the film so its frayed edge has room.
    const float fray=1.3;
    vec2 across=vec2(-st.y,st.x);
    vec2 position=n.xy+across*side*n.z*fray;
    film=vec2(n.w,side*fray);
    fade=st.z;
    density=st.w;
    halfWidth=n.z;
    vec2 pixel=sourcePos+sourceSize*.5+position*(sourceSize/size)-origin;
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
}
