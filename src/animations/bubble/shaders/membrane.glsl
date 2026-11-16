// Shared by every program that reads a membrane. Mirrors BubbleSurface and BubbleDeformation.
struct Hole { vec3 centre; float radius, rim, precursor; vec4 amplitude, phase; };
struct Membrane {
    vec2 halfSize, core;
    float corner, depth, content, film, visibility, ripple, thickness, flow, aureole;
    int spherical, holeCount;
    Hole holes[4];
    vec2 drag;
    vec4 waveCos, waveSin;
    float sheen;
};
bool moving(Membrane s) {
    return s.spherical==0 && (s.drag!=vec2(0) || s.waveCos!=vec4(0) || s.waveSin!=vec4(0));
}
// Where movement carries a point of the undeformed membrane (logical px from its centre, y down);
// mirrors bubbleMotionOffset. The leading edge stays with the window, the trailing film lags by drag,
// bowing out in the middle and narrowing across the motion, and capillary modes n = 2..5 ripple the
// outline.
vec2 motionOffset(Membrane s, vec2 p) {
    vec2 h=max(s.halfSize,vec2(1.0)), offset=vec2(0);
    float drag=length(s.drag);
    if (drag>1e-6) {
        vec2 dir=s.drag/drag;
        float reach=dot(abs(dir),h), breadth=dot(abs(dir.yx),h), along=dot(p,dir);
        float w=smoothstep(-1.0,1.0,clamp(along/reach,-1.0,1.0));
        vec2 across=p-dir*along;
        float side=min(1.0,dot(across,across)/(breadth*breadth));
        offset=s.drag*(w*(1.0-.55*side))-across*(.35*drag/reach*w);
    }
    vec2 u=p/h, u2=u*u;
    float rho4=dot(u2,u2);
    if (rho4>1e-12) {
        float rho=min(sqrt(sqrt(rho4)),1.25), phi=atan(u.y,u.x);
        vec4 n=vec4(2,3,4,5);
        float radial=dot(s.waveCos,cos(n*phi))+dot(s.waveSin,sin(n*phi));
        offset+=normalize(u2*u/h)*radial*rho*rho*rho;
    }
    return offset;
}
// The undeformed point a deformed one came from: the offset is a small, smooth field, so a few
// fixed-point steps invert it.
vec2 motionSource(Membrane s, vec2 p) {
    vec2 q=p;
    for (int i=0;i<3;i++) q=p-motionOffset(s,q);
    return q;
}
