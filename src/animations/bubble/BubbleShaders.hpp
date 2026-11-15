#pragma once
constexpr const char *VERTEX = R"GLSL(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr const char *FRAGMENT = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D image;
uniform sampler2D wallpaperImage;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform float windowMix;
uniform int wallpaperReady;
uniform vec2 canvas, sourcePos, sourceSize, textureOrigin, textureSize, outputOrigin;
uniform vec2 size, halfSize, rupture, core;
uniform float corner, depth, content, film, visibility, ripple, time, tear, retract, remnant, seed;
uniform int closing;
out vec4 colour;

float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - h + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    // The work buffer and Hyprland's canvas quad both use texture v=0 at y=0.
    vec2 device = outputOrigin + gl_FragCoord.xy;
    vec2 logical = (device - sourcePos) * size / sourceSize;
    vec2 p = logical - size * 0.5;
    vec2 baseUV = (sourcePos + sourceSize * (logical / size) - textureOrigin) / textureSize;
    vec2 lo = (sourcePos - textureOrigin + 0.5) / textureSize;
    vec2 hi = (sourcePos + sourceSize - textureOrigin - 0.5) / textureSize;
    lo = max(lo, vec2(0.5) / textureSize);
    hi = min(hi, vec2(1.0) - vec2(0.5) / textureSize);
    float tension = closing != 0 ? smoothstep(0.0, 0.045, time) * (1.0 - smoothstep(0.10, 0.20, time)) : 0.0;
    vec2 tensionDelta = p - rupture;
    float tensionRadius = max(4.0, min(size.x, size.y) * 0.22);
    float tensionField = exp(-dot(tensionDelta, tensionDelta) / (tensionRadius * tensionRadius));
    vec2 tug = tensionDelta / max(size, vec2(1.0)) * (0.055 * tension * tensionField);


    // Exact endpoint: no analytical silhouette or filtering change to native alpha.
    if (depth == 0.0 && all(equal(core, vec2(0.0))) && film == 0.0 && ripple == 0.0 && tear == 0.0 &&
        retract == 0.0 && visibility == 1.0 && content == 1.0 && windowMix == 1.0) {
        if (any(lessThan(logical, vec2(0.0))) || any(greaterThanEqual(logical, size))) colour = vec4(0.0);
        else {
            colour = texture(image, clamp(baseUV + tug * sourceSize / textureSize, lo, hi));
            float shine = 0.055 * tension * tensionField;
            colour.rgb = mix(colour.rgb, vec3(colour.a) * vec3(0.90, 0.95, 1.0), shine);
        }
        return;
    }
    vec2 h = max(halfSize, vec2(0.001));
    float r = clamp(corner, 0.0, min(h.x, h.y));
    float phase = atan(p.y, p.x);
    float wave = ripple * sin(phase * 5.0 + seed * 6.0) * 0.35;
    float sd = boxDistance(p, h + vec2(wave), r);
    float aa = max(fwidth(sd), 0.5 * max(size.x / sourceSize.x, size.y / sourceSize.y));
    float shape = 1.0 - smoothstep(-aa, aa, sd);
    vec2 nxy = clamp(p / h, -1.0, 1.0);
    // The dome follows the *current* membrane boundary, not a circular lens
    // left inside the flattened rectangle. For a sphere this is exactly |p|/r.
    float radius = clamp(1.0 + sd / min(h.x, h.y), 0.0, 1.0);
    float cap = sqrt(max(0.0, 1.0 - radius * radius));
    float curve = clamp(depth / max(min(h.x, h.y), 1.0), 0.0, 1.0);
    vec2 q = abs(p) - h + r;
    vec2 gradient = max(q, 0.0);
    if (dot(gradient, gradient) > 0.000001)
        gradient = normalize(gradient) * sign(p);
    else
        gradient = q.x > q.y ? vec2(sign(p.x), 0.0) : vec2(0.0, sign(p.y));
    vec2 domeNormal = gradient * radius * curve / max(0.22, cap + 0.22);
    // A clamped rectangular membrane, not another circular lens. Height and
    // slope vanish at every edge; two pressure modes move only the soft core.
    vec2 b = max(vec2(0.0), vec2(1.0) - nxy * nxy);
    float support = b.x * b.x * b.y * b.y;
    float pressure = core.x + core.y * dot(nxy, nxy);
    vec2 supportGradient = -4.0 * nxy * vec2(b.x * b.y * b.y, b.y * b.x * b.x);
    vec2 coreNormal = -(supportGradient * pressure + support * core.y * 2.0 * nxy) / h;
    vec2 coreOffset = coreNormal * (0.30 * min(h.x, h.y)) / size;
    vec2 normal = domeNormal + coreNormal;
    // The spherical surface refracts wallpaper near its screen position, not a scaled destination window.
    vec2 projected = nxy * (1.0 - 0.22 * curve * (1.0 - cap));
    vec2 uvUnit = 0.5 + 0.5 * projected - domeNormal * (0.028 * curve) - coreOffset + tug;
    vec2 fromTear = p - rupture;
    float a = atan(fromTear.y, fromTear.x);
    float dist = length(fromTear);
    float uneven = 1.0 + 0.10 * sin(3.0*a + seed*6.0 + retract*1.3) +
                   0.05 * sin(7.0*a - seed*4.0 - retract*2.0);
    float edge = dist - tear * uneven;
    float intact = tear > 0.0 ? smoothstep(-aa, aa, edge) : 1.0;
    float rimWidth = max(1.5, min(h.x, h.y) * 0.025);
    float rim = tear > 0.0 ? exp(-abs(edge) / rimWidth) * shape : 0.0;
    // Roll a wider source band into the narrow retreating edge, with a local
    // normal deflection. The displacement remains in window UV space only.
    vec2 radial = fromTear / max(dist, 0.001);
    float band = exp(-max(edge, 0.0) / (rimWidth * 5.0));
    float curl = retract * band * (rimWidth * 7.0 + max(edge, 0.0) * 1.2);
    vec2 rolled = -radial * curl / max(size, vec2(1.0)) +
                  radial * (0.04 * retract * exp(-abs(edge - rimWidth) / rimWidth));
    uvUnit += rolled;
    vec2 uv = (sourcePos + sourceSize * uvUnit - textureOrigin) / textureSize;
    vec4 imagery = windowMix > 0.0 ? texture(image, clamp(uv, lo, hi)) * windowMix : vec4(0.0);
    if (windowMix < 1.0 && wallpaperReady != 0) {
        vec2 backgroundUnit = 0.5 + (h * projected - domeNormal * depth * 0.14) / size - coreOffset + tug + rolled;
        vec2 backgroundUV = wallpaperOrigin + wallpaperX * backgroundUnit.x + wallpaperY * backgroundUnit.y;
        imagery += texture(wallpaperImage, clamp(backgroundUV, wallpaperPixel * 0.5,
                                                 vec2(1.0) - wallpaperPixel * 0.5)) * (1.0 - windowMix);
    }
    float breakup = 1.0 - 0.38 * remnant * smoothstep(0.25, 0.9, sin(a * 13.0 + seed * 8.0 + retract * 2.0)) *
                    exp(-abs(sd) / max(rimWidth * 3.0, 1.0));
    float transmission = clamp(content, 0.0, 1.0) * shape * intact * visibility * breakup;
    vec4 result = imagery * transmission;
    float viewCosine = inversesqrt(1.0 + dot(normal, normal));
    float fresnel = pow(1.0 - viewCosine, 2.0);
    // Thin-film phase for three representative wavelengths, with a gentle
    // thickness gradient. Low contrast keeps the film pale rather than rainbow.
    float thickness = 310.0 + 85.0 * cap + 24.0 * nxy.y + 12.0 * sin(phase + seed * 6.0);
    vec3 interference = cos(6.2831853 * 2.0 * 1.33 * thickness * viewCosine / vec3(650.0, 510.0, 475.0));
    vec3 tint = vec3(0.82, 0.89, 0.92) + 0.035 * interference;
    // Two narrow, elongated reflections instead of a large plastic specular orb.
    vec2 light = nxy + vec2(0.38, 0.46) + normal * 0.12;
    float glint = exp(-light.x * light.x * 85.0 - light.y * light.y * 9.0) +
                  0.38 * exp(-pow((light.x - 0.11) * 12.0, 2.0) - pow((light.y + 0.12) * 5.0, 2.0));
    float filmAlpha = clamp(film * shape * intact * visibility * (0.022 + 0.40*fresnel + 0.14*glint), 0.0, 0.45);
    // A dark outer curl beside a narrow luminous lip makes retraction legible
    // over both light and dark content, rather than reading as an expanding mask.
    float curlShadow = tear > 0.0 ? exp(-abs(edge - rimWidth * 1.8) / (rimWidth * 1.3)) : 0.0;
    result.rgb *= 1.0 - 0.38 * curlShadow * retract;
    float lip = tear > 0.0 ? exp(-abs(edge) / max(0.8, rimWidth * 0.22)) * shape : 0.0;
    float rimAlpha = clamp((rim * 0.18 + lip * 0.72) * smoothstep(0.0, 0.14, retract) * visibility, 0.0, 0.85);
    float materialAlpha = filmAlpha + rimAlpha * (1.0 - filmAlpha);
    vec3 material = mix(tint, vec3(0.92, 0.97, 1.0), rimAlpha);
    result.rgb = material * materialAlpha + result.rgb * (1.0 - materialAlpha);
    result.a = materialAlpha + result.a * (1.0 - materialAlpha);
    // Sparse detached droplets, constrained to the conservative close bounds.
    if (closing != 0 && remnant > 0.0) {
        float drops = 0.0;
        for (int i = 0; i < 3; ++i) {
            float angle = seed * 6.0 + float(i) * 2.18 + 0.6;
            vec2 direction = vec2(cos(angle), sin(angle));
            vec2 centre = direction * min(h.x, h.y) * (0.88 + 0.15 * retract);
            float d = length(p - centre) - max(0.7, min(h.x, h.y) * 0.009);
            drops = max(drops, 1.0 - smoothstep(-aa, aa, d));
        }
        float da = drops * remnant * 0.25 * visibility * (1.0 - shape * intact);
        result.rgb += vec3(0.85, 0.92, 0.97) * da * (1.0 - result.a);
        result.a += da * (1.0 - result.a);
    }
    colour = result;
}
)GLSL";
