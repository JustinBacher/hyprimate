#version 300 es
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
