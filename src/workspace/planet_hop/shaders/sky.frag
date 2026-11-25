#version 300 es
precision highp float;
precision highp int;
in vec2 vNDC;
uniform mat4 inverseViewProjection;
uniform vec3 cameraPosition;
uniform float endpointEnvelope;
uniform float starDensity;
uniform vec3 starOffsetNear;
uniform vec3 starOffsetMid;
uniform vec3 starOffsetFar;
out vec4 fragColor;
float hash31(vec3 p) {
    p = fract(p*0.1031);
    p += dot(p,p.yzx+33.33);
    return fract((p.x+p.y)*p.z);
}
vec3 stars(vec3 direction, vec3 offset, float scale, float seed) {
    // Cube-chart depth grids provide stable, bounded world-space parallax.
    vec3 n = direction;
    // Cube-face chart avoids stretched stars at spherical poles. Face identity
    // is included in the seed; a subpixel-sized seam cannot create huge streaks.
    vec3 a = abs(n);
    vec2 uv, shift;
    float face;
    if (a.x>=a.y && a.x>=a.z) { uv=n.yz/a.x; shift=offset.yz; face=n.x>0.0?1.0:2.0; }
    else if (a.y>=a.z) { uv=n.xz/a.y; shift=offset.xz; face=n.y>0.0?3.0:4.0; }
    else { uv=n.xy/a.z; shift=offset.xy; face=n.z>0.0?5.0:6.0; }
    vec2 p = uv*scale+shift;
    // Derivatives must precede the divergent sparse-star early return.
    float width = max(length(fwidth(p)),0.03);
    vec2 cell = mod(floor(p),1024.0);
    float h = hash31(vec3(cell,seed+face*17.0));
    float density = clamp(starDensity,0.0,1.0);
    if (h < 1.0-density*0.035) return vec3(0);
    vec2 centre = vec2(hash31(vec3(cell,seed+31.0)),hash31(vec3(cell,seed+73.0)));
    float radius = mix(0.045,0.13,h);
    float d = length(fract(p)-centre);
    float intensity = 1.0-smoothstep(max(0.0,radius-width),radius+width,d);
    intensity *= min(1.0,(radius*radius)/(width*width));
    vec3 tint = mix(vec3(0.53,0.7,1.0),vec3(1.0,0.83,0.61),hash31(vec3(cell,seed+91.0)));
    return tint*intensity*mix(0.4,1.0,h);
}
void main() {
    float envelope = clamp(endpointEnvelope,0.0,1.0);
    vec3 c = vec3(0.0015,0.002,0.005);
    if (starDensity <= 0.0 || envelope == 0.0) {
        fragColor = vec4(c*envelope,1.0);
        return;
    }
    vec4 farPoint = inverseViewProjection*vec4(vNDC,1.0,1.0);
    vec3 direction = normalize(farPoint.xyz/farPoint.w-cameraPosition);
    c += stars(direction,starOffsetNear,58.0,13.0);
    c += stars(direction,starOffsetMid,93.0,41.0)*0.7;
    c += stars(direction,starOffsetFar,147.0,79.0)*0.45;
    fragColor = vec4(c*envelope,1.0);
}
