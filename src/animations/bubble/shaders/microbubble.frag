#version 300 es
precision highp float;
// A small soap bubble: a clear, thin-walled sphere. Its colour is interference in the wall, which
// drains toward the bottom and reflects most at grazing angles, so the rim carries the colour and
// the middle stays see-through. The studio window shows as a highlight on the front wall and,
// fainter and inverted, on the back. A forming bubble is a swelling lens in the parent film; a neck
// of film joins an attached one to it; a collapsing one tears open from a thin spot.
uniform sampler2D filmLut;
in vec2 pixel, centre, anchor;
in vec4 params;
in vec2 filmData;
out vec4 colour;
#include "soap.glsl"
void main() {
    float r=max(params.x,.5), growth=clamp(params.y,0.0,1.0), attach=params.z, collapse=params.w;
    float seed=filmData.y;
    vec2 p=(pixel-centre)/r;
    float d=length(p);
    float aa=1.2/r;
    float body=1.0-smoothstep(1.0-aa,1.0+aa,d);
    // Rupture: a hole opens at a thin spot and its rim sweeps the film away.
    vec2 spot=.55*vec2(cos(seed*6.2832),sin(seed*6.2832));
    float hole=2.4*collapse, toHole=length(p-spot);
    float open=collapse>0.0 ? smoothstep(hole-aa,hole+aa,toHole) : 1.0;
    float tear=collapse>0.0 ? exp(-pow((toHole-hole)/(1.5/r+.04),2.0))*(1.0-collapse)*body : 0.0;
    float z=sqrt(max(0.0,1.0-d*d));
    float swirl=noise(vec3(p*2.4,seed*13.0))*2.0-1.0;
    float thick=filmData.x*(.5+.9*clamp(.5+.5*p.y,0.0,1.0))*exp(.45*swirl);
    vec3 R=texture(filmLut,vec2(clamp(thick/1500.0,0.0,1.0),max(z,.04))).rgb;
    float grazing=1.0+3.0*pow(1.0-z,3.0);
    vec2 key=p-vec2(-.36,-.42), back=p-vec2(.3,.38);
    float highlight=exp(-dot(key,key)*26.0)+.35*exp(-dot(back,back)*40.0);
    float outline=exp(-pow((d-.97)/(1.4/r+.025),2.0));
    float formed=smoothstep(0.0,1.0,growth);
    // A swelling lens shows its outline and highlight before the colour of a free wall.
    float alpha=(luminance(R)*grazing*1.4*formed+.8*highlight+.35*outline)*body*open*mix(.35,1.0,formed);
    vec3 rgb=(R*grazing*1.8*formed+vec3(.9*highlight+.4*outline))*body*open*mix(.35,1.0,formed);
    // The neck: a short meniscus of thicker film where the bubble meets its parent.
    float toAnchor=length(pixel-anchor)/r;
    float meniscus=attach*exp(-toAnchor*toAnchor*5.0)*smoothstep(.45,1.05,d)*(1.0-collapse);
    alpha=max(alpha,.28*meniscus)+.4*tear;
    rgb=max(rgb,vec3(.3*meniscus))+vec3(.5*tear);
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
