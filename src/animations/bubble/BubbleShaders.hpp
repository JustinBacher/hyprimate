#pragma once
constexpr const char *VERTEX = R"GLSL(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr const char *FRAGMENT = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D image, wallpaperImage;
uniform vec2 wallpaperOrigin, wallpaperX, wallpaperY, wallpaperPixel;
uniform int wallpaperReady;
uniform vec2 sourcePos, sourceSize, textureOrigin, textureSize, outputOrigin, size;
uniform float seed, residueTime;
struct Membrane {
    vec2 halfSize, rupture, core;
    float corner, depth, content, film, visibility, ripple, time, tear, retract, remnant;
    int closing;
};
uniform Membrane windowSurface, shellSurface;
out vec4 colour;

vec4 over(vec4 front, vec4 back) { return front + back * (1.0 - front.a); }
vec3 filmTint(float thickness, float viewCosine) {
    vec3 interference = cos(6.2831853 * 2.0 * 1.33 * thickness * viewCosine / vec3(650.0, 510.0, 475.0));
    return vec3(0.82, 0.89, 0.92) + 0.035 * interference;
}
float boxDistance(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - h + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
vec4 membrane(Membrane s, vec2 logical, bool background) {
    if (s.visibility <= 0.0) return vec4(0.0);
    vec2 halfSize = s.halfSize, rupture = s.rupture, core = s.core;
    float corner = s.corner, depth = s.depth, content = s.content, film = s.film;
    float visibility = s.visibility, ripple = s.ripple, time = s.time;
    float tear = s.tear, retract = s.retract, remnant = s.remnant;
    vec2 p = logical - size * 0.5;
    vec2 baseUV = (sourcePos + sourceSize * (logical / size) - textureOrigin) / textureSize;
    vec2 lo = max((sourcePos - textureOrigin + 0.5) / textureSize, vec2(0.5) / textureSize);
    vec2 hi = min((sourcePos + sourceSize - textureOrigin - 0.5) / textureSize,
                  vec2(1.0) - vec2(0.5) / textureSize);
    float tension = s.closing != 0 ? smoothstep(0.0, 0.045, time) * (1.0 - smoothstep(0.10, 0.20, time)) : 0.0;
    vec2 tensionDelta = p - rupture;
    float tensionRadius = max(4.0, min(size.x, size.y) * 0.22);
    float tensionField = exp(-dot(tensionDelta, tensionDelta) / (tensionRadius * tensionRadius));
    vec2 tug = tensionDelta / max(size, vec2(1.0)) * (0.055 * tension * tensionField);

    // Exact endpoint: no analytical silhouette or filtering change to native alpha.
    if (!background && depth == 0.0 && all(equal(core, vec2(0.0))) && film == 0.0 && ripple == 0.0 &&
        tear == 0.0 && retract == 0.0 && visibility == 1.0 && content == 1.0) {
        if (any(lessThan(logical, vec2(0.0))) || any(greaterThanEqual(logical, size))) return vec4(0.0);
        vec4 result = texture(image, clamp(baseUV + tug * sourceSize / textureSize, lo, hi));
        float shine = 0.055 * tension * tensionField;
        result.rgb = mix(result.rgb, vec3(result.a) * vec3(0.90, 0.95, 1.0), shine);
        return result;
    }
    vec2 h = max(halfSize, vec2(0.001));
    float r = clamp(corner, 0.0, min(h.x, h.y));
    float phase = atan(p.y, p.x);
    float wave = ripple * sin(phase * 5.0 + seed * 6.0) * 0.35;
    float sd = boxDistance(p, h + vec2(wave), r);
    float aa = max(fwidth(sd), 0.5 * max(size.x / sourceSize.x, size.y / sourceSize.y));
    float shape = 1.0 - smoothstep(-aa, aa, sd);
    vec2 nxy = clamp(p / h, -1.0, 1.0);
    // The dome follows the current boundary: the shell stays spherical, while
    // the independently emerging window's cap flattens onto its rectangular rim.
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
    // Height and slope vanish at every edge; the two approved pressure modes
    // move only the soft core after the perimeter has landed.
    vec2 b = max(vec2(0.0), vec2(1.0) - nxy * nxy);
    float support = b.x * b.x * b.y * b.y;
    float pressure = core.x + core.y * dot(nxy, nxy);
    vec2 supportGradient = -4.0 * nxy * vec2(b.x * b.y * b.y, b.y * b.x * b.x);
    vec2 coreNormal = -(supportGradient * pressure + support * core.y * 2.0 * nxy) / h;
    vec2 coreOffset = coreNormal * (0.30 * min(h.x, h.y)) / size;
    vec2 normal = domeNormal + coreNormal;
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
    // Gather a wide band of imagery into the narrow rolling, retracting lip.
    vec2 radial = fromTear / max(dist, 0.001);
    float band = exp(-max(edge, 0.0) / (rimWidth * 5.0));
    float curl = retract * band * (rimWidth * 7.0 + max(edge, 0.0) * 1.2);
    vec2 rolled = -radial * curl / max(size, vec2(1.0)) +
                  radial * (0.04 * retract * exp(-abs(edge - rimWidth) / rimWidth));
    uvUnit += rolled;
    vec4 imagery = vec4(0.0);
    if (background) {
        if (wallpaperReady != 0) {
            vec2 backgroundUnit = 0.5 + (h * projected - domeNormal * depth * 0.14) / size - coreOffset + tug + rolled;
            vec2 backgroundUV = wallpaperOrigin + wallpaperX * backgroundUnit.x + wallpaperY * backgroundUnit.y;
            imagery = texture(wallpaperImage, clamp(backgroundUV, wallpaperPixel * 0.5,
                                                     vec2(1.0) - wallpaperPixel * 0.5));
        }
    } else {
        vec2 uv = (sourcePos + sourceSize * uvUnit - textureOrigin) / textureSize;
        imagery = texture(image, clamp(uv, lo, hi));
    }
    float breakup = 1.0 - 0.38 * remnant * smoothstep(0.25, 0.9, sin(a * 13.0 + seed * 8.0 + retract * 2.0)) *
                    exp(-abs(sd) / max(rimWidth * 3.0, 1.0));
    float transmission = clamp(content, 0.0, 1.0) * shape * intact * visibility * breakup;
    vec4 result = imagery * transmission;
    float viewCosine = inversesqrt(1.0 + dot(normal, normal));
    float fresnel = pow(1.0 - viewCosine, 2.0);
    float thickness = 310.0 + 85.0 * cap + 24.0 * nxy.y + 12.0 * sin(phase + seed * 6.0);
    vec3 tint = filmTint(thickness, viewCosine);
    // Narrow, elongated reflections instead of a large plastic specular orb.
    vec2 light = nxy + vec2(0.38, 0.46) + normal * 0.12;
    float glint = exp(-light.x * light.x * 85.0 - light.y * light.y * 9.0) +
                  0.38 * exp(-pow((light.x - 0.11) * 12.0, 2.0) - pow((light.y + 0.12) * 5.0, 2.0));
    float filmAlpha = clamp(film * shape * intact * visibility * (0.022 + 0.40*fresnel + 0.14*glint), 0.0, 0.45);
    float curlShadow = tear > 0.0 ? exp(-abs(edge - rimWidth * 1.8) / (rimWidth * 1.3)) : 0.0;
    result.rgb *= 1.0 - 0.38 * curlShadow * retract;
    float lip = tear > 0.0 ? exp(-abs(edge) / max(0.8, rimWidth * 0.22)) * shape : 0.0;
    float rimAlpha = clamp((rim * 0.18 + lip * 0.72) * smoothstep(0.0, 0.14, retract) * visibility, 0.0, 0.85);
    float materialAlpha = filmAlpha + rimAlpha * (1.0 - filmAlpha);
    vec3 material = mix(tint, vec3(0.92, 0.97, 1.0), rimAlpha);
    result = over(vec4(material * materialAlpha, materialAlpha), result);
    if (s.closing != 0 && remnant > 0.0) {
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
    return result;
}
vec4 screenFilm(vec2 p) {
    float radius = min(size.x, size.y) * 0.5;
    float envelope = smoothstep(0.0, 0.08, residueTime) * (1.0 - smoothstep(0.45, 1.0, residueTime));
    float wetting = mix(0.72, 1.0, smoothstep(0.0, 0.16, residueTime));
    vec4 result = vec4(0.0);
    // Two flat smears and three tiny beads wet the screen. Their centres and
    // orientation never follow the window, its core, or a later workspace move.
    for (int i = 0; i < 5; ++i) {
        float angle = seed * 6.0 + float(i) * 2.18 + 0.6;
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 centre = direction * radius * (i < 2 ? 0.62 : 0.82);
        vec2 delta = p - centre;
        vec2 local = vec2(dot(delta, vec2(-direction.y, direction.x)), dot(delta, direction));
        vec2 axes = radius * wetting * (i < 2 ? vec2(0.22, 0.052) : vec2(0.022, 0.036));
        vec2 q = local / max(axes, vec2(0.75));
        float distance = length(q);
        float aa = max(fwidth(distance), 0.015);
        float shape = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, distance);
        float rim = exp(-pow((distance - 0.86) * 17.0, 2.0)) * shape;
        float glint = exp(-pow(q.x + 0.32, 2.0) * 30.0 - pow(q.y + 0.60, 2.0) * 55.0) * shape;
        float shadow = rim * smoothstep(-0.4, 0.7, q.y);
        float alpha = envelope * (shape * 0.008 + rim * 0.20 + glint * 0.38);
        vec3 tint = filmTint(320.0 + 24.0 * q.y, 0.8) - vec3(shadow * 0.55);
        tint = mix(tint, vec3(0.96, 0.98, 1.0), glint);
        result = over(vec4(tint * alpha, alpha), result);
    }
    return result;
}
void main() {
    // The work buffer and Hyprland's canvas quad both use texture v=0 at y=0.
    vec2 logical = (outputOrigin + gl_FragCoord.xy - sourcePos) * size / sourceSize;
    if (residueTime >= 0.0) {
        colour = screenFilm(logical - size * 0.5);
        return;
    }
    colour = over(membrane(shellSurface, logical, true), membrane(windowSurface, logical, false));
}
)GLSL";
