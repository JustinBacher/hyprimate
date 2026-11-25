#version 300 es
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
