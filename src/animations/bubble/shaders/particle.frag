#version 300 es
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
