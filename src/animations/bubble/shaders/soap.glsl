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
