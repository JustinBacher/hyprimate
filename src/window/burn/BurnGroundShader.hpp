#pragma once

#include <string>
#include <string_view>

#include "BurnShaders.hpp"

namespace burn_ground_shader {
    constexpr const char *VERTEX = R"GLSL(#version 300 es
precision highp float;
out vec2 uv;
uniform vec4 viewUV;
void main() {
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    uv=viewUV.xy+p*viewUV.zw;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)GLSL";
    // Shared by the attached wallpaper and detached flakes. Keep the print's contrast and tint
    // as the paper carbonizes; ash lightens that same print rather than painting over it.
    inline constexpr std::string_view PRINTED_CHAR = R"GLSL(
vec3 burnPrintedChar(vec3 content, float depth, float ash) {
    float ink=dot(content,vec3(0.2126,0.7152,0.0722));
    vec3 print=mix(vec3(ink),content,0.36);
    vec3 toasted=content*vec3(0.91,0.63,0.40)+vec3(0.079,0.037,0.009);
    vec3 carbon=vec3(0.032,0.020,0.015)+print*vec3(0.42,0.36,0.32);
    vec3 pale=vec3(0.22,0.208,0.18)+0.36*print;
    return mix(mix(content,toasted,smoothstep(0.0,0.43,depth)),
               mix(carbon,pale,ash),smoothstep(0.30,0.96,depth));
}
)GLSL";

    inline std::string fragment() {
        constexpr std::string_view HEADER = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D wallpaper;
uniform sampler2D field;
uniform vec2 origin;
uniform vec2 axisX;
uniform vec2 axisY;
uniform vec4 logicalScreen;
uniform int openingCount;
uniform vec4 openingRect[32];
uniform vec4 openingState[32];
in vec2 uv;
out vec4 color;
)GLSL";
        constexpr std::string_view BODY = R"GLSL(
// Movement uses the same coherent lattice as burnGrain, plus its analytic slope. This
// keeps relief local to the material without extra field taps or screen-space derivatives.
vec3 movementGrain(vec2 point) {
    vec2 cell=floor(point);
    vec2 f=fract(point);
    vec2 t=f*f*(3.0-2.0*f);
    float a=burnHash(cell);
    float b=burnHash(cell+vec2(1.0,0.0));
    float c=burnHash(cell+vec2(0.0,1.0));
    float d=burnHash(cell+vec2(1.0));
    vec2 slope=mix(vec2(b-a,c-a),vec2(d-c,d-b),t.yx)*6.0*f*(1.0-f);
    return vec3(mix(mix(a,b,t.x),mix(c,d,t.x),t.y),slope);
}

vec3 wallPrint(vec2 point) {
    return texture(wallpaper,origin+axisX*point.x+axisY*point.y).rgb;
}

vec4 movementSurface(vec3 clean, vec2 paper, vec2 state) {
    const mat2 TURN=mat2(0.8,-0.6,0.6,0.8);
    vec3 stock=movementGrain(paper*0.0105+vec2(9.3,18.7));
    vec3 broad=movementGrain(paper*0.032);
    vec2 angled=TURN*paper;
    vec3 rag=movementGrain(angled*0.131+vec2(18.6,7.1));
    float mottle=0.45*stock.x+0.37*broad.x+0.18*rag.x;
    float singe=state.x-(0.02+0.14*mottle);
    float opacity=smoothstep(0.0,0.29,singe);
    if(opacity<=0.0) return vec4(0.0);

    // Damage moves the cut through a fixed pattern: as the field heals, the same holes
    // contract in place. Its slope gives a local light direction and a logical-pixel lip.
    float bite=state.x-(0.583+0.76*mottle);
    vec2 slope=-0.76*(0.45*0.0105*stock.yz+0.37*0.032*broad.yz+0.18*0.131*(rag.yz*TURN));
    float rim=bite/max(length(slope),0.012);
    vec2 intoHole=slope*inversesqrt(max(dot(slope,slope),0.000025));
    float facing=dot(intoHole,vec2(-0.66,-0.75));
    float hole=smoothstep(-0.55,0.7,rim);
    float curl=smoothstep(-8.0,-3.2,rim)*(1.0-smoothstep(-2.3,-0.3,rim));
    float edge=1.0-smoothstep(0.1,1.6,abs(rim+0.45));

    float fiber=burnGrain(angled*vec2(0.31,0.067)+vec2(13.5,26.8));
    float depth=smoothstep(0.06,0.85,state.x-0.42*stock.x-0.18*broad.x+0.15*(0.5-rag.x));
    // The cut and black crust heal first. Keep a thin stain in the actual
    // print until the local regrowth front has passed, even on resistant stock.
    depth=max(depth,0.41*smoothstep(0.0,0.24,singe));
    float ash=smoothstep(0.58,0.84,broad.x)*smoothstep(0.36,0.73,fiber)*depth;
    float seam=abs(fiber-0.48);
    float branch=abs(rag.x-0.49)+0.3*seam;
    float fracture=(1.0-smoothstep(0.012,0.063,min(seam,branch)))*smoothstep(0.28,0.66,broad.x);
    vec3 print=clean;
    if(curl>0.0) {
        // The ground capture includes padding for this sub-three-logical-pixel offset.
        vec2 folded=clamp(uv-2.8*curl*intoHole/logicalScreen.zw,vec2(0.0),vec2(1.0));
        print=textureLod(wallpaper,origin+axisX*folded.x+axisY*folded.y,0.0).rgb;
    }
    vec3 sheet=burnPrintedChar(print,depth,ash);
    sheet*=1.0+curl*(0.12+0.29*facing)-0.23*edge-fracture*0.46*depth;
    sheet+=vec3(0.105,0.073,0.046)*curl*max(facing,0.0);
    sheet+=vec3(0.21,0.20,0.17)*edge*smoothstep(0.42,0.72,fiber);

    // Holes expose a procedural matte, never another scene sample. The nearer paper edge
    // occludes it, with a longer shadow on the side opposite the light.
    vec3 backing=vec3(0.164,0.153,0.140)+0.027*stock.x+0.014*(fiber-0.5);
    float shadow=(1.0-smoothstep(0.0,3.2+1.8*facing,rim))*(0.42+0.13*facing);
    backing*=1.0-shadow;

    float outerEdge=1.0-smoothstep(0.015,0.055,abs(singe-0.12));
    float glow=smoothstep(0.63,0.85,fiber)*smoothstep(0.20,0.85,state.y);
    vec3 surface=mix(sheet,backing,hole)+vec3(0.53,0.15,0.025)*glow*max(edge,0.22*outerEdge);
    return vec4(surface*opacity,opacity);
}

