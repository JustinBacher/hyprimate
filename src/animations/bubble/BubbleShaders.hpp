#pragma once
constexpr const char *VERTEX = R"GLSL(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr const char *FRAGMENT = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D image, wallpaperImage, filmLut;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform int wallpaperReady;
uniform vec2 sourcePos, sourceSize, textureOrigin, imageSize, outputOrigin, size;
struct Hole { vec3 centre; float radius, rim, precursor; vec4 amplitude, phase; };
struct Membrane {
    vec2 halfSize, core;
    float corner, depth, content, film, visibility, ripple, thickness, flow, aureole;
    int spherical, holeCount;
    Hole holes[4];
};
uniform Membrane windowSurface, shellSurface;
out vec4 colour;
vec4 over(vec4 a, vec4 b) { return a + b * (1.0 - a.a); }
float luminance(vec3 c) { return dot(c, vec3(.2126,.7152,.0722)); }
float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - h + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float noise(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
    vec4 h = vec4(dot(i,vec3(1,57,113)),dot(i+vec3(1,0,0),vec3(1,57,113)),
                  dot(i+vec3(0,1,0),vec3(1,57,113)),dot(i+vec3(1,1,0),vec3(1,57,113)));
    vec4 a = fract(sin(h)*43758.5453), b = fract(sin(h+113.0)*43758.5453);
    return mix(mix(mix(a.x,a.y,f.x),mix(a.z,a.w,f.x),f.y),
               mix(mix(b.x,b.y,f.x),mix(b.z,b.w,f.x),f.y),f.z);
}
// White studio seen in reflection (y down, z toward the viewer): dim ground, brighter sky, a mullioned window
// softbox at the upper left and a small fill light. All colour comes from interference in the film.
vec3 env(vec3 n) {
    vec3 r = reflect(vec3(0,0,-1), n);
    float sky = mix(.05,.5,smoothstep(-.3,.6,-r.y));
    vec2 q = r.xy / max(.25, abs(r.z)+.4);
    vec2 k = (q-vec2(-.36,-.40))/vec2(.17,.24);
    float window = 1.0-smoothstep(.78,1.0,max(abs(k.x),abs(k.y)));
    window *= mix(.2,1.0,min(smoothstep(.035,.09,abs(k.x)),smoothstep(.035,.09,abs(k.y+.12))));
    float fill = exp(-dot((q-vec2(.42,-.16))*7.0,(q-vec2(.42,-.16))*7.0));
    return vec3(sky + 4.0*window + 1.2*fill);
}
vec4 layer(Membrane s, vec2 p, vec3 point, vec3 normal, float cosView, float curve, float cap, bool back, bool background) {
    float minHalf = max(min(s.halfSize.x,s.halfSize.y),1.0);
    float edge = 1e6, band = 0.0, bandWeight = 0.0, precursor = 0.0, blackFilm = 0.0;
    float domain=1.0;
    vec2 radial = vec2(0);
    for (int i=0;i<4;i++) {
        if (i >= s.holeCount) break;
        Hole h = s.holes[i];
        vec2 delta = p-h.centre.xy;
        float dist, phi;
        if (s.spherical != 0) {
            vec3 axis = abs(h.centre.z)<.9 ? vec3(0,0,1) : vec3(1,0,0);
            vec3 e1 = normalize(cross(axis,h.centre)), e2 = cross(h.centre,e1);
            phi = atan(dot(point,e2),dot(point,e1));
            dist = acos(clamp(dot(point,h.centre),-1.0,1.0))*minHalf;
            delta = p-h.centre.xy*minHalf;
        } else { dist=length(delta); phi=atan(delta.y,delta.x); }
        float radius = h.radius * (s.spherical != 0 ? minHalf : 1.0);
        float boundary = radius * (1.0 + dot(h.amplitude,cos(vec4(3,5,9,14)*phi+h.phase)));
        if (s.spherical!=0 && boundary>=3.14159265359*minHalf) domain=0.0;
        float d = dist-boundary;
        float spot = exp(-pow(dist/(minHalf*(s.spherical != 0 ? .12 : .1)),2.0));
        spot *= step(.002,spot);
        precursor = max(precursor,h.precursor*spot);
        // The black film: the very centre turns see-through in the last moments before the opening.
        blackFilm = max(blackFilm,smoothstep(.75,1.0,h.precursor)*exp(-pow(dist/(minHalf*.025+2.0),2.0)));
        if (radius > 0.0 && d < edge) {
            edge=d; radial=normalize(delta+vec2(1e-5));
            float w=max(radius*.25,1.0);
            float u=clamp(1.0-max(d,0.0)/w,0.0,1.0);
            bandWeight = d>=0.0 ? s.aureole*u*u : 0.0;
            band = d>=0.0 ? .3*s.aureole*w*u*u*u : 0.0;
        }
    }
    float aa=max(fwidth(edge),.35);
    float intact=domain*smoothstep(-aa,aa,edge);
    // Aureole: film material ahead of the rim is drawn toward it (compressed) and flaps.
    vec2 shifted=p+radial*(band + 1.8*bandWeight*sin(edge*.3-s.flow*12.0));
    float local=max(s.film,max(precursor,bandWeight*.8));
    vec4 reflection=vec4(0);
    // The thickness field is only evaluated where film is visible: a closing window needs it near its holes only.
    if (local>0.0 && intact>0.0) {
        vec3 warpedPoint=point;
        warpedPoint.xy += (shifted-p)/max(s.halfSize,vec2(1.0));
        vec3 q=warpedPoint*2.6+vec3(0,s.flow*.12,0);
        float turn=s.flow*.15;
        q.xy=mat2(cos(turn),-sin(turn),sin(turn),cos(turn))*q.xy;
        vec3 swirl=vec3(noise(q*.8),noise(q*.8+vec3(5.2,1.3,2.7)),0.0)*2.0-1.0;
        vec3 warp=q+swirl*1.1;
        float marble=(noise(warp)*.65+noise(warp*2.1+3.1)*.35)*2.0-1.0;
        float thick=s.thickness * exp(.65*marble) * clamp(1.0+.28*warpedPoint.y,.55,1.45);
        thick *= 1.0-.97*precursor;
        thick *= 1.0 + .5*bandWeight;
        // Near the rim the thickened film curls toward it: its tilted surface catches the light as a moving sheen.
        vec3 curled=normalize(normal-vec3(radial,0.0)*(.7*bandWeight*bandWeight));
        float curledView=clamp(curled.z,0.0,1.0);
        vec3 R=texture(filmLut,vec2(clamp(thick/1500.0,0.0,1.0),curledView)).rgb;
        vec3 reflectedNormal=back ? vec3(-curled.xy,curled.z) : curled;
        vec3 illumination=env(reflectedNormal)+vec3(precursor);
        reflection=vec4(R*illumination*(back ? .45 : 1.0), luminance(R))*local*intact;
    }
    vec4 imagery=vec4(0);
    if (!back) {
        vec2 corePoint=p/max(s.halfSize,vec2(1.0));
        vec2 b=max(vec2(0),vec2(1)-corePoint*corePoint);
        vec2 supportGradient=-4.0*corePoint*vec2(b.x*b.y*b.y,b.y*b.x*b.x);
        vec2 coreNormal=-(supportGradient*(s.core.x+s.core.y*dot(corePoint,corePoint)) +
                          b.x*b.x*b.y*b.y*s.core.y*2.0*corePoint)/max(s.halfSize,vec2(1.0));
        vec2 unit=background ? (shifted+size*.5)/size :
            .5+.5*(shifted/max(s.halfSize,vec2(1.0)))*(1.0-.12*curve*(1.0-cap))-coreNormal*.15;
        if (background) {
            if (wallpaperReady!=0) {
                vec2 offset=point.xy*(1.0-cosView)*.015*minHalf/size;
                vec2 uv=wallpaperOrigin+wallpaperX*(unit.x+offset.x)+wallpaperY*(unit.y+offset.y);
                vec4 wall=texture(wallpaperImage,clamp(uv,wallpaperPixel*.5,vec2(1)-wallpaperPixel*.5));
                imagery=wall*(.25*(1.0-cosView)*s.content);
            }
        } else {
            vec2 uv=(sourcePos+sourceSize*unit-textureOrigin)/imageSize;
            vec2 lo=max((sourcePos-textureOrigin+.5)/imageSize,vec2(.5)/imageSize);
            vec2 hi=min((sourcePos+sourceSize-textureOrigin-.5)/imageSize,vec2(1)-.5/imageSize);
            imagery=texture(image,clamp(uv,lo,hi))*s.content*(1.0-blackFilm);
        }
    }
    vec4 result=over(reflection,imagery*intact);
    return result;
}
vec4 membrane(Membrane s, vec2 logical, bool background) {
    if (s.visibility<=0.0) return vec4(0);
    vec2 p=logical-size*.5;
    vec2 base=(sourcePos+sourceSize*(logical/size)-textureOrigin)/imageSize;
    if (!background && s.holeCount==0 && s.film==0.0 && s.depth==0.0 &&
        all(equal(s.core,vec2(0))) && s.ripple==0.0 && s.content==1.0 && s.visibility==1.0) {
        if (any(lessThan(logical,vec2(0))) || any(greaterThanEqual(logical,size))) return vec4(0);
        return texture(image,clamp(base,max((sourcePos-textureOrigin+.5)/imageSize,vec2(.5)/imageSize),
                     min((sourcePos+sourceSize-textureOrigin-.5)/imageSize,vec2(1)-.5/imageSize)));
    }
    vec2 h=max(s.halfSize,vec2(.001));
    float corner=clamp(s.corner,0.0,min(h.x,h.y));
    float sd=boxDistance(p,h,corner);
    float aa=max(fwidth(sd),.5);
    float shape=1.0-smoothstep(-aa,aa,sd);
    vec2 xy=p/h;
    float z=sqrt(max(0.0,1.0-min(dot(xy,xy),1.0)));
    float curve=clamp(s.depth/min(h.x,h.y),0.0,1.0);
    // The window's dome follows its current rounded outline, so corners slope like edges instead of grazing.
    float radius=clamp(1.0+sd/min(h.x,h.y),0.0,1.0);
    float cap=sqrt(max(0.0,1.0-radius*radius));
    vec2 q=abs(p)-h+corner;
    vec2 gradient=max(q,0.0);
    gradient=dot(gradient,gradient)>1e-6 ? normalize(gradient)*sign(p) :
                                           (q.x>q.y ? vec2(sign(p.x),0.0) : vec2(0.0,sign(p.y)));
    vec2 b=max(vec2(0),vec2(1)-xy*xy);
    vec2 supportGradient=-4.0*xy*vec2(b.x*b.y*b.y,b.y*b.x*b.x);
    vec2 coreNormal=-(supportGradient*(s.core.x+s.core.y*dot(xy,xy)) +
                      b.x*b.x*b.y*b.y*s.core.y*2.0*xy)/h;
    vec3 normal=normalize(vec3(gradient*radius*curve/max(.22,cap+.22)+coreNormal,1.0));
    vec3 front=vec3(xy,max(z,.001)), back=vec3(xy,-max(z,.001));
    if (s.spherical!=0) { normal=normalize(front); cap=z; }
    float cv=clamp(normal.z,0.0,1.0);
    vec4 a=layer(s,p,front,normal,cv,curve,cap,false,background);
    if (s.spherical!=0) a=over(a,layer(s,p,back,normal,cv,curve,cap,true,background));
    return a*shape*s.visibility;
}
void main() {
    vec2 logical=(outputOrigin+gl_FragCoord.xy-sourcePos)*size/sourceSize;
    colour=over(membrane(shellSurface,logical,true),membrane(windowSurface,logical,false));
    colour.rgb=min(colour.rgb,vec3(1.0));
}
)GLSL";
constexpr const char *PARTICLE_VERTEX = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec4 geometry;
layout(location=1) in vec4 detail;
layout(location=2) in vec4 optical;
uniform vec2 canvas, origin, sourcePos, sourceSize, size;
out vec2 pixel, centre, tail;
out vec4 params;
out vec3 optics;
void main() {
    vec2 scale=sourceSize/size;
    centre=sourcePos+sourceSize*.5+geometry.xy*scale-origin;
    tail=sourcePos+sourceSize*.5+geometry.zw*scale-origin;
    float r=max(detail.x,detail.y)*max(scale.x,scale.y)+2.0+optical.z*2.0;
    vec2 lo=min(centre,tail)-r, hi=max(centre,tail)+r;
    vec2 corner=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    pixel=mix(lo,hi,corner);
    gl_Position=vec4(pixel/canvas*2.0-1.0,0,1);
    params=vec4(detail.xy*max(scale.x,scale.y),detail.zw);
    optics=optical.xyz;
}
)GLSL";
constexpr const char *PARTICLE_FRAGMENT = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D filmLut;
uniform int onGlass;
in vec2 pixel, centre, tail;
in vec4 params;
in vec3 optics;
out vec4 colour;
void main() {
    vec2 p=pixel-centre;
    if (onGlass!=0) p.y*=1.25;
    if (optics.x>0.0 && onGlass==0) {
        vec2 q=p/max(params.x,.001);
        float c=cos(optics.y), s=sin(optics.y);
        q=mat2(c,-s,s,c)*q;
        float angle=atan(q.y,q.x);
        float boundary=.72+.12*sin(3.0*angle+.7)+.08*sin(5.0*angle-1.1);
        float dist=length(q)-boundary;
        float coverage=1.0-smoothstep(-fwidth(dist),fwidth(dist),dist);
        vec3 n=normalize(vec3(.65*sin(q.x*4.0+optics.y),.5*cos(q.y*4.0-optics.y),1.0));
        vec3 R=texture(filmLut,vec2(clamp(optics.x/1500.0,0.0,1.0),abs(n.z))).rgb;
        float alpha=coverage*dot(R,vec3(.2126,.7152,.0722));
        colour=vec4(min(coverage*R*(1.2+1.8*max(0.0,dot(n,normalize(vec3(-.4,-.5,1))))),vec3(1)),alpha);
        return;
    }
    float r=max(params.x,.001), d=length(p)/r;
    vec2 v=tail-centre;
    float len2=dot(v,v);
    float t=clamp(dot(pixel-centre,v)/max(len2,.001),0.0,1.0);
    // A ligament leaves the drop as a neck thinner than the drop, then tapers toward its anchor.
    float lineRadius=mix(min(r*.55,params.y*1.6+.2),params.y,t);
    float lineDistance=length(pixel-centre-v*t);
    float aa=.5+optics.z*1.5;
    float dropCoverage=(1.0-smoothstep(r-aa,r+aa,length(p)))*min(1.0,2.0*r);
    float lineCoverage=(1.0-smoothstep(lineRadius-aa,lineRadius+aa,lineDistance))*
                       min(1.0,2.5*lineRadius)*step(.001,params.y)*step(.01,len2);
    // A clear drop: dark refraction ring, bright highlight toward the window light, faint focused caustic. Drops
    // resting on the glass spread into flatter lenses with a broader highlight and a brighter focused crescent.
    float spread=onGlass!=0 ? 2.4 : 3.0;
    float ringOffset=(d-.8)*6.0;
    float ring=exp(-ringOffset*ringOffset);
    float highlight=exp(-dot((p/r-vec2(-.3,-.36))*spread,(p/r-vec2(-.3,-.36))*spread));
    float caustic=exp(-dot((p/r-vec2(.32,.42))*4.0,(p/r-vec2(.32,.42))*4.0))*(onGlass!=0 ? 1.8 : 1.0);
    float tiny=1.0-smoothstep(1.0,2.2,r);
    float alpha=dropCoverage*mix(.05+.32*ring+.9*highlight+.22*caustic,.62,tiny);
    vec3 rgb=vec3(dropCoverage*mix(.95*highlight+.3*caustic,.62,tiny));
    // Threads glint most where they lie across the light's direction.
    vec2 direction=len2>.01 ? v*inversesqrt(len2) : vec2(1,0);
    vec2 light=normalize(vec2(-.5,-.62));
    float glint=.3+.7*abs(direction.x*light.y-direction.y*light.x);
    float thread=lineCoverage*(.26+.34*glint);
    alpha=max(alpha,thread);
    rgb=max(rgb,vec3(thread*(.1+.9*glint)));
    rgb*=params.z<0.0 ? .75 : 1.0;
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
)GLSL";
