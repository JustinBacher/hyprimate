#pragma once

#include <string_view>

namespace paper_shader {
    // Geometric-mean units, y down, eye=(0,0,3). Homogeneous projection preserves material
    // coordinates under perspective: projected xy = position.xy * 3 / (3-position.z).
    inline constexpr std::string_view VERTEX = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec3 normal;
uniform vec2 canvasSize, center, shapeScale;
uniform float pixelsPerUnit;
uniform int depthPass;
uniform mat3 lightBasis;
uniform vec4 lightBounds;
uniform vec2 lightRange;
out vec2 materialUV;
out vec3 surfaceNormal;
out vec3 surfacePosition;
void main() {
    vec3 p=vec3(position.xy*shapeScale,position.z);
    if(depthPass!=0) {
        vec2 lightXY=vec2(dot(lightBasis[0],p),dot(lightBasis[1],p));
        float depth=(lightRange.y-dot(lightBasis[2],p))/(lightRange.y-lightRange.x);
        gl_Position=vec4((lightXY-lightBounds.xy)/lightBounds.zw,2.0*depth-1.0,1.0);
    } else {
        float w=3.0-p.z;
        vec2 pixel=center*w+p.xy*(3.0*pixelsPerUnit);
        vec2 clip=vec2(2.0*pixel.x/canvasSize.x-w,w-2.0*pixel.y/canvasSize.y);
        // Near=.25 matches the publication's conservative projection bounds.
        float z=(30.25/29.75)*w-(15.0/29.75);
        gl_Position=vec4(clip,z,w);
    }
    materialUV=uv;
    surfaceNormal=vec3(normal.xy/shapeScale,normal.z);
    surfacePosition=p;
}
)GLSL";

    inline constexpr std::string_view FRAGMENT = R"GLSL(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
in vec2 materialUV;
in vec3 surfaceNormal;
in vec3 surfacePosition;
uniform sampler2D image, sceneDepth;
uniform vec4 uvTransform, uvBounds, lightBounds;
uniform vec2 shadowSize, lightRange;
uniform mat3 lightBasis;
uniform float opacity, shading, thickness;
uniform int depthPass;
out vec4 color;
float shadow(vec3 p,vec3 n) {
    vec2 lightXY=vec2(dot(lightBasis[0],p),dot(lightBasis[1],p));
    vec2 coord=(lightXY-lightBounds.xy)/lightBounds.zw*0.5+0.5;
    float receiver=dot(lightBasis[2],p);
    vec2 texelWorld=2.0*lightBounds.zw/shadowSize;
    // Receiver-plane depth correction handles grazing slopes without a large bias
    // that would detach the contact shadow. Both sides cast and receive shadows.
    float facing=dot(n,lightBasis[2]);
    vec2 slope=vec2(dot(n,lightBasis[0]),dot(n,lightBasis[1]))/max(abs(facing),0.08)*sign(facing);
    float result=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec2 offset=vec2(float(x),float(y));
        vec2 q=coord+offset/shadowSize;
        if(any(lessThan(q,vec2(0.0))) || any(greaterThan(q,vec2(1.0)))) continue;
        vec2 samplePixel=clamp(floor(q*shadowSize),vec2(0.0),shadowSize-1.0);
        float stored=texelFetch(sceneDepth,ivec2(samplePixel),0).r;
        float blocker=mix(lightRange.y,lightRange.x,stored);
        vec2 actualOffset=samplePixel+0.5-coord*shadowSize;
        float gap=blocker-receiver+dot(slope,actualOffset*texelWorld);
        float bias=max(thickness*0.45,0.00008);
        float coverage=smoothstep(bias,bias*2.5,gap);
        // Close sheets get an additional short-range contact shadow; distant folds
        // still cast a directional shadow instead of disappearing with screen depth.
        result+=coverage*(0.48+0.16*exp(-max(gap,0.0)/max(thickness*12.0,0.008)));
    }
    return result/9.0;
}
void main() {
    vec2 uv=clamp(uvTransform.xy+materialUV*uvTransform.zw,uvBounds.xy,uvBounds.zw);
    vec4 texel=texture(image,uv);
    // Holes neither occlude nor cast. Translucent material retains its own RGBA,
    // but cannot reveal a second copy of the window through the folded paper.
    if(texel.a<=0.001) discard;
    if(depthPass!=0) { color=vec4(0.0); return; }
    if(shading<=0.0) { color=texel*opacity; return; }
    vec3 n=normalize(surfaceNormal);
    vec3 view=normalize(vec3(0.0,0.0,3.0)-surfacePosition);
    if(dot(n,view)<0.0) n=-n;
    vec3 lightDirection=lightBasis[2];
    float diffuse=dot(n,lightDirection);
    // Flat endpoint is neutral; Lambert/Oren-Nayar-like matte response has no specular.
    float light=clamp(1.0+0.62*(diffuse-lightDirection.z),0.42,1.08);
    vec3 geometricNormal=normalize(cross(dFdx(surfacePosition),dFdy(surfacePosition)));
    float occlusion=shadow(surfacePosition,geometricNormal);
    float bent=smoothstep(0.001,0.15,length(n.xy));
    // Immutable material-space fibres, derivative filtered to avoid temporal sparkle
    // or moire in minified printed text. This perturbs roughness, never alpha or UV.
    vec2 grainUV=materialUV*vec2(2300.0,1700.0);
    float footprint=max(length(dFdx(grainUV)),length(dFdy(grainUV)));
    float grain=sin(grainUV.x+sin(grainUV.y*0.7))*sin(grainUV.y*1.13);
    float fibre=grain*0.012*(1.0-smoothstep(0.5,2.0,footprint))*bent;
    // The unresolved fibre ensemble still changes the matte diffuse lobe. Only
    // high-frequency variation disappears under minification, not the roughness.
    vec2 coarseUV=materialUV*vec2(95.0,140.0);
    float coarseFootprint=max(length(dFdx(coarseUV)),length(dFdy(coarseUV)));
    float coarseGrain=sin(coarseUV.x+0.5*sin(coarseUV.y))*sin(coarseUV.y*0.93);
    float roughness=0.72+0.1*coarseGrain*(1.0-smoothstep(0.5,2.0,coarseFootprint));
    light*=1.0-0.12*roughness*bent*(1.0-max(diffuse,0.0));
    float grazing=1.0-clamp(dot(n,view),0.0,1.0);
    float thicknessResponse=thickness/(thickness+0.004);
    float edge=0.18*grazing*grazing*bent*thicknessResponse;
    float shade=mix(1.0,light*(1.0-occlusion)*(1.0-edge)+fibre,clamp(shading,0.0,1.0));
    color=vec4(min(texel.rgb*shade,vec3(texel.a)),texel.a)*opacity;
}
)GLSL";
}  // namespace paper_shader