void main() {
    vec4 coarse=texture(field,uv);
    if(coarse.r<=0.0 && coarse.b<=0.02 && openingCount==0) discard;
    vec2 paper=logicalScreen.xy+uv*logicalScreen.zw;
    float severity=0.0;
    float heat=0.0;
    if(coarse.r>0.0) {
        vec2 warp=vec2(burnGrain(paper*0.038),
            burnGrain(paper*0.038+vec2(29.4,13.8)))*2.0-1.0;
        vec2 state=texture(field,uv+warp*12.0/logicalScreen.zw).rg;
        float mottle=burnFbm(paper*0.019);
        severity=smoothstep(0.10+0.19*(1.0-mottle),0.90,state.r);
        heat=state.g*(0.25+1.25*mottle);
    }
    float opacity=severity;
    float frontDistance=0.0, frontWidth=0.055;
    vec2 frontUV=vec2(0.0);
    bool onFront=false;
    for(int index=0;index<openingCount;++index) {
        vec4 rect=openingRect[index];
        vec4 state=openingState[index];
        vec2 local=(uv-rect.xy)/rect.zw;
        if(any(lessThan(local,vec2(0.0))) || any(greaterThan(local,vec2(1.0)))) continue;
        float distance=burnArrival(local,state.y,state.w)-state.x;
        float width=max(state.z,0.0001);
        if(distance<0.0 || distance>width*2.6) continue;
        float rim=1.0-smoothstep(width*0.25,width*2.6,distance);
        if(rim>opacity) {
            opacity=rim;
            frontDistance=distance;
            frontWidth=width;
            frontUV=local;
            onFront=true;
        }
    }
    if(opacity<=0.0 && coarse.b<=0.02) discard;
    vec3 clean=wallPrint(uv);
    vec4 legacy=vec4(0.0);
    if(opacity>0.0) {
        vec3 burned;
        if(onFront) {
            burned=burnChar(clean,frontDistance,frontWidth,frontUV,1.0);
        } else {
            float luminance=dot(clean,vec3(0.2126,0.7152,0.0722));
            vec3 desaturated=mix(clean,vec3(luminance),severity*0.85);
            burned=burnChar(desaturated,(1.0-severity)*0.055*2.6,0.055,paper/800.0,heat);
        }
        legacy=vec4(burned*opacity,opacity);
    }
    if(coarse.b<=0.02) {
        color=legacy;
        return;
    }
    // At the limit of the sweep, leave ragged tongues of surviving print, rather
    // than exposing the straight silhouette of a linearly-filtered grid cell.
    vec2 state=coarse.ba;
    if(state.x<0.82) {
        vec2 tear=vec2(burnGrain(paper*0.044),burnGrain(paper*0.044+vec2(13.7,29.1)))-0.5;
        float fray=1.0-smoothstep(0.48,0.82,state.x);
        vec2 notch=texture(field,uv+tear*27.0/logicalScreen.zw).ba;
        state=mix(state,min(state,notch),0.78*fray);
    }
    vec4 trail=movementSurface(clean,paper,state);
    if(trail.a<=0.0 && opacity<=0.0) discard;
    // The live opening rim remains foremost, while movement holes cover old closing scorch.
    color=onFront ? legacy+trail*(1.0-legacy.a) : trail+legacy*(1.0-trail.a);
}
)GLSL";
        std::string source;
        source.reserve(
            HEADER.size() + burn_shader::COMMON.size() + PRINTED_CHAR.size() + BODY.size());
        source.append(HEADER).append(burn_shader::COMMON).append(PRINTED_CHAR).append(BODY);
        return source;
    }
}  // namespace burn_ground_shader
