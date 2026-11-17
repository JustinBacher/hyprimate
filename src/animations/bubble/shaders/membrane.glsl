// Shared by every program that reads a membrane. Mirrors BubbleSurface and BubbleDeformation.
struct Hole { vec3 centre; float radius, rim, precursor; vec4 amplitude, phase; };
struct Membrane {
    vec2 halfSize, core;
    float corner, depth, content, film, visibility, ripple, thickness, flow, aureole;
    int spherical, holeCount;
    Hole holes[4];
    vec2 drag;
    vec4 waveCos, waveSin;
    float sheen, round;
    vec2 extent, carry;
    float transparency;
};
bool moving(Membrane s) {
    return s.spherical==0 &&
           (s.drag!=vec2(0) || s.waveCos!=vec4(0) || s.waveSin!=vec4(0) || s.round!=0.0 ||
            s.carry!=vec2(0));
}
// The membrane's half size at its roundness; mirrors bubbleRoundHalf.
vec2 roundHalf(Membrane s) {
    vec2 h=max(s.halfSize,vec2(1.0));
    return mix(h,max(s.extent,vec2(1.0)),s.round);
}
// Contraction: a point of the rectangle mixed toward its place on the ellipse of half extents
// `extent` under the elliptical-grid square-to-disc map; mirrors bubbleRoundPoint.
vec2 roundPoint(Membrane s, vec2 q) {
    if (s.round==0.0) return q;
    vec2 n=clamp(q/max(s.halfSize,vec2(1.0)),-1.5,1.5);
    vec2 disc=s.extent*n*sqrt(max(vec2(0),1.0-n.yx*n.yx*.5));
    return mix(q,disc,s.round);
}
// Displacement of a point of the rounded membrane; mirrors bubbleMotionOffset. The leading edge
// stays with the window, the trailing film lags by drag, bowing out in the middle and narrowing
// across the motion, and capillary modes n = 2..5 ripple the outline.
vec2 motionOffset(Membrane s, vec2 p) {
    vec2 h=roundHalf(s), offset=vec2(0);
    float drag=length(s.drag);
    if (drag>1e-6) {
        vec2 dir=s.drag/drag;
        float reach=dot(abs(dir),h), breadth=dot(abs(dir.yx),h), along=dot(p,dir);
        float w=smoothstep(-1.0,1.0,clamp(along/reach,-1.0,1.0));
        vec2 across=p-dir*along;
        float side=min(1.0,dot(across,across)/(breadth*breadth));
        offset=s.drag*(w*(1.0-.55*side))-across*(.35*drag/reach*w);
    }
    float rounded=clamp(s.round,0.0,1.0);
    vec2 u=p/h, u2=u*u;
    float rho4=dot(u2,u2);
    if (rho4>1e-12) {
        float rho=min(mix(sqrt(sqrt(rho4)),length(u),rounded),1.25), phi=atan(u.y,u.x);
        vec4 n=vec4(2,3,4,5);
        float radial=dot(s.waveCos,cos(n*phi))+dot(s.waveSin,sin(n*phi));
        vec2 g=normalize(u2*u/h)*(1.0-rounded)+normalize(u/h)*rounded;
        offset+=normalize(g)*radial*rho*rho*rho;
    }
    return offset;
}
// Where movement carries a point of the undeformed membrane; mirrors bubbleMotionPoint.
vec2 motionPoint(Membrane s, vec2 q) {
    vec2 p=roundPoint(s,q);
    return p+motionOffset(s,p)+s.carry;
}
// The rectangle point a contracted one came from. The disc-to-square inverse of the elliptical grid
// map starts Newton's method on the mixed map, which converges in a few steps while the roundness
// stays short of a full disc.
vec2 roundSource(Membrane s, vec2 p) {
    if (s.round==0.0) return p;
    vec2 h=max(s.halfSize,vec2(1.0)), E=max(s.extent,vec2(1.0));
    vec2 d=p/E;
    d*=min(1.0,.999/max(length(d),1e-6));
    float t=2.0+d.x*d.x-d.y*d.y, k=2.0*sqrt(2.0);
    vec2 square=.5*vec2(sqrt(max(0.0,t+k*d.x))-sqrt(max(0.0,t-k*d.x)),
                        sqrt(max(0.0,4.0-t+k*d.y))-sqrt(max(0.0,4.0-t-k*d.y)));
    vec2 q=mix(p,h*square,clamp(s.round,0.0,1.0));
    for (int i=0;i<4;i++) {
        vec2 n=clamp(q/h,-1.5,1.5);
        float a=sqrt(max(1e-3,1.0-n.y*n.y*.5)), b=sqrt(max(1e-3,1.0-n.x*n.x*.5));
        // Columns: d/dq.x and d/dq.y of the mixed map.
        mat2 J=mat2(1.0-s.round)+s.round*mat2(E.x*a/h.x,-E.y*n.x*n.y/(2.0*b)/h.x,
                                               -E.x*n.x*n.y/(2.0*a)/h.y,E.y*b/h.y);
        q-=inverse(J)*(roundPoint(s,q)-p);
    }
    return q;
}
// The undeformed point a deformed one came from: the carry is undone first, the displacement is a
// small, smooth field that a few fixed-point steps invert, and contraction is inverted by
// roundSource.
vec2 motionSource(Membrane s, vec2 p) {
    p-=s.carry;
    vec2 r=p;
    for (int i=0;i<3;i++) r=p-motionOffset(s,r);
    return roundSource(s,r);
}
