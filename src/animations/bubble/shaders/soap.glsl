// Helpers every soap-film fragment program shares.
float luminance(vec3 c) { return dot(c, vec3(.2126,.7152,.0722)); }
float noise(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
    vec4 h = vec4(dot(i,vec3(1,57,113)),dot(i+vec3(1,0,0),vec3(1,57,113)),
                  dot(i+vec3(0,1,0),vec3(1,57,113)),dot(i+vec3(1,1,0),vec3(1,57,113)));
    vec4 a = fract(sin(h)*43758.5453), b = fract(sin(h+113.0)*43758.5453);
    return mix(mix(mix(a.x,a.y,f.x),mix(a.z,a.w,f.x),f.y),
               mix(mix(b.x,b.y,f.x),mix(b.z,b.w,f.x),f.y),f.z);
}
// A Phong lobe widened by the variance of the normals one pixel covers (Toksvig), keeping its
// energy: a highlight smaller than a pixel spreads into a fainter, stable one instead of flickering.
float filteredLobe(float c, float exponent, float variance) {
    float k=exponent/(1.0+exponent*variance);
    return pow(max(c,0.0),k)*k/exponent;
}
// Broad, unstructured illumination: highlights, not invented reflected imagery. Both intact and
// peeling film use the same lighting and interference response; variance is the pixel's spread
// of normals (0 for large, smooth films).
vec3 soapLightFiltered(vec3 n, float variance) {
    vec3 r=reflect(vec3(0,0,-1),n);
    float sky=mix(.12,.65,smoothstep(-.4,.7,-r.y));
    float key=filteredLobe(dot(r,normalize(vec3(-.45,-.6,.65))),24.0,variance);
    float fill=filteredLobe(dot(r,normalize(vec3(.6,-.2,.75))),40.0,variance);
    return vec3(sky+1.8*key+.6*fill);
}
vec3 soapLight(vec3 n) { return soapLightFiltered(n,0.0); }
