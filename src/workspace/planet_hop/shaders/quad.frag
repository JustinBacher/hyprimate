#version 300 es
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
