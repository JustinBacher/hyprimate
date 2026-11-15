#pragma once

constexpr const char *CURL_VERTEX = R"GLSL(#version 300 es
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
    holeIndex=gl_InstanceID;
    Hole h=sheet.holes[holeIndex];
    float boundary=h.radius*(1.0+dot(h.amplitude,cos(vec4(3,5,9,14)*phi+h.phase)));
    boundaryRadius=boundary;
    float R=min(sheet.halfSize.x,sheet.halfSize.y);
    float physical=boundary*(sheet.spherical!=0 ? R : 1.0);
    float width=max(0.0,min(48.0,min(.18*R,.45*physical)))*clamp(sheet.aureole,0.0,1.0);
    float turn=2.7+.65*sin(3.0*phi+h.phase.x)+.35*sin(7.0*phi+h.phase.y);
    float a=width/turn, theta=u*turn;
    vec3 base, outward, normal;
    if (sheet.spherical!=0) {
        vec3 axis=abs(h.centre.z)<.9 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 e1=normalize(cross(axis,h.centre)), e2=cross(h.centre,e1);
        vec3 direction=e1*cos(phi)+e2*sin(phi);
        normal=h.centre*cos(boundary)+direction*sin(boundary);
        outward=-h.centre*sin(boundary)+direction*cos(boundary);
        base=R*normal;
        float travel=u*width/max(R,.001);
        materialPoint=normal*cos(travel)-outward*sin(travel);
    } else {
        outward=vec3(cos(phi),sin(phi),0);
        normal=vec3(0,0,1);
        base=vec3(h.centre.xy+outward.xy*boundary,0);
        materialPoint=base-outward*(u*width);
    }
    vec3 position=base-outward*a*sin(theta)+normal*a*(1.0-cos(theta));
    filmNormal=normal*cos(theta)+outward*sin(theta);
    rootPoint=base.xy;
    sourcePoint=sheet.spherical!=0 ? materialPoint.xy*R : materialPoint.xy;
    sourcePoint+=outward.xy*(.3*sheet.aureole*max(physical*.25,1.0));
    facing=normal.z;
    vec2 pixel=sourcePos+sourceSize*((position.xy+size*.5)/size)-origin;
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
}
)GLSL";

