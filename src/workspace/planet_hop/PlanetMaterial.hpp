#pragma once

#include <string_view>

namespace hyprimate::workspace::planet_hop::shader {

// GLES 3.0, premultiplied output throughout. Compile once per GL context.
// Attributes: 0=flatPosition.xyz, 1=spherePosition.xyz, 2=texcoord.xy,
// 3=hemisphere (+1 front, -1 back; parametric fullSphere meshes only).
// Positions are planet-local (+x right, +y up, +z towards the viewer); UVs are
// top-left-origin. Matrices are column-major. No shader changes GL state.
//
// VERTEX uniforms: viewProjection(mat4), planetRotation(mat3), planetCenter(vec3),
// curvature, endpointEnvelope, elevation, depthStrength(float),
// uDepthEnabled(bool), depthSource(int: 0 procedural, 1 depthImage),
// samplingQuality(int: 0..2), depthImage(sampler2D, top-down decoded PNG rows).
// parametricMesh(bool, default false), aspect/radius/coverage(float) optionally
// derive positions from UV. fullSphere(bool) selects the production closed-body
// chart: a square-to-disk front hemisphere and matched back hemisphere flatten
// into the native rectangle. Its front matches Scene::project_surface exactly.
// All layers share that chart; no exterior viewport/backing rectangle is drawn.
// FRAGMENT: image/backdrop(sampler2D), reconstructLayer(bool), uvTransform(vec4),
// Both image programs accept previousImage/previousBackdrop(sampler2D),
// previousReconstruct(bool), liveMix(float). ALWAYS bind liveMix=1 for ordinary
// captures (GLSL initializes uniforms to zero). Old/live layers are separately
// recovered to premultiplied colors before blending and material effects.
// layer(int: 0 wallpaper/opaque dark globe, 1 attached windows, 2 world shell),
// opacity, endpointEnvelope, nightBrightness, indirectBrightness,
// cloudCoverage, cloudOpacity, cloudSpeed, time, atmosphereIntensity,
// atmosphereThickness(float), lightDirection/cameraPosition(vec3), lightingEnabled,
// cloudsEnabled, atmosphereEnabled(bool), samplingQuality(int: 0..2).
// Shared planetRotation(mat3)/planetCenter(vec3), planetRadius(float world units),
// cloudHeight(float fraction of radius, recommended .035) define the independent
// analytic cloud shell, an oblate local approximation while unrolling and
// exactly spherical on the globe. Height/optical opacity vanish at endpoints.
// uvTransform is offset.xy/scale.zw; FBO bottom-left images use (0,1,1,-1).
// Set endpointEnvelope to zero at BOTH native endpoints. At zero the image path
// is exact and no lighting, relief, cloud, atmosphere or visibility mask remains.
// Samplers use CLAMP_TO_EDGE, never mirrored repeat. Production front UVs remain
// inside the captured desktop; its material perimeter IS the body's perimeter.
// No wallpaper-edge extension or independently masked interior chart is drawn.
// All elevations are local scene units; Renderer/Scene own depth and ordering.
inline constexpr std::string_view VERTEX = R"GLSL(#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 flatPosition;
layout(location=1) in vec3 spherePosition;
layout(location=2) in vec2 texcoord;
layout(location=3) in float hemisphere;
uniform mat4 viewProjection;
uniform mat3 planetRotation;
uniform vec3 planetCenter;
uniform float curvature;
uniform float endpointEnvelope;
uniform float elevation;
uniform int layer;
uniform float depthStrength;
uniform bool uDepthEnabled;
uniform int depthSource;
uniform int samplingQuality;
uniform sampler2D depthImage;
uniform bool parametricMesh;
uniform bool fullSphere;
uniform float aspect;
uniform float radius;
uniform float coverage;
out vec2 vUV;
out vec3 vNormal;
out vec3 vLocalNormal;
out vec3 vWorld;
flat out float vHemisphere;
float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float noise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash31(i), hash31(i+vec3(1,0,0)), f.x),
                   mix(hash31(i+vec3(0,1,0)), hash31(i+vec3(1,1,0)), f.x), f.y),
               mix(mix(hash31(i+vec3(0,0,1)), hash31(i+vec3(1,0,1)), f.x),
                   mix(hash31(i+vec3(0,1,1)), hash31(i+vec3(1,1,1)), f.x), f.y), f.z);
}
// Stable through the planar limit; no curvature-sized divisions or branches.
float stableSinc(float x) {
    float squared = x*x;
    return abs(x)<0.01 ? 1.0-squared/6.0+squared*squared/120.0 : sin(x)/x;
}
vec3 spinY(vec3 point, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(c*point.x+s*point.z,point.y,-s*point.x+c*point.z);
}
void main() {
    vec3 desktopPoint = flatPosition;
    vec3 sphere = spherePosition;
    vec2 chartUV = texcoord;
    float bend = clamp(curvature,0.0,1.0);
    vec3 angularPoint = vec3(0);
    vec3 angularNormal = vec3(0,0,1);
    float spin = atan(planetRotation[2][0],planetRotation[0][0])*bend;
    if (parametricMesh) {
        vec2 uv = clamp(texcoord,0.0,1.0);
        float a = max(aspect,0.001);
        float r = max(radius,0.001);
        float c = clamp(coverage,0.001,1.0);
        if (!fullSphere) {
        float longitudeLimit = 3.141592653589793*c*min(1.0,a/2.0);
        float latitudeLimit = 1.570796326794897*c*min(1.0,2.0/a);
        float longitude = (2.0*uv.x-1.0)*longitudeLimit;
        float latitude = (1.0-2.0*uv.y)*latitudeLimit;
        chartUV = uv;
        desktopPoint = vec3((2.0*chartUV.x-1.0)*a,1.0-2.0*chartUV.y,r);
        // The chart is an ellipsoid patch with axes scaleX/bend,
        // scaleY/bend,r/bend and frontmost point z=r. Its back recedes
        // continuously to infinity as the front becomes the native plane.
        // Stable central arithmetic avoids cancellation in that planar limit.
        float lonAngle = bend*longitude, latAngle = bend*latitude;
        float scaleX = mix(a/longitudeLimit,r,bend);
        float scaleY = mix(1.0/latitudeLimit,r,bend);
        float halfLon = stableSinc(0.5*lonAngle);
        float halfLat = stableSinc(0.5*latAngle);
        float lonSquared = longitude*longitude*halfLon*halfLon;
        float latSquared = latitude*latitude*halfLat*halfLat;
        // sin-half identity avoids subtracting nearly equal cosines at k~0.
        float depthOffset = bend*(0.5*lonSquared+0.5*latSquared
                                 -0.25*bend*bend*lonSquared*latSquared);
        angularPoint = vec3(desktopPoint.x*cos(latAngle)*stableSinc(lonAngle)*
                                mix(1.0,r*longitudeLimit/a,bend),
                            desktopPoint.y*stableSinc(latAngle)*
                                mix(1.0,r*latitudeLimit,bend),
                            r-r*depthOffset);
        angularNormal = normalize(vec3((r/scaleX)*sin(lonAngle)*cos(latAngle),
                                       (r/scaleY)*sin(latAngle),
                                       cos(lonAngle)*cos(latAngle)));
        sphere = r*vec3(cos(latAngle)*sin(lonAngle),
                        sin(latAngle),cos(latAngle)*cos(lonAngle));
        } else {
            // A square-to-disk chart maps the entire desktop to the FRONT
            // hemisphere. Its perimeter is the body's silhouette, never an
            // internal rectangle painted against a second dark surface.
            vec2 native = vec2(2.0*uv.x-1.0,1.0-2.0*uv.y);
            vec2 density = c*vec2(min(1.0,a),min(1.0,1.0/a));
            vec2 square = native*(density+(1.0-density)*native*native);
            vec2 root = sqrt(max(vec2(0),1.0-0.5*square.yx*square.yx));
            vec3 disk = vec3(square*root,
                sqrt(max(0.0,(1.0-square.x*square.x)*(1.0-square.y*square.y))));
            float side = hemisphere<0.0 ? -1.0 : 1.0;
            angularPoint = vec3(mix(a*native.x,r*disk.x,bend),
                                mix(native.y,r*disk.y,bend),
                                r*(1.0-bend+side*bend*disk.z));
            // Differentiate the geometric chart, scaling the cross product by
            // disk.z to avoid 1/sqrt singularities at the hemisphere perimeter.
            vec2 derivative = density+3.0*(1.0-density)*native*native;
            float xx = (1.0-bend)*a+bend*r*derivative.x*root.x;
            float xy = -bend*r*square.x*square.y*derivative.y/(2.0*max(root.x,0.000001));
            float yx = -bend*r*square.y*square.x*derivative.x/(2.0*max(root.y,0.000001));
            float yy = 1.0-bend+bend*r*derivative.y*root.y;
            float zx = -side*bend*r*square.x*(1.0-square.y*square.y)*derivative.x;
            float zy = -side*bend*r*square.y*(1.0-square.x*square.x)*derivative.y;
            vec3 crossed = side*vec3(yx*zy-zx*yy,zx*xy-xx*zy,(xx*yy-yx*xy)*disk.z);
            angularNormal = length(crossed)>0.000001 ? normalize(crossed) :
                normalize(vec3(disk.xy,side*disk.z));
            if (bend == 0.0) angularNormal = vec3(0,0,side);
            sphere = r*vec3(disk.xy,side*disk.z);
        }
    }
    vec3 localNormal = normalize(sphere);
    vec3 normal;
    if (parametricMesh) {
        normal = spinY(angularNormal,spin);
    } else {
        vec3 mixedNormal = mix(vec3(0,0,1), planetRotation*localNormal,bend);
        float normalLength = length(mixedNormal);
        normal = normalLength>0.000001 ? mixedNormal/normalLength : planetRotation*localNormal;
    }
    float depth = 0.0;
    if (uDepthEnabled && endpointEnvelope > 0.0 && depthStrength != 0.0) {
        if (depthSource == 1) {
            // Decoded PNG row zero is the chart's top row; unlike native FBO
            // captures, this texture must not receive the FBO Y transform.
            depth = textureLod(depthImage, clamp(chartUV, 0.0, 1.0), 0.0).r;
        } else {
            // Spherical domain has no UV seam and stays attached during turns.
            depth = noise3(localNormal * 5.0 + vec3(19,7,3));
            if (samplingQuality > 0)
                depth = depth * 0.72 + noise3(localNormal * 13.0) * 0.28;
            if (samplingQuality > 1)
                depth = depth * 0.88 + noise3(localNormal * 29.0) * 0.12;
        }
        vec2 chartDistance = min(chartUV,1.0-chartUV);
        float envelope = clamp(endpointEnvelope,0.0,1.0);
        float chartShape = min(1.0,3.0*sqrt(envelope));
        float feather = max(0.000001,0.16*chartShape);
        vec2 boundary = smoothstep(vec2(0.0),vec2(feather),chartDistance);
        float radial = 1.0-smoothstep(mix(1.5,0.62,chartShape),mix(1.75,1.05,chartShape),
                                      length((chartUV-0.5)*2.0));
        float horizon = mix(-1.05,0.0,chartShape);
        float reliefMask = boundary.x*boundary.y*radial*smoothstep(horizon,horizon+0.06,localNormal.z);
        depth = (depth - 0.5) * depthStrength*reliefMask;
    }
    vec3 world = planetCenter;
    if (parametricMesh) world += spinY(angularPoint,spin);
    else world += mix(desktopPoint,planetRotation*sphere,bend);
    world += normal * ((elevation + depth) * endpointEnvelope);
    vWorld = world;
    vNormal = normal;
    vLocalNormal = localNormal;
    vUV = chartUV;
    vHemisphere = fullSphere ? hemisphere : 0.0;
    gl_Position = viewProjection * vec4(world, 1.0);
    // Foregrounds flatten onto the wallpaper. Separately rasterized triangles
    // can round to opposite sides of a D24 depth value even with LEQUAL.
    // Reserve four depth steps behind them without moving any projected pixel.
    // The bias vanishes on full spheres, preserving interplanetary occlusion.
    if (layer == 0) gl_Position.z += gl_Position.w * (8.0 / 16777216.0) * (1.0-bend);
}
)GLSL";

