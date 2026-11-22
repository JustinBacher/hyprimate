#version 300 es
precision highp float;
// A small soap bubble: a thin water film around air. It neither lenses nor tints what lies behind
// it; most light passes through both walls. What makes it visible is the light its walls reflect:
// the thin-film reflectance (Airy, from the shared lookup table) of the local film thickness at
// the local viewing angle, under the same broad illumination as the window's membrane. The rim
// is therefore not a stroke but where the walls are seen at grazing angles; the colour follows the
// drained, slowly swirling thickness field. Detail the bubble's projected size cannot resolve is
// filtered, not removed: unresolved fringes average to their mean reflectance, sub-pixel
// highlights widen into fainter ones, and the grazing rim is averaged over the pixel it covers.
uniform sampler2D filmLut;
in vec2 pixel, centre, anchor, squash;
in vec4 params;
in vec4 filmData;
out vec4 colour;
#include "soap.glsl"
// Reflectance of film `thick` nm thick at viewing cosine `c`, averaged over a relative thickness
// spread the pixel cannot resolve.
vec3 film(float thick, float c, float spread) {
    vec2 at=vec2(thick/1500.0,c);
    vec3 R=texture(filmLut,vec2(clamp(at.x,0.0,1.0),at.y)).rgb;
    if (spread<=.01) return R;
    vec3 lo=texture(filmLut,vec2(clamp(at.x*(1.0-spread),0.0,1.0),at.y)).rgb;
    vec3 hi=texture(filmLut,vec2(clamp(at.x*(1.0+spread),0.0,1.0),at.y)).rgb;
    return (lo+R+hi)/3.0;
}
// Thickness of the wall at unit-sphere point P: gravity drains it from the cap toward the bottom,
// and the film flow advects a marbled thickness pattern that is only as fine as can be resolved.
float thickness(vec3 P, float base, float seed, float flow, float detail) {
    vec3 q=P*mix(1.1,2.4,detail)+vec3(seed*17.0,seed*31.0,flow*.12);
    float turn=flow*.15;
    q.xy=mat2(cos(turn),-sin(turn),sin(turn),cos(turn))*q.xy;
    vec3 warp=q+(vec3(noise(q*.8),noise(q*.8+vec3(5.2,1.3,2.7)),0.0)*2.0-1.0)*1.1;
    float marble=(noise(warp)*.65+noise(warp*2.1+3.1)*.35)*2.0-1.0;
    // Screen y points down: the lower wall holds the drained liquid.
    return base*exp(.32*P.y+.55*detail*marble);
}
void main() {
    float r=max(params.x,.35), growth=clamp(params.y,0.0,1.0), attach=params.z, collapse=params.w;
    float base=filmData.x>0.0 ? filmData.x : 450.0, seed=filmData.y, flow=filmData.z;
    float visibility=clamp(filmData.w,0.0,1.0);
    vec2 q=pixel-centre;
    float phi=atan(q.y,q.x);
    // The outline flattens where neighbours press on it (an area-preserving quadrupole).
    float rim=r*(1.0+squash.x*cos(2.0*phi)+squash.y*sin(2.0*phi));
    vec2 p=q/max(rim,.35);
    float d=length(p);
    float aa=1.0/max(rim,.35);  // one pixel in units of the radius
    // Box-filtered coverage of the outline.
    float body=clamp((1.0-d)/aa+.5,0.0,1.0);
    // How much of the film's structure the projected size can resolve: none for a few-pixel
    // bubble, all of it by about 30 px radius. Continuous; the material itself never changes.
    float detail=smoothstep(3.0,30.0,r);
    float formed=smoothstep(0.0,1.0,growth);
    // A surfacing bubble is a shallow cap of the liquid before it is a free sphere.
    float lift=mix(.4,1.0,formed);
    float z=sqrt(max(0.0,1.0-d*d));
    // Average the steep grazing rim over the pixel it falls in rather than point-sampling it.
    float zf=min(1.0,sqrt(z*z+.5*aa));
    vec3 n=normalize(vec3(p,zf/lift));
    float c=clamp(n.z,0.0,1.0);
    // Normals turn by about aa/z per pixel; their spread widens the highlights.
    float variance=pow(aa/max(zf,.08),2.0);
    float spread=.35*(1.0-detail);
    vec3 front=vec3(p,z), back=vec3(p,-z);
    vec3 Rf=film(thickness(front,base,seed,flow,detail),c,spread);
    vec3 Rb=film(thickness(back,base,seed+.37,flow,detail),c,spread);
    // The walls reflect a room about as bright as what is seen through them; without the lift a
    // clear bubble would read as a grey disc.
    vec3 Lf=soapLightFiltered(n,variance)+.35;
    // The inside of the far wall is a concave mirror: its highlight appears opposite and fainter.
    vec3 Lb=(soapLightFiltered(vec3(-n.xy,n.z),variance)+.35)*.7;
    // Lossless walls: what one wall does not reflect it transmits.
    float Tf=1.0-luminance(Rf), Tb=1.0-luminance(Rb);
    vec3 rgb=Rf*Lf+Tf*Rb*Lb;
    float alpha=1.0-Tf*Tb;
    // Rupture: a hole opens at a thin spot and its rim sweeps the film away.
    if (collapse>0.0) {
        vec2 spot=.55*vec2(cos(seed*6.2832),sin(seed*6.2832));
        float hole=2.4*collapse, toHole=length(p-spot);
        float open=smoothstep(hole-aa,hole+aa,toHole);
        float band=exp(-pow((toHole-hole)/(1.5*aa+.04),2.0))*(1.0-collapse);
        // The retracting rim gathers the film: brief, thicker, coloured, not a white flash.
        vec3 Rr=film(base*2.5,1.0,.35);
        rgb=rgb*open+Rr*soapLight(vec3(0,0,1))*band*.6;
        alpha=alpha*open+luminance(Rr)*band*.6;
    }
    float shown=body*visibility*mix(.45,1.0,formed);
    rgb*=shown;
    alpha*=shown;
    // The neck: a tube of film joining an attached bubble to its parent. It flares into a meniscus
    // where it meets the parent film and narrows to a waist as the attachment weakens, until it
    // pinches off. Seen from the side it is clear along its middle and reflects at its flanks.
    if (attach>0.0) {
        vec2 axis=centre-anchor;
        float L=length(axis);
        vec2 u=L>1e-3 ? axis/L : vec2(0.0,-1.0);
        vec2 w=pixel-anchor;
        float t=dot(w,u);
        float across=length(w-u*clamp(t,0.0,L));
        float waist=r*mix(.06,.45,attach);
        float flare=waist+r*.45*attach*exp(-max(t,0.0)/(.3*r));
        float edge=across/max(flare,.5);
        float neck=clamp((1.0-edge)*flare+.5,0.0,1.0)*smoothstep(-1.0,.5,t)*(1.0-body)*
                   (1.0-collapse)*visibility;
        float graze=pow(clamp(edge,0.0,1.0),4.0);
        vec3 Rm=film(base*1.6,mix(.9,.3,graze),.35);
        float k=neck*(.10+.55*graze);
        rgb+=Rm*soapLight(vec3(0,0,1))*k*(1.0-alpha);
        alpha+=luminance(Rm)*k*(1.0-alpha);
    }
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
