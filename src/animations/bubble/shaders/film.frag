#version 300 es
precision highp float;
// Soap film the moving bubble left behind. Its colour is thin-film interference at the local
// thickness: streaked along the path, thinning as it drains, black film where it is about to break.
// Holes open in the thinnest film and their rims retract; the frayed edge pulls in as it ages.
uniform sampler2D filmLut;
uniform float seed;
uniform vec2 arcRange;  // arc length of the oldest and the newest point
in vec2 film;
in float fade, density, halfWidth;
out vec4 colour;
#include "soap.glsl"
void main() {
    float arc=film.x, across=film.y, spread=abs(across);
    vec2 point=vec2(arc, across*halfWidth);  // px along and across the path
    float side=across<0.0 ? 0.0 : 7.3;
    float fray=noise(vec3(arc*.016, side+seed*11.0, fade*1.2))*.7+
               noise(vec3(arc*.05, side+seed*11.0+3.7, fade*2.0))*.3;
    // Both ends round off, so the ribbon never ends in a straight cut.
    float head=min(arc-arcRange.x,arcRange.y-arc)/max(halfWidth,1.0);
    float edge=(.62+.38*fray)*(1.0-.4*fade)*sqrt(clamp(head*1.4,0.0,1.0));
    float aa=max(fwidth(spread),.002)*1.5;
    float cover=1.0-smoothstep(edge-aa-.12,edge+aa,spread);
    // Breakup: the film tears where it is thinnest, and holes widen as it drains.
    float thin=noise(vec3(point*.022,seed*5.0))*.65+noise(vec3(point*.051+4.0,seed*5.0))*.35;
    float threshold=1.25*fade-.3;
    float intact=smoothstep(threshold,threshold+.05,thin);
    float rim=exp(-pow((thin-threshold-.025)/.02,2.0))*step(0.0,threshold);
    // Thickness streaks along the path; a few lenses of gathered film stay thicker for longer.
    float marble=noise(vec3(arc*.011,point.y*.035,seed*3.0))*2.0-1.0;
    float lens=smoothstep(.8,.95,noise(vec3(point*.04+9.0,seed*7.0)));
    float thick=mix(700.0,90.0,fade)*exp(.5*marble)*(1.0+.8*lens);
    vec3 R=texture(filmLut,vec2(clamp(thick/1500.0,0.0,1.0),.9)).rgb;
    float strength=density*cover*(1.0-fade*fade);
    // The film's own edge is a thicker, brighter border, as on any soap film.
    float border=exp(-pow((spread-edge)/.05,2.0))*intact;
    float alpha=(luminance(R)*1.3*intact+.18*rim+.16*border)*strength;
    vec3 rgb=(R*1.5*intact+vec3(.22*rim+.2*border))*strength;
    colour=vec4(min(rgb,vec3(1.0)),clamp(alpha,0.0,1.0));
}