inline constexpr std::string_view FRAGMENT = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
in vec3 vNormal;
in vec3 vLocalNormal;
in vec3 vWorld;
flat in float vHemisphere;
uniform sampler2D image;
uniform sampler2D backdrop;
uniform bool reconstructLayer;
uniform sampler2D previousImage;
uniform sampler2D previousBackdrop;
uniform bool previousReconstruct;
uniform float liveMix;
uniform vec4 uvTransform;
uniform int layer;
uniform float opacity;
uniform float endpointEnvelope;
uniform vec3 lightDirection;
uniform vec3 cameraPosition;
uniform mat3 planetRotation;
uniform vec3 planetCenter;
uniform float planetRadius;
uniform float cloudHeight;
uniform float nightBrightness;
uniform float indirectBrightness;
uniform bool lightingEnabled;
uniform bool cloudsEnabled;
uniform bool atmosphereEnabled;
uniform float cloudCoverage;
uniform float cloudOpacity;
uniform float cloudSpeed;
uniform float time;
uniform float atmosphereIntensity;
uniform float atmosphereThickness;
uniform int samplingQuality;
// Zero preserves the captured image; renderer applies only modest shared-paper identity variation.
uniform vec3 identityTint;
uniform bool opaqueCapture;
uniform bool parametricMesh;
uniform bool fullSphere;
uniform float aspect;
uniform float coverage;
out vec4 fragColor;
vec4 recoverCapture(sampler2D captured, sampler2D background, bool reconstruct, vec2 uv) {
    vec4 color = texture(captured,uv);
    if (reconstruct)
        color.rgb = max(color.rgb-texture(background,uv).rgb*(1.0-color.a),vec3(0));
    return color;
}
vec4 blendCapture(sampler2D captured, sampler2D background, bool reconstruct,
                  sampler2D oldCapture, sampler2D oldBackground, bool oldReconstruct,
                  float amount, vec2 uv) {
    if (amount <= 0.0) return recoverCapture(oldCapture,oldBackground,oldReconstruct,uv);
    vec4 color = recoverCapture(captured,background,reconstruct,uv);
    if (amount >= 1.0) return color;
    return mix(recoverCapture(oldCapture,oldBackground,oldReconstruct,uv),color,amount);
}
float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float noise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(mix(hash31(i),hash31(i+vec3(1,0,0)),f.x),
                   mix(hash31(i+vec3(0,1,0)),hash31(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash31(i+vec3(0,0,1)),hash31(i+vec3(1,0,1)),f.x),
                   mix(hash31(i+vec3(0,1,1)),hash31(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float clouds(vec3 n) {
    // Separate spherical shell motion, not a scrolling screen-space overlay.
    float a = time * cloudSpeed;
    vec2 xz = mat2(cos(a),-sin(a),sin(a),cos(a))*n.xz;
    vec3 p = vec3(xz.x,n.y,xz.y)*6.0;
    float f = noise3(p)*0.64 + noise3(p*2.03+7.0)*0.36;
    if (samplingQuality > 0) f = f*0.85 + noise3(p*4.07+13.0)*0.15;
    if (samplingQuality > 1) f = f*0.93 + noise3(p*8.13+23.0)*0.07;
    float threshold = mix(0.85,0.2,clamp(cloudCoverage,0.0,1.0));
    return smoothstep(threshold-0.07,threshold+0.07,f);
}
void main() {
    // Cull the body's inward faces in shader so a numerically coincident back
    // plane cannot overwrite native pixels when curvature tends to zero.
    if (parametricMesh && fullSphere && vHemisphere<0.0 &&
        dot(vNormal,cameraPosition-vWorld)<=0.0) discard;
    if (parametricMesh && vHemisphere<0.0 && layer!=0) discard;
    vec2 uv = clamp(uvTransform.xy + vUV*uvTransform.zw,0.0,1.0);
    vec4 c = blendCapture(image,backdrop,reconstructLayer,
                         previousImage,previousBackdrop,previousReconstruct,liveMix,uv);
    float envelope = clamp(endpointEnvelope,0.0,1.0);
    if (opaqueCapture) c.a = 1.0;
    if (parametricMesh && vHemisphere<0.0) c = vec4(vec3(0.008,0.012,0.023),1);
    if (envelope == 0.0) {
        fragColor = c*opacity;
        return;
    }
    vec3 n = normalize(vNormal);
    // Reconstruct the actual displaced facet normal without extra texture taps.
    vec3 facet = cross(dFdx(vWorld),dFdy(vWorld));
    float facetLength = length(facet);
    if (facetLength > 0.000001) {
        facet /= facetLength;
        if (dot(facet,n)<0.0) facet = -facet;
        n = normalize(mix(n,facet,0.35*envelope));
    }
    vec2 chartDistance = min(vUV,1.0-vUV);
    float imageMask;
    if (parametricMesh) {
        // Every front vertex has an actual desktop UV. There is no exterior
        // wallpaper continuation or hidden chart-shaped support boundary.
        imageMask = 1.0;
    } else {
        // Explicit-attribute material clients keep their legacy chart support.
        float edge = max(fwidth(vLocalNormal.z)*1.5,0.025);
        float chartShape = min(1.0,3.0*sqrt(envelope));
        float horizon = mix(-1.05,0.0,chartShape);
        float visible = smoothstep(horizon-edge,horizon+edge,vLocalNormal.z);
        vec2 chartLower = -max(fwidth(vUV)*1.5,vec2(0.000001))*(1.0-chartShape);
        vec2 chartUpper = vec2(0.16*chartShape);
        vec2 chartMask = smoothstep(chartLower,chartUpper,chartDistance);
        float radialMask = 1.0-smoothstep(mix(1.5,0.62,chartShape),
            mix(1.75,1.05,chartShape),length((vUV-0.5)*2.0));
        imageMask = visible*chartMask.x*chartMask.y*radialMask;
    }
    if (layer == 0) {
        if (!parametricMesh)
            c.rgb = mix(vec3(0.008,0.012,0.023)*c.a,c.rgb,imageMask);
        c.rgb *= vec3(1.0) + identityTint;
    } else {
        // Foregrounds retain premultiplied alpha and never receive terrain/clouds.
        c *= imageMask;
    }
    if (layer == 0) {
        vec3 light = lightDirection;
        light = length(light)>0.0001 ? normalize(light) : vec3(0,0,1);
        float day = smoothstep(-0.24,0.5,dot(n,light));
        float indirect = clamp(indirectBrightness,0.0,1.0);
        if (lightingEnabled) {
            float ambient = clamp(nightBrightness+indirect*0.25,0.0,1.0);
            c.rgb *= mix(1.0,mix(ambient,1.0,day),envelope);
            // Actual bounced radiance, not just a multiplier of a near-black
            // unseen hemisphere. One continuous base receives the same light.
            c.rgb += vec3(0.025,0.035,0.055)*indirect*(1.0-day)*envelope*c.a;
        }
        if (cloudsEnabled && cloudOpacity > 0.0 && planetRadius > 0.0) {
            // Intersect the view ray with a distinct elevated spherical shell.
            // The hit depends on camera position and height, unlike a surface
            // normal lookup; this supplies true shell/surface parallax.
            vec3 cameraToSurface = vWorld-cameraPosition;
            float surfaceDistance = length(cameraToSurface);
            vec3 ray = cameraToSurface/max(surfaceDistance,0.000001);
            vec3 origin = cameraPosition-planetCenter;
            vec3 shellAxes = vec3(planetRadius*(1.0+max(cloudHeight,0.0)*envelope));
            mat3 shellRotation = planetRotation;
            vec3 shellCenter = vec3(0);
            if (parametricMesh) {
                float spin = atan(planetRotation[2][0],planetRotation[0][0])*envelope;
                float co = cos(spin), si = sin(spin);
                shellRotation = mat3(co,0,-si,0,1,0,si,0,co);
                // Local oblate-shell approximation to the rounded-rectangle
                // morph. It is exact on the globe, shares the front pole and
                // flattens with the body; no separate undeformed sphere remains.
                shellCenter.z = planetRadius*(1.0-envelope);
                shellAxes = vec3(mix(max(aspect,0.001),planetRadius,envelope),
                                 mix(1.0,planetRadius,envelope),planetRadius*envelope);
                shellAxes += max(cloudHeight,0.0)*planetRadius*envelope;
            }
            vec3 localOrigin = transpose(shellRotation)*origin-shellCenter;
            vec3 localRay = transpose(shellRotation)*ray;
            vec3 q = localOrigin/shellAxes;
            vec3 d = localRay/shellAxes;
            float aa = dot(d,d), b = dot(q,d);
            float cc = dot(q,q)-1.0;
            float discriminant = b*b-aa*cc;
            if (discriminant > 0.0) {
                float root = sqrt(discriminant);
                // Stable near root; the naive -b-root cancels near landing.
                float farRoot = -b+(b<0.0 ? root : -root);
                float t0 = farRoot/aa;
                float t1 = abs(farRoot)>0.000001 ? cc/farRoot : t0;
                float t = min(t0,t1);
                if (t < 0.0) t = max(t0,t1);
                float hit = t;
                // A shell behind the surface cannot paint over that surface.
                if (hit > 0.0 && hit <= surfaceDistance+planetRadius*0.001) {
                    vec3 localCloudNormal = normalize(q+d*t);
                    vec3 shellNormal = shellRotation*normalize(localCloudNormal/shellAxes);
                    float density = clouds(localCloudNormal);
                    float viewFacing = max(abs(dot(shellNormal,-ray)),0.2);
                    float opticalDepth = cloudOpacity*density/viewFacing;
                    float cloudAlpha = (1.0-exp(-opticalDepth))*envelope;
                    vec3 cloudLight = vec3(0.72,0.80,0.92);
                    if (lightingEnabled) {
                        float shellDay = smoothstep(-0.24,0.5,dot(shellNormal,light));
                        float ambient = clamp(nightBrightness+indirect*0.25,0.0,1.0);
                        cloudLight *= mix(ambient,1.0,shellDay);
                        cloudLight += vec3(0.025,0.035,0.055)*indirect*(1.0-shellDay);
                        float sunward = pow(max(dot(-ray,light),0.0),8.0);
                        cloudLight += vec3(0.10,0.085,0.065)*sunward*shellDay;
                    }
                    c.rgb = mix(c.rgb,cloudLight*c.a,clamp(cloudAlpha,0.0,1.0));
                }
            }
        }
    }
    if (atmosphereEnabled && layer == 0) {
        vec3 view = normalize(cameraPosition-vWorld);
        float facing = clamp(abs(dot(n,view)),0.0,1.0);
        float limb = pow(1.0-facing,
                         mix(12.0,2.0,clamp(atmosphereThickness*8.0,0.0,1.0)));
        c.rgb += vec3(0.12,0.36,0.68)*limb*atmosphereIntensity*envelope*c.a;
    }
    fragColor = c*opacity;
}
)GLSL";

// A unit rectangle, TRIANGLE_STRIP with four vertices and no vertex buffer.
// model maps centered [-.5,.5] local XY to world; imageRect is normalized
// full-monitor offset.xy/extent.zw, top-left origin. Scene owns each window's
// independently anchored model, its elevation, and camera-relative parallax.
// Native full-screen blits use identity VP and model scale (2,2,1), imageRect
// (0,0,1,1). Uniforms: viewProjection/model(mat4), imageRect/uvTransform(vec4),
// image/backdrop(sampler2D), opacity(float), reconstructLayer/forceOpaque(bool).
// HUD blending optionally uses secondaryImage/secondaryBackdrop(sampler2D),
// secondaryReconstruct(bool), mixAmount(float, default 0). Recover and history-
// blend each endpoint, then mix premultiplied endpoints before opacity/GL blend.
// The secondary endpoint also accepts secondaryPreviousImage/secondaryPreviousBackdrop,
// secondaryPreviousReconstruct and secondaryLiveMix; bind secondaryLiveMix=1
// when it has no capture history. Endpoint and history blends each skip
// inactive texture reads when their interpolation is exactly zero or one.
// forceOpaque is ONLY for native full-monitor composites whose RGB already
// includes the opaque backdrop but whose stored alpha describes coverage.
inline constexpr std::string_view QUAD_VERTEX = R"GLSL(#version 300 es
precision highp float;
uniform mat4 viewProjection;
uniform mat4 model;
uniform vec4 imageRect;
out vec2 vUV;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1),float((gl_VertexID >> 1) & 1));
    vUV = imageRect.xy + vec2(p.x,1.0-p.y)*imageRect.zw;
    gl_Position = viewProjection*model*vec4(p-0.5,0.0,1.0);
}
)GLSL";
inline constexpr std::string_view QUAD_FRAGMENT = R"GLSL(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D image;
uniform sampler2D backdrop;
uniform bool reconstructLayer;
uniform sampler2D previousImage;
uniform sampler2D previousBackdrop;
uniform bool previousReconstruct;
uniform float liveMix;
uniform bool forceOpaque;
uniform sampler2D secondaryImage;
uniform sampler2D secondaryBackdrop;
uniform bool secondaryReconstruct;
uniform sampler2D secondaryPreviousImage;
uniform sampler2D secondaryPreviousBackdrop;
uniform bool secondaryPreviousReconstruct;
uniform float secondaryLiveMix;
uniform float mixAmount;
uniform vec4 uvTransform;
uniform float opacity;
out vec4 fragColor;
vec4 recoverCapture(sampler2D captured, sampler2D background, bool reconstruct, vec2 uv) {
    vec4 color = texture(captured,uv);
    if (reconstruct)
        color.rgb = max(color.rgb-texture(background,uv).rgb*(1.0-color.a),vec3(0));
    return color;
}
vec4 blendCapture(sampler2D captured, sampler2D background, bool reconstruct,
                  sampler2D oldCapture, sampler2D oldBackground, bool oldReconstruct,
                  float amount, vec2 uv) {
    if (amount <= 0.0) return recoverCapture(oldCapture,oldBackground,oldReconstruct,uv);
    vec4 color = recoverCapture(captured,background,reconstruct,uv);
    if (amount >= 1.0) return color;
    return mix(recoverCapture(oldCapture,oldBackground,oldReconstruct,uv),color,amount);
}
void main() {
    vec2 uv = clamp(uvTransform.xy+vUV*uvTransform.zw,0.0,1.0);
    float endpointMix = clamp(mixAmount,0.0,1.0);
    vec4 c = vec4(0);
    if (endpointMix < 1.0)
        c = blendCapture(image,backdrop,reconstructLayer,
                         previousImage,previousBackdrop,previousReconstruct,liveMix,uv);
    if (endpointMix > 0.0) {
        vec4 secondary = blendCapture(secondaryImage,secondaryBackdrop,secondaryReconstruct,
                                     secondaryPreviousImage,secondaryPreviousBackdrop,
                                     secondaryPreviousReconstruct,secondaryLiveMix,uv);
        c = endpointMix >= 1.0 ? secondary : mix(c,secondary,endpointMix);
    }
    if (forceOpaque) c.a = 1.0;
    fragColor = c*opacity;
}
)GLSL";

// Full-screen TRIANGLES, three vertices, no VBO. Draw before opaque planets.
// Uniforms: inverseViewProjection(mat4), cameraPosition(vec3, floating-origin
// relative), endpointEnvelope/starDensity(float), starOffsetNear/Mid/Far(vec3).
// Each offset component is computed in CPU double precision as positiveModulo(
// (worldOrigin+relativeCamera)*starParallax/depth,1024), depth=40/90/210.
// The periodic cell hash makes offset wraps continuous, avoiding huge floats
// and preserving parallax through camera rebasing and across journeys.
inline constexpr std::string_view SKY_VERTEX = R"GLSL(#version 300 es
precision highp float;
out vec2 vNDC;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2),float(gl_VertexID & 2));
    vNDC = p*2.0-1.0;
    gl_Position = vec4(vNDC,1.0,1.0);
}
)GLSL";
inline constexpr std::string_view SKY_FRAGMENT = R"GLSL(#version 300 es
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
)GLSL";

} // namespace hyprimate::workspace::planet_hop::shader
