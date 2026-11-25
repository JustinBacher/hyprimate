#pragma once

namespace hyprimate::workspace {
    inline constexpr const char *VERTEX_SHADER = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
    inline constexpr const char *FRAGMENT_SHADER = R"(#version 300 es
precision highp float;
uniform sampler2D sourceImage, destinationImage, sourceWall, destinationWall;
uniform mat3 sourceInverse, destinationInverse;
uniform vec2 size;
uniform float progress, direction, blurRadius;
uniform int style, matching;
uniform vec4 gapColor;
out vec4 color;
vec2 uv(vec2 p) { return p / size; }
bool inside(vec2 p) { return all(greaterThanEqual(p, vec2(0.0))) && all(lessThanEqual(p, size)); }
vec4 wall(sampler2D image, vec2 p, float radius) {
    vec2 q = clamp(uv(p), vec2(0.0), vec2(1.0));
    if (radius <= 0.0) return texture(image, q);
    vec4 sum = texture(image, q) * 4.0;
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
        if (x == 0 && y == 0) continue;
        vec2 offset = vec2(float(x), float(y)) * radius / (2.0 * size);
        sum += texture(image, clamp(q + offset, vec2(0.0), vec2(1.0)));
    }
    return sum / 28.0;
}
void main() {
    // Hyprland's offscreen textures and final TexPass use framebuffer Y as screen Y.
    vec2 p = gl_FragCoord.xy;
    vec2 a = (sourceInverse * vec3(p, 1.0)).xy;
    vec2 b = (destinationInverse * vec3(p, 1.0)).xy;
    bool inA = inside(a), inB = inside(b);
    if (inA || inB) {
        bool useB = inB && !inA;
        vec2 local = useB ? b : a;
        vec4 scene = useB ? texture(destinationImage, uv(local)) : texture(sourceImage, uv(local));
        if (style == 3) {
            vec3 originalWall = useB ? texture(destinationWall, uv(local)).rgb : texture(sourceWall, uv(local)).rgb;
            vec3 foreground = max(scene.rgb - originalWall * (1.0 - scene.a), vec3(0.0));
            vec3 background = texture(sourceWall, uv(p)).rgb;
            if (useB && matching == 0) background = originalWall;
            color = vec4(foreground + background * (1.0 - scene.a), 1.0);
        } else color = vec4(scene.rgb, 1.0);
        return;
    }
    if (style == 1) { color = gapColor; return; }
    if (style == 3) { color = vec4(texture(sourceWall, uv(p)).rgb, 1.0); return; }
    // Distances to the actual rectangular boundaries include the top/bottom exposed
    // regions, not only the wedge between the two facing vertical edges.
    vec2 edgeA = clamp(a, vec2(0.0), size);
    vec2 edgeB = clamp(b, vec2(0.0), size);
    float distanceA = length(a - edgeA), distanceB = length(b - edgeB);
    float across = distanceA / max(distanceA + distanceB, 0.0001);
    float mixAmount = smoothstep(0.0, 1.0, across);
    if (style == 0) {
        // Pull the incoming upper facing corner toward the outgoing boundary.
        // At either canonical boundary its wallpaper sample is unchanged.
        float incomingEdge = direction > 0.0 ? 0.0 : size.x;
        float taper = 4.0 * progress * (1.0 - progress);
        vec2 bridge = mix(edgeB, vec2(incomingEdge, 0.0), (1.0 - across) * taper);
        color = vec4(mix(wall(sourceWall, edgeA, 0.0).rgb,
            wall(destinationWall, bridge, 0.0).rgb, mixAmount), 1.0);
    } else {
        // Copies grow away from their original boundaries. Both enlargement and
        // blur vanish at the join, avoiding a hard edge on patterned wallpapers.
        float fadeA = smoothstep(0.0, size.x * 0.15, distanceA);
        float fadeB = smoothstep(0.0, size.x * 0.15, distanceB);
        vec2 ea = mix(edgeA, (a - size * 0.5) / 1.5 + size * 0.5, fadeA);
        vec2 eb = mix(edgeB, (b - size * 0.5) / 1.5 + size * 0.5, fadeB);
        color = vec4(mix(wall(sourceWall, ea, blurRadius * fadeA).rgb,
            wall(destinationWall, eb, blurRadius * fadeB).rgb, mixAmount), 1.0);
    }
})";
}  // namespace hyprimate::workspace
