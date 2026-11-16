#version 300 es
precision highp float;
uniform sampler2D image, wallpaperImage, filmLut;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform int wallpaperReady;
uniform vec2 sourcePos, sourceSize, textureOrigin, imageSize, outputOrigin, size;
#include "membrane.glsl"
uniform Membrane windowSurface, shellSurface;
out vec4 colour;
vec4 over(vec4 a, vec4 b) { return a + b * (1.0 - a.a); }
#include "soap.glsl"
float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - h + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
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
    float edge = 1e6, precursor = 0.0, blackFilm = 0.0;
    float domain=1.0;
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
        float width=max(0.0,min(64.0,min(.24*minHalf,.45*boundary)))*clamp(s.aureole,0.0,1.0);
        if (s.spherical!=0) width=min(width,max(0.0,3.14159265359*minHalf-boundary));
        float d = dist-boundary-width;
        float spot = exp(-pow(dist/(minHalf*(s.spherical != 0 ? .12 : .1)),2.0));
        spot *= step(.002,spot);
        precursor = max(precursor,h.precursor*spot);
        // The black film: the very centre turns see-through in the last moments before the opening.
        blackFilm = max(blackFilm,smoothstep(.75,1.0,h.precursor)*exp(-pow(dist/(minHalf*.025+2.0),2.0)));
        if (radius > 0.0) edge=min(edge,d);
    }
    float aa=max(fwidth(edge),.35);
    float intact=domain*smoothstep(-aa,aa,edge);
    // Movement stretches the film near the outline, most on the trailing side: there it thins and
    // shows interference colour while the window's middle stays clear.
    float stress=0.0;
    if (s.sheen>0.0 && s.spherical==0) {
        vec2 h=max(s.halfSize,vec2(1.0)), u=p/h, u2=u*u;
        float trailing=1.0, drag=length(s.drag);
        if (drag>1e-6) {
            vec2 dir=s.drag/drag;
            trailing=.35+.65*smoothstep(-1.0,1.0,dot(p,dir)/dot(abs(dir),h));
        }
        stress=s.sheen*smoothstep(.55,1.0,sqrt(sqrt(dot(u2,u2))))*trailing;
    }
    // A rounded-up window is a bubble: film over all of it, its imagery seen through the film.
    float rounded=s.spherical==0 ? smoothstep(0.0,.45,s.round) : 0.0;
    float local=max(max(max(s.film,precursor),stress),rounded);
    vec4 reflection=vec4(0);
    // The thickness field is only evaluated where film is visible: a closing window needs it near its holes only.
    if (local>0.0 && intact>0.0) {
        vec3 warpedPoint=point;
        vec3 q=warpedPoint*2.6+vec3(0,s.flow*.12,0);
        float turn=s.flow*.15;
        q.xy=mat2(cos(turn),-sin(turn),sin(turn),cos(turn))*q.xy;
        vec3 swirl=vec3(noise(q*.8),noise(q*.8+vec3(5.2,1.3,2.7)),0.0)*2.0-1.0;
        vec3 warp=q+swirl*1.1;
        float marble=(noise(warp)*.65+noise(warp*2.1+3.1)*.35)*2.0-1.0;
        float thick=s.thickness * exp(.65*marble) * clamp(1.0+.28*warpedPoint.y,.55,1.45);
        thick *= (1.0-.97*precursor)*(1.0-.45*stress);
        vec3 curled=normal;
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
        vec2 unit=background ? (p+size*.5)/size :
            .5+.5*(p/max(s.halfSize,vec2(1.0)))*(1.0-.12*curve*(1.0-cap))-coreNormal*.15;
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
            // A contracted window becomes part of the translucent soap film: its premultiplied
            // colour and opacity fade together while the dome lensing and reflections remain.
            imagery=texture(image,clamp(uv,lo,hi))*s.content*(1.0-blackFilm)*(1.0-.4*rounded);
        }
    }
    vec4 result=over(reflection,imagery*intact);
    return result;
}
vec4 membrane(Membrane s, vec2 logical, bool background) {
    if (s.visibility<=0.0) return vec4(0);
    vec2 p=logical-size*.5;
    vec2 base=(sourcePos+sourceSize*(logical/size)-textureOrigin)/imageSize;
    bool deformed=!background && moving(s);
    if (!background && !deformed && s.sheen==0.0 && s.holeCount==0 && s.film==0.0 && s.depth==0.0 &&
        all(equal(s.core,vec2(0))) && s.ripple==0.0 && s.content==1.0 && s.visibility==1.0) {
        if (any(lessThan(logical,vec2(0))) || any(greaterThanEqual(logical,size))) return vec4(0);
        return texture(image,clamp(base,max((sourcePos-textureOrigin+.5)/imageSize,vec2(.5)/imageSize),
                     min((sourcePos+sourceSize-textureOrigin-.5)/imageSize,vec2(1)-.5/imageSize)));
    }
    // Every material property below belongs to the undeformed film point drawn here. Far outside
    // the bubble the inverse has no solution; the point it settles on does not map back here.
    vec2 drawn=p;
    float valid=1.0;
    if (deformed) {
        p=motionSource(s,p);
        valid=step(length(motionPoint(s,p)-drawn),1.5);
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
    // The film leans the way movement carries it, so its reflections slide with the deformation.
    vec2 tilt=deformed ? motionOffset(s,roundPoint(s,p))/max(min(h.x,h.y),1.0)*1.5 : vec2(0);
    vec3 normal=normalize(vec3(gradient*radius*curve/max(.22,cap+.22)+coreNormal+tilt,1.0));
    // Contracted, the film bulges like a dome over its outline: its reflections, rim and lensing follow it.
    // Optical depth reaches full strength for a pillow too; geometry keeps its own roundness.
    float rounded=deformed ? smoothstep(0.0,.45,s.round) : 0.0;
    if (rounded>0.0) {
        vec2 ps=roundPoint(s,p)/max(s.extent,vec2(1.0));
        float zs=sqrt(max(0.0,1.0-min(dot(ps,ps),1.0)));
        normal=normalize(mix(normal,normalize(vec3(ps,max(zs,.05))),.85*rounded));
        cap=mix(cap,zs,rounded);
        curve=max(curve,rounded);
    }
    vec3 front=vec3(xy,max(z,.001)), back=vec3(xy,-max(z,.001));
    if (s.spherical!=0) { normal=normalize(front); cap=z; }
    float cv=clamp(normal.z,0.0,1.0);
    vec4 a=layer(s,p,front,normal,cv,curve,cap,false,background);
    if (s.spherical!=0) a=over(a,layer(s,p,back,normal,cv,curve,cap,true,background));
    return a*shape*s.visibility*valid;
}
void main() {
    vec2 logical=(outputOrigin+gl_FragCoord.xy-sourcePos)*size/sourceSize;
    colour=over(membrane(shellSurface,logical,true),membrane(windowSurface,logical,false));
    colour.rgb=min(colour.rgb,vec3(1.0));
}