constexpr const char *CURL_FRAGMENT = R"GLSL(#version 300 es
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
uniform sampler2D image, wallpaperImage, filmLut;
uniform vec2 size, sourcePos, sourceSize, textureOrigin, imageSize;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform int wallpaperReady;
in vec3 materialPoint, filmNormal;
in vec2 rootPoint, sourcePoint;
flat in int holeIndex;
in float facing, boundaryRadius, roll;
out vec4 colour;
float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q=abs(p)-h+r;
    return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
}
vec3 env(vec3 n) {
    vec3 r=reflect(vec3(0,0,-1),n);
    float sky=mix(.05,.5,smoothstep(-.3,.6,-r.y));
    vec2 q=r.xy/max(.25,abs(r.z)+.4);
    vec2 k=(q-vec2(-.36,-.40))/vec2(.17,.24);
    float window=1.0-smoothstep(.78,1.0,max(abs(k.x),abs(k.y)));
    window*=mix(.2,1.0,min(smoothstep(.035,.09,abs(k.x)),smoothstep(.035,.09,abs(k.y+.12))));
    float fill=exp(-dot((q-vec2(.42,-.16))*7.0,(q-vec2(.42,-.16))*7.0));
    return vec3(sky+4.0*window+1.2*fill);
}
void main() {
    if (boundaryRadius<=0.0 || (sheet.spherical!=0 && boundaryRadius>=3.14159265359)) discard;
    float R=min(sheet.halfSize.x,sheet.halfSize.y);
    vec2 p=sheet.spherical!=0 ? materialPoint.xy*R : materialPoint.xy;
    float sd=max(boxDistance(p,sheet.halfSize,clamp(sheet.corner,0.0,R)),
                 boxDistance(rootPoint,sheet.halfSize,clamp(sheet.corner,0.0,R)));
    float coverage=1.0-smoothstep(-max(fwidth(sd),.5),max(fwidth(sd),.5),sd);
    for (int i=0;i<4;i++) {
        if (i>=sheet.holeCount) break;
        if (i==holeIndex) continue;
        Hole h=sheet.holes[i];
        float phi, dist;
        if (sheet.spherical!=0) {
            vec3 axis=abs(h.centre.z)<.9 ? vec3(0,0,1) : vec3(1,0,0);
            vec3 e1=normalize(cross(axis,h.centre)), e2=cross(h.centre,e1);
            vec3 point=normalize(materialPoint);
            phi=atan(dot(point,e2),dot(point,e1));
            dist=acos(clamp(dot(point,h.centre),-1.0,1.0))*R;
        } else {
            vec2 delta=p-h.centre.xy;
            phi=atan(delta.y,delta.x); dist=length(delta);
        }
        float boundary=h.radius*(sheet.spherical!=0 ? R : 1.0)*(1.0+dot(h.amplitude,cos(vec4(3,5,9,14)*phi+h.phase)));
        float d=dist-boundary, aa=max(fwidth(d),.35);
        if (h.radius>0.0) coverage*=smoothstep(-aa,aa,d);
    }
    if (coverage<=0.0) discard;
    vec3 n=normalize(filmNormal);
    vec3 reflectance=texture(filmLut,vec2(clamp(sheet.thickness/1500.0,0.0,1.0),abs(n.z))).rgb;
    float lightAlpha=dot(reflectance,vec3(.2126,.7152,.0722));
    vec3 light=reflectance*env(n.z<0.0 ? -n : n);
    vec4 imageColour=vec4(0);
    float imageMask=1.0;
    if (sheet.spherical==0) {
        vec2 corePoint=sourcePoint/max(sheet.halfSize,vec2(1));
        vec2 b=max(vec2(0),vec2(1)-corePoint*corePoint);
        vec2 grad=-4.0*corePoint*vec2(b.x*b.y*b.y,b.y*b.x*b.x);
        vec2 coreNormal=-(grad*(sheet.core.x+sheet.core.y*dot(corePoint,corePoint))+b.x*b.x*b.y*b.y*sheet.core.y*2.0*corePoint)/max(sheet.halfSize,vec2(1));
        float sdRoot=boxDistance(rootPoint,sheet.halfSize,clamp(sheet.corner,0.0,R));
        float radius=clamp(1.0+sdRoot/max(R,1.0),0.0,1.0);
        float cap=sqrt(max(0.0,1.0-radius*radius));
        float curve=clamp(sheet.depth/max(R,1.0),0.0,1.0);
        vec2 unit=.5+.5*corePoint*(1.0-.12*curve*(1.0-cap))-coreNormal*.15;
        vec2 uv=(sourcePos+sourceSize*unit-textureOrigin)/imageSize;
        vec2 lo=max((sourcePos-textureOrigin+.5)/imageSize,vec2(.5)/imageSize);
        vec2 hi=min((sourcePos+sourceSize-textureOrigin-.5)/imageSize,vec2(1)-.5/imageSize);
        imageColour=texture(image,clamp(uv,lo,hi));
        imageMask=mix(1.0,imageColour.a,clamp(sheet.content,0.0,1.0));
        // The rolled film carries the consumed image inward; the turned-over free edge is transparent soap film.
        imageColour*=sheet.content*(1.0-smoothstep(.1,.72,roll));
        imageColour*=mix(.38,1.0,smoothstep(-.3,.3,n.z));
    } else if (wallpaperReady!=0 && facing>0.0) {
        vec2 unit=(sourcePoint+size*.5)/size;
        vec2 uv=wallpaperOrigin+wallpaperX*unit.x+wallpaperY*unit.y;
        imageColour=texture(wallpaperImage,clamp(uv,wallpaperPixel*.5,vec2(1)-wallpaperPixel*.5))*(.25*(1.0-clamp(facing,0.0,1.0))*sheet.content);
    }
    float attenuation=sheet.spherical!=0 && facing<0.0 ? .45 : 1.0;
    float alpha=(lightAlpha*imageMask+imageColour.a*(1.0-lightAlpha))*coverage*sheet.visibility*attenuation;
    vec3 rgb=(light*imageMask+imageColour.rgb*(1.0-lightAlpha))*coverage*sheet.visibility*attenuation;
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
)GLSL";
