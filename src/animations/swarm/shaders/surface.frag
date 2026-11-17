#version 300 es
precision highp float;
precision highp int;

uniform sampler2D image;
uniform sampler2D original;
uniform ivec2 imageSize;
uniform int standingAt;
uniform float radius;
uniform int vertical;
out vec4 colour;

// Explicit zero extension prevents both edge clamping and crossing into the walking region.
float alphaAt(ivec2 p) {
    if (p.x < standingAt || p.x >= imageSize.x || p.y < 0 || p.y >= imageSize.y)
        return 0.0;
    return texelFetch(image, p, 0).a;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (p.x < standingAt) {
        colour = vertical != 0 ? texelFetch(original, p, 0) : vec4(0.0);
        return;
    }

    // 33 samples over +/- three standard deviations, with linear interpolation in the
    // filtering direction. Work is bounded independently of the physical radius.
    float sum = 0.0, total = 0.0;
    for (int i = -16; i <= 16; ++i) {
        float t = float(i) * (3.0 / 16.0);
        float offset = t * radius;
        float base = floor(offset);
        ivec2 axis = vertical != 0 ? ivec2(0, 1) : ivec2(1, 0);
        ivec2 q = p + axis * int(base);
        float weight = exp(-0.5 * t * t);
        sum += mix(alphaAt(q), alphaAt(q + axis), offset - base) * weight;
        total += weight;
    }
    float a = sum / total;
    if (vertical != 0) {
        float width = max(fwidth(a), 0.001);
        a = smoothstep(0.5 - width * 0.5, 0.5 + width * 0.5, a);
    }
    colour = vec4(a);
}
