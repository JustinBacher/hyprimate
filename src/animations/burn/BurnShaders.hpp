#pragma once

#include <string>
#include <string_view>

namespace burn_shader {
    // Keep burnArrival identical to hyprimate::burn_arrival for deposits and debris.
    inline constexpr std::string_view COMMON = R"GLSL(
float burnArrival(vec2 uv, float seedPhase, float variation) {
    vec2 p=2.0*uv-1.0;
    float r=max(abs(p.x),abs(p.y));
    float angle=dot(p,p)>0.0 ? atan(p.y,p.x) : 0.0;
    float a=angle+0.19*sin(3.0*angle+seedPhase+1.3*r);
    float grain=0.5+0.19*sin(3.0*a+seedPhase)+0.14*sin(7.0*a-seedPhase)+
        0.10*sin(17.0*a+2.0*seedPhase+4.0*r)+0.05*sin(41.0*a+seedPhase+6.0*r)+
        0.02*sin(89.0*a-seedPhase);
    float v=clamp(variation,0.0,1.0);
    float radius=mix(r,length(p)*0.70710678,0.82*v);
    float site=0.5+0.5*cos(5.0*angle+seedPhase+0.7*sin(3.0*angle-seedPhase));
    float site2=site*site,site4=site2*site2;
    return mix(radius*(1.0-0.43*v*grain),r,smoothstep(0.65,1.0,r)*site4*site4);
}
float burnHash(vec2 cell) {
    vec3 p=fract(vec3(cell.xyx)*vec3(0.1031,0.1030,0.0973));
    p+=dot(p,p.yzx+33.33);
    return fract((p.x+p.y)*p.z);
}
// Hash only lattice corners; interpolation keeps ash and pores spatially coherent.
float burnGrain(vec2 point) {
    vec2 cell=floor(point);
    vec2 fraction=fract(point);
    fraction=fraction*fraction*(3.0-2.0*fraction);
    return mix(mix(burnHash(cell),burnHash(cell+vec2(1.0,0.0)),fraction.x),
        mix(burnHash(cell+vec2(0.0,1.0)),burnHash(cell+vec2(1.0)),fraction.x),fraction.y);
}
float burnFbm(vec2 point) {
    const mat2 TURN=mat2(0.8,-0.6,0.6,0.8);
    float broad=burnGrain(point);
    point=TURN*point*2.03+vec2(17.3,9.1);
    float middle=burnGrain(point);
    point=TURN*point*2.07+vec2(4.7,21.2);
    return broad*0.57+middle*0.29+burnGrain(point)*0.14;
}
vec2 burnWarp(vec2 uv, float heat) {
    return uv+(vec2(burnGrain(uv*28.0),burnGrain(uv*28.0+vec2(23.7,8.3)))-0.5)*0.003*heat;
}
// Shared by wallpaper, retained window, and flakes: muted coal, not opaque white speckles.
vec3 charPalette(float amount, float heat) {
    vec3 coal=mix(vec3(0.12,0.061,0.031),vec3(0.019,0.017,0.015),clamp(amount,0.0,1.0));
    return coal+vec3(1.0,0.19,0.012)*heat+vec3(0.55,0.35,0.055)*heat*heat;
}
vec3 burnChar(vec3 content, float distance, float width, vec2 uv, float heat) {
    float mottle=burnFbm(uv*23.0);
    vec2 fibers=uv*vec2(9.0,112.0)+vec2(mottle*3.5,mottle*2.0);
    float fiber=burnGrain(fibers);
    float amount=1.0-smoothstep(0.0,width*2.6,distance);
    float glow=exp(-abs(distance)/max(width*0.20,0.0001))*heat*(0.45+0.8*mottle);
    float ink=dot(content,vec3(0.2126,0.7152,0.0722));
    vec3 carbon=charPalette(amount,glow)+vec3(ink*(0.34+0.13*mottle));
    carbon*=0.76+0.30*mottle+0.10*fiber;
    // Sparse broad ash islands retain the original dark ink, never independent bright pixels.
    float ash=smoothstep(0.57,0.78,mottle)*(0.35+0.65*fiber)*(1.0-clamp(glow,0.0,1.0));
    carbon=mix(carbon,vec3(0.20+ink*0.45),ash*0.72);
    float crease=1.0-smoothstep(0.015,0.075,abs(fiber-0.48));
    carbon*=1.0-crease*0.15;
    return mix(content,carbon,amount)+vec3(0.22,0.033,0.002)*crease*glow;
}
)GLSL";

    inline std::string vertex() {
        return std::string{R"GLSL(#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec2 patchUV;
layout(location=3) in vec3 normal;
layout(location=4) in vec4 material;
uniform vec2 canvasSize;
uniform vec4 source;
uniform float threshold, seedPhase, variation, frontWidth, curl, alpha;
uniform int closing, topFirst;
out vec2 materialUV, localUV;
out vec3 surfacePosition, surfaceNormal;
out vec4 properties;
)GLSL"} + std::string{COMMON} +
               R"GLSL(
vec3 sheet(vec2 q) {
    vec3 p=vec3(source.xy+q*source.zw,0.0);
    if(closing==0 || threshold<=0.0 || threshold>=1.0) return p;
    float d=threshold-burnArrival(q,seedPhase,variation);
    float t=clamp(d/max(frontWidth,0.0001),0.0,1.0);
    vec2 radial=q-0.5;
    vec2 direction=length(radial)>0.0001 ? normalize(radial) : vec2(1.0,0.0);
    float a=atan(direction.y,direction.x);
    float roll=(1.0-t)*3.14159265*curl*(0.78+0.17*sin(a*11.0+seedPhase));
    float radius=min(source.z,source.w)*frontWidth/3.14159265;
    p.xy+=direction*(sin(roll)-roll)*radius;
    p.z=(1.0-cos(roll))*radius;
    return p;
}
void main() {
    vec3 p=position;
    vec3 n=normal;
    if(material.x<0.5) {
        p=vec3(source.xy+uv*source.zw,0.0);
        if(closing!=0 && curl>0.0 && threshold>0.0 && threshold<1.0) {
            p=sheet(uv);
            float edge=threshold-burnArrival(uv,seedPhase,variation);
            if(abs(edge)<frontWidth*1.05) {
            float e=0.0002;
            n=normalize(cross(sheet(uv+vec2(e,0.0))-sheet(uv-vec2(e,0.0)),
                              sheet(uv+vec2(0.0,e))-sheet(uv-vec2(0.0,e))));
            }
        }
    }
    // A small local perspective lift, not a rigid card rotation.
    float eye=max(canvasSize.x,canvasSize.y)*3.0;
    vec2 pixel=source.xy+source.zw*0.5+(p.xy-source.xy-source.zw*0.5)*eye/(eye-p.z);
    vec2 clip=pixel/canvasSize*2.0-1.0;
    if(topFirst==0) clip.y=-clip.y;
    gl_Position=vec4(clip,-p.z/max(canvasSize.x,canvasSize.y),1.0);
    surfacePosition=p;
    surfaceNormal=n;
    materialUV=uv;
    localUV=patchUV;
    properties=vec4(material.xy,material.z*alpha,material.w);
}
)GLSL";
    }

    inline std::string fragment() {
        return std::string{R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D image;
uniform vec4 uvTransform, uvBounds;
uniform float threshold, seedPhase, variation, frontWidth;
uniform int closing;
in vec2 materialUV, localUV;
in vec3 surfacePosition, surfaceNormal;
in vec4 properties;
layout(location=0) out vec4 color;
)GLSL"} + std::string{COMMON} +
               R"GLSL(
void main() {
    float kind=properties.x;
    float heat=properties.y;
    float opacity=properties.z;
    if(kind>1.5) {
        float radius=length(localUV*2.0-1.0);
        float coverage=1.0-smoothstep(0.35,1.0,radius);
        vec3 rgb=kind>2.5 ? charPalette(1.0,heat*1.7) : vec3(0.15+properties.w*0.54);
        float a=coverage*opacity;
        color=vec4(rgb*a,a);
        return;
    }
    float arrival=burnArrival(materialUV,seedPhase,variation);
    float distance=threshold-arrival;
    if(kind<0.5 && (threshold<=0.0 || (threshold<1.0 && distance<0.0))) discard;
    float coverage=1.0;
    float exposed=0.0;
    if(kind>0.5) {
        // Intersect a torn patch with an oblique fracture in material space.
        // Its exposed outline keeps receding as the detached sheet oxidizes.
        vec2 p=localUV*2.0-1.0;
        float fiber=burnGrain(materialUV*157.0);
        float footprint=max(max(abs(p.x),abs(p.y)),abs(p.x*0.68+p.y*0.76+0.15));
        exposed=0.98-0.51*properties.w-(footprint+0.22*(fiber-0.5));
        coverage=smoothstep(-0.055,0.035,exposed);
        if(coverage<=0.0) discard;
    }
    float heated=kind>0.5 ? heat :
        (closing!=0 && threshold<1.0 ? heat*(1.0-smoothstep(frontWidth*0.15,frontWidth*1.2,distance)) : 0.0);
    vec2 sampleUV=heated>0.0 ? burnWarp(materialUV,heated) : materialUV;
    vec4 texel=texture(image,clamp(uvTransform.xy+sampleUV*uvTransform.zw,uvBounds.xy,uvBounds.zw));
    // Captures are premultiplied. Material arithmetic operates on straight RGB.
    vec3 content=texel.a>0.0001 ? texel.rgb/texel.a : vec3(0.0);
    vec3 n=normalize(surfaceNormal);
    float lighting=0.55+0.45*max(0.0,dot(n,normalize(vec3(-0.35,-0.45,1.0))));
    bool burning=(closing!=0 && threshold<1.0) || kind>0.5;
    if(burning && (kind>0.5 || distance<frontWidth*2.6)) {
        float d=kind>0.5 ? frontWidth*clamp(exposed,0.0,0.8) : distance;
        content=burnChar(content,d,frontWidth,materialUV,heat);
        // The reverse face is fibrous carbon, not a mirrored bright window.
        if(n.z<0.0) content=mix(charPalette(0.8,heat*0.15),content,0.16);
        content*=mix(1.0,lighting,kind>0.5 ? 1.0 : 1.0-smoothstep(0.0,frontWidth*2.6,d));
    }
    if(kind<0.5 && closing!=0 && threshold<1.0) {
        // Small separated bites at the hot front produce brittle breakup rather
        // than an opacity wipe. Detached pieces retain their own image footprint.
        float brittle=burnGrain(materialUV*170.0);
        if(distance<frontWidth*0.20 && brittle>0.72+0.28*distance/(frontWidth*0.20)) discard;
    }
    float a=texel.a*opacity*coverage;
    if(a<0.001) discard;
    color=vec4(content*a,a);
}
)GLSL";
    }
}  // namespace burn_shader
