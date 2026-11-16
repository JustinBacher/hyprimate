#version 300 es
precision highp float;
precision highp int;
struct Hole { vec3 centre; float radius, rim, precursor; vec4 amplitude, phase; };
struct Membrane {
    vec2 halfSize, core;
    float corner, depth, content, film, visibility, ripple, thickness, flow, aureole;
    int spherical, holeCount;
    Hole holes[4];
};
uniform Membrane sheet;
uniform vec2 size, canvas, origin, sourcePos, sourceSize;
uniform int sheetMode, sheetHole;
uniform vec3 pivot, axis, translation;
uniform float rotation, shrink;
out vec3 materialPoint, filmNormal;
out vec2 rootPoint, sourcePoint;
flat out int holeIndex;
out float facing, boundaryRadius, roll;
void main() {
    int cell=gl_VertexID/6, corner=gl_VertexID%6;
    int x=cell/12, y=cell%12;
    int dx=(corner==1 || corner==2 || corner==4) ? 1 : 0;
    int dy=(corner==2 || corner==4 || corner==5) ? 1 : 0;
    float phi=6.28318530718*float(x+dx)/96.0;
    float u=float(y+dy)/12.0;
    roll=u;
    holeIndex=sheetMode!=0 ? sheetHole : gl_InstanceID;
    Hole h=sheet.holes[holeIndex];
    float boundary=h.radius*(1.0+dot(h.amplitude,cos(vec4(3,5,9,14)*phi+h.phase)));
    boundaryRadius=boundary;
    float R=min(sheet.halfSize.x,sheet.halfSize.y);
    float physical=boundary*(sheet.spherical!=0 ? R : 1.0);
    float width=max(0.0,min(64.0,min(.24*R,.45*physical)))*clamp(sheet.aureole,0.0,1.0);
    if (sheet.spherical!=0) width=min(width,max(0.0,3.14159265359*R-physical));
    float q=clamp((physical/R-.04)/(.85-.04),0.0,1.0);
    float turn=max(.8,1.0+3.8*q*q*(3.0-2.0*q)+.65*sin(3.0*phi+h.phase.x)+.35*sin(7.0*phi+h.phase.y));
    float a=width/turn, theta=u*turn;
    vec3 base, outward, normal;
    if (sheet.spherical!=0) {
        vec3 axis=abs(h.centre.z)<.9 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 e1=normalize(cross(axis,h.centre)), e2=cross(h.centre,e1);
        vec3 direction=e1*cos(phi)+e2*sin(phi);
        float root=boundary+width/max(R,.001);
        normal=h.centre*cos(root)+direction*sin(root);
        outward=-h.centre*sin(root)+direction*cos(root);
        base=R*normal;
        float travel=u*width/max(R,.001);
        materialPoint=normal*cos(travel)-outward*sin(travel);
    } else {
        outward=vec3(cos(phi),sin(phi),0);
        normal=vec3(0,0,1);
        base=vec3(h.centre.xy+outward.xy*(boundary+width),0);
        materialPoint=base-outward*(u*width);
    }
    vec3 position=base-outward*a*sin(theta)+normal*a*(1.0-cos(theta));
    filmNormal=normal*cos(theta)+outward*sin(theta);
    rootPoint=base.xy;
    sourcePoint=sheet.spherical!=0 ? materialPoint.xy*R : materialPoint.xy;
    facing=normal.z;
    if (sheetMode!=0) {
        vec3 v=(position-pivot)*shrink;
        float c=cos(rotation), s=sin(rotation);
        position=pivot+translation+v*c+cross(axis,v)*s+axis*dot(axis,v)*(1.0-c);
        v=filmNormal;
        filmNormal=v*c+cross(axis,v)*s+axis*dot(axis,v)*(1.0-c);
    }
    vec2 pixel=sourcePos+sourceSize*((position.xy+size*.5)/size)-origin;
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
}
