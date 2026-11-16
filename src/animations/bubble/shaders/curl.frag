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
uniform sampler2D image, wallpaperImage, filmLut;
uniform vec2 size, sourcePos, sourceSize, textureOrigin, imageSize;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform int wallpaperReady;
struct Mask { vec4 domain, radii, onsets; };
uniform Mask masks[7];
uniform int sheetMode, maskCount;
uniform vec2 sector;
uniform float imageFade;
in vec3 materialPoint, filmNormal;
in vec2 rootPoint, sourcePoint;
flat in int holeIndex;
in float facing, boundaryRadius, roll;
out vec4 colour;
float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q=abs(p)-h+r;
    return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
}
void coordinates(Hole h, vec3 material, float R, out float phi, out float dist) {
    if (sheet.spherical!=0) {
        vec3 axis=abs(h.centre.z)<.9 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 e1=normalize(cross(axis,h.centre)), e2=cross(h.centre,e1);
        vec3 point=normalize(material);
        phi=atan(dot(point,e2),dot(point,e1));
        dist=acos(clamp(dot(point,h.centre),-1.0,1.0))*R;
    } else {
        vec2 delta=material.xy-h.centre.xy;
        phi=atan(delta.y,delta.x);
        dist=length(delta);
    }
}
float peelWidth(float boundary, float R) {
    float width=max(0.0,min(64.0,min(.24*R,.45*boundary)))*clamp(sheet.aureole,0.0,1.0);
    return sheet.spherical!=0 ? min(width,max(0.0,3.14159265359*R-boundary)) : width;
}
bool containsMask(Mask mask, vec3 material, float R) {
    int index=int(mask.domain.x);
    Hole h=sheet.holes[index];
    float phi, dist;
    coordinates(h,material,R,phi,dist);
    vec4 amp=mask.onsets[index]*vec4(.014,.012,.010,.008);
    float boundary=mask.radii[index]*(sheet.spherical!=0 ? R : 1.0)*
        (1.0+dot(amp,cos(vec4(3,5,9,14)*phi+h.phase)));
    float width=peelWidth(boundary,R);
    if (width<=0.0) return false;
    float u=1.0-(dist-boundary)/width;
    float delta=atan(sin(phi-mask.domain.y),cos(phi-mask.domain.y));
    float limit=mask.domain.z*(.82+.14*sin(3.14159265359*u)+.04*sin(3.0*3.14159265359*u+h.phase.x));
    if (u<0.0 || u>1.0 || abs(delta)>limit) return false;
    float ownRank=(dist-boundary)/width;
    // A sector removes only the material its source hole owned at detachment.
    for (int i=0;i<4;i++) {
        if (i>=sheet.holeCount) break;
        if (i==index || mask.radii[i]<=0.0) continue;
        Hole other=sheet.holes[i];
        float otherPhi, otherDist;
        coordinates(other,material,R,otherPhi,otherDist);
        vec4 otherAmp=mask.onsets[i]*vec4(.014,.012,.010,.008);
        float otherBoundary=mask.radii[i]*(sheet.spherical!=0 ? R : 1.0)*
            (1.0+dot(otherAmp,cos(vec4(3,5,9,14)*otherPhi+other.phase)));
        float otherWidth=peelWidth(otherBoundary,R);
        float rank=(otherDist-otherBoundary)/max(otherWidth,.00001);
        if (otherWidth>0.0 && rank>=0.0 && rank<=1.0 &&
            (rank<ownRank || (rank==ownRank && i<index))) return false;
    }
    return true;
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
    float sd=boxDistance(p,sheet.halfSize,clamp(sheet.corner,0.0,R));
    float coverage=1.0-smoothstep(-max(fwidth(sd),.5),max(fwidth(sd),.5),sd);
    float ownPhi, ownDist;
    coordinates(sheet.holes[holeIndex],materialPoint,R,ownPhi,ownDist);
    float ownBoundary=sheet.holes[holeIndex].radius*(sheet.spherical!=0 ? R : 1.0)*
        (1.0+dot(sheet.holes[holeIndex].amplitude,cos(vec4(3,5,9,14)*ownPhi+sheet.holes[holeIndex].phase)));
    float ownWidth=peelWidth(ownBoundary,R);
    if (ownWidth<=0.0) discard;
    float ownRank=(ownDist-ownBoundary)/ownWidth;
    if (sheetMode!=0) {
        float delta=atan(sin(ownPhi-sector.x),cos(ownPhi-sector.x));
        float u=1.0-ownRank;
        float limit=sector.y*(.82+.14*sin(3.14159265359*u)+
            .04*sin(3.0*3.14159265359*u+sheet.holes[holeIndex].phase.x));
        if (u<0.0 || u>1.0 || abs(delta)>limit) discard;
    }
    for (int i=0;i<7;i++) {
        if (i>=maskCount) break;
        if (containsMask(masks[i],materialPoint,R)) discard;
    }
    for (int i=0;i<4;i++) {
        if (i>=sheet.holeCount) break;
        if (i==holeIndex) continue;
        Hole h=sheet.holes[i];
        float phi, dist;
        coordinates(h,materialPoint,R,phi,dist);
        float boundary=h.radius*(sheet.spherical!=0 ? R : 1.0)*(1.0+dot(h.amplitude,cos(vec4(3,5,9,14)*phi+h.phase)));
        float d=dist-boundary, aa=max(fwidth(d),.35);
        if (sheet.spherical!=0 && boundary>=3.14159265359*R) discard;
        float width=peelWidth(boundary,R);
        float rank=(dist-boundary)/max(width,.00001);
        if (h.radius>0.0 && width>0.0 && rank>=0.0 && rank<=1.0 &&
            (rank<ownRank || (rank==ownRank && i<holeIndex))) discard;
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
        float sdRoot=boxDistance(sourcePoint,sheet.halfSize,clamp(sheet.corner,0.0,R));
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
        imageColour*=sheet.content*(1.0-smoothstep(.1,.72,roll))*(sheetMode!=0 ? imageFade : 1.0);
    } else if (wallpaperReady!=0 && facing>0.0) {
        vec2 unit=(sourcePoint+size*.5)/size;
        vec2 uv=wallpaperOrigin+wallpaperX*unit.x+wallpaperY*unit.y;
        imageColour=texture(wallpaperImage,clamp(uv,wallpaperPixel*.5,vec2(1)-wallpaperPixel*.5))*(.25*(1.0-clamp(facing,0.0,1.0))*sheet.content)*(sheetMode!=0 ? imageFade : 1.0);
    }
    float attenuation=sheet.spherical!=0 && facing<0.0 ? .45 : 1.0;
    float alpha=(lightAlpha*imageMask+imageColour.a*(1.0-lightAlpha))*coverage*sheet.visibility*attenuation;
    vec3 rgb=(light*imageMask+imageColour.rgb*(1.0-lightAlpha))*coverage*sheet.visibility*attenuation;
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
