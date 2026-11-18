#version 300 es
precision highp float;
// Soap film a moving bubble left on the screen, drying as real film does. The liquid lies thickest
// along the strand's middle and in a few lenses, and thins to nothing at its ragged border.
// Evaporation removes the same depth everywhere, so the border dries first and the drying front
// walks inward; the colour at each point is thin-film interference at the thickness left there, so
// the colour bands march inward with the front. The last tens of nanometres reflect almost nothing
// (black film) and dewet: holes open in the thinnest parts. Nothing fades out as a whole.
uniform sampler2D filmLut;
uniform float seed;
uniform vec2 arcRange;  // arc length over which this run still holds film
in vec2 film;
in float thickness, evaporated, halfWidth;
out vec4 colour;
#include "soap.glsl"
void main() {
    float arc=film.x, across=film.y, s=abs(across);
    vec2 point=vec2(arc,across*halfWidth);  // px along and across the path
    // The contact line wanders in and out along the strand.
    float rag=noise(vec3(arc*.045,sign(across)*4.0+seed*11.0,0.0))-.5;
    float r=s*(1.0+.35*rag);
    // A meniscus across, thinning into the free ends along.
    float end=clamp(min(arc-arcRange.x,arcRange.y-arc)/max(halfWidth,1.5),0.0,1.0);
    float profile=max(0.0,1.0-r*r)*sqrt(end);
    // Streaks along the path and lenses of gathered liquid.
    float streak=noise(vec3(arc*.012,point.y*.04,seed*3.0))*2.0-1.0;
    float lens=smoothstep(.62,.92,noise(vec3(point*.035+9.0,seed*7.0)));
    float h=thickness*profile*exp(.35*streak)*(1.0+.45*lens)-evaporated;
    float aa=max(fwidth(h),1.0);
    float wet=smoothstep(0.0,1.5*aa,h);
    // Dewetting: thin film breaks into holes where it is thinnest, their edges gathering liquid.
    float pits=noise(vec3(point*.11,seed*5.0))*.6+noise(vec3(point*.27+4.0,seed*5.0))*.4;
    float thin=1.0-smoothstep(25.0,140.0,h);
    float threshold=thin*1.1-.15;
    float intact=smoothstep(threshold-.03,threshold+.03,pits);
    float rim=exp(-pow((pits-threshold)/.035,2.0))*thin*wet;
    // Interference at the thickness left here. Where the thickness changes faster than a pixel
    // can resolve (the steep border of a narrow strand) the fringes average out instead of
    // aliasing into a hard rainbow outline.
    float spread=min(fwidth(h)*1.5,250.0);
    vec2 at=vec2(clamp(h/1500.0,0.0,1.0),.92);
    vec3 R=(texture(filmLut,at).rgb+texture(filmLut,vec2(clamp((h-spread)/1500.0,0.0,1.0),.92)).rgb+
            texture(filmLut,vec2(clamp((h+spread)/1500.0,0.0,1.0),.92)).rgb)/3.0;
    // The receding contact line holds a hair of extra liquid: a faint line at the drying front.
    float front=exp(-pow(h/(3.0*aa+6.0),2.0))*(1.0-.5*wet)*(1.0-smoothstep(.95,1.0,r))*step(.02,end);
    // A thread is a liquid cylinder: it also catches a highlight along its crest.
    float rod=(1.0-smoothstep(2.0,6.0,halfWidth))*exp(-s*s*4.0)*wet;
    float cover=wet*intact;
    vec3 rgb=R*1.9*cover+vec3(.10*rim+.04*front+.12*rod);
    float alpha=luminance(R)*1.3*cover+.08*rim+.03*front+.10*rod;
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
