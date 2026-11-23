#pragma once

#include <array>

namespace hyprimate::construction_shader {

inline constexpr auto VERTEX = std::to_array(R"GLSL(#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec3 a_origin;
layout(location = 1) in vec3 a_u;
layout(location = 2) in vec3 a_v;
layout(location = 3) in vec4 a_uv;
layout(location = 4) in vec4 a_tint;
layout(location = 5) in uint a_surface;

uniform vec3 u_pivot;        // Capture-local projection center and reciprocal focal length.
uniform vec4 u_canvas;       // NDC at the pivot and NDC units per logical pixel.
uniform vec4 u_crop;         // Authorized image edges in capture-local logical pixels.
uniform vec4 u_client_map;   // Capture-local logical coordinates -> image-relative device pixels.
uniform int u_mesh;         // 0: instanced quad; 1: indexed close; 2: indexed close at identity.

out vec2 v_uv;
flat out vec4 v_tint;
flat out uint v_surface;
flat out int v_on_texel_grid;

const uint CLIENT = 1u;
const vec2 CORNERS[4] = vec2[4](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 1));

vec4 project_point(vec3 p) {
    float w = 1.0 - p.z * u_pivot.z;
    // Homogeneous w keeps UV interpolation perspective-correct for both streams.
    return vec4(u_canvas.xy * w + (p.xy - u_pivot.xy) * u_canvas.zw, 0, w);
}

void main() {
    if (u_mesh != 0) {
        // The indexed stream binds just posed xyz and the already clipped source xy.
        v_uv = u_client_map.xy + a_uv.xy * u_client_map.zw;
        v_tint = vec4(1);
        v_surface = CLIENT;
        v_on_texel_grid = int(u_mesh == 2);
        gl_Position = project_point(a_origin);
        return;
    }

    vec2 corner = CORNERS[gl_VertexID];
    vec2 uv = a_uv.xy + corner * a_uv.zw;
    v_tint = a_tint;
    v_surface = a_surface;
    v_on_texel_grid = 0;
    if (a_surface == CLIENT) {
        vec2 first = max((u_crop.xy - a_uv.xy) / a_uv.zw, vec2(0));
        vec2 last = min((u_crop.zw - a_uv.xy) / a_uv.zw, vec2(1));
        if (any(lessThanEqual(a_uv.zw, vec2(0))) || any(lessThanEqual(last, first))) {
            // All four vertices coincide outside the clip volume: no rasterization.
            v_uv = vec2(0);
            gl_Position = vec4(2, 2, 0, 1);
            return;
        }
        // Clip unposed source and posed geometry by the same fractions before projection.
        // Keep unclipped 0/1 corners exact so neighboring installed panels share an edge.
        corner = mix(first, last, corner);
        uv = u_client_map.xy + (a_uv.xy + corner * a_uv.zw) * u_client_map.zw;
        if (all(equal(a_origin, vec3(a_uv.xy, 0))) &&
            all(equal(a_u, vec3(a_uv.z, 0, 0))) &&
            all(equal(a_v, vec3(0, a_uv.w, 0))))
            v_on_texel_grid = 1;
    }
    v_uv = uv;
    gl_Position = project_point(a_origin + corner.x * a_u + corner.y * a_v);
}
)GLSL");

inline constexpr auto FRAGMENT = std::to_array(R"GLSL(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 v_uv;
flat in vec4 v_tint;
flat in uint v_surface;
flat in int v_on_texel_grid;

uniform sampler2D u_image;
uniform sampler2D u_atlas;
uniform vec4 u_sample_bounds;  // Authorized texel centers, common to every client quad.
uniform vec2 u_inverse_image;
uniform ivec3 u_texel_grid;  // X offset, Y offset, row direction (zero for a subpixel grid shift).
uniform bool u_rgbx;
uniform int u_mask_mode;      // 0: color; 1: image matte; 2: synthesized body matte.
uniform vec4 u_mask_body;     // Image-relative device pixel edges, as used by v_uv.
uniform vec2 u_mask_rounding; // Radius in source device pixels, then superellipse exponent.
layout(location = 0) out vec4 color;

const uint CLIENT = 1u;
const uint ATLAS = 2u;
const uint SHADOW = 3u;
const int MASK_NONE = 0;
const int MASK_BODY = 2;

vec4 client_pixel() {
    // A stationary client face has the same source and destination pixel centers, even at
    // fractional output scale. Fetch the exact texel without interpolator/sampler rounding.
    if (v_on_texel_grid != 0 && u_texel_grid.z != 0) {
        ivec2 texel = u_texel_grid.xy + ivec2(gl_FragCoord.xy) * ivec2(1, u_texel_grid.z);
        return texelFetch(u_image, clamp(texel, ivec2(u_sample_bounds.xy),
            ivec2(u_sample_bounds.zw)), 0);
    }

    // Hardware filtering is safe in the interior. At a true crop edge, clamp integer tap
    // indices as well so subtexel/normalized-coordinate rounding cannot read foreign padding.
    vec2 first = u_sample_bounds.xy;
    vec2 last = u_sample_bounds.zw;
    if (all(greaterThan(v_uv, first + 1.0)) && all(lessThan(v_uv, last - 1.0)))
        return textureLod(u_image, v_uv * u_inverse_image, 0.0);

    vec2 p = clamp(v_uv, first, last) - 0.5;
    ivec2 lo = ivec2(floor(p));
    ivec2 hi = min(lo + 1, ivec2(last));
    vec2 t = fract(p);
    vec4 top = mix(texelFetch(u_image, lo, 0), texelFetch(u_image, ivec2(hi.x, lo.y), 0), t.x);
    vec4 bottom = mix(texelFetch(u_image, ivec2(lo.x, hi.y), 0), texelFetch(u_image, hi, 0), t.x);
    return mix(top, bottom, t.y);
}

float body_coverage() {
    if (any(lessThan(v_uv, u_mask_body.xy)) || any(greaterThanEqual(v_uv, u_mask_body.zw)))
        return 0.0;
    float radius = u_mask_rounding.x;
    if (radius == 0.0)
        return 1.0;
    vec2 inset = min(v_uv - u_mask_body.xy, u_mask_body.zw - v_uv);
    vec2 corner = vec2(radius) - inset;
    if (any(lessThanEqual(corner, vec2(0.0))))
        return 1.0; // Straight spans don't need the superellipse.

    float power = u_mask_rounding.y;
    float shape_distance;
    if (power == 2.0) {
        shape_distance = length(corner);
    } else {
        // Factor out the larger axis to keep the powers in a bounded range.
        float major = max(corner.x, corner.y);
        float ratio = min(corner.x, corner.y) / major;
        shape_distance = major * pow(1.0 + pow(ratio, power), 1.0 / power);
    }
    // Match the native rounding ramp, evaluated in the source plane before posing.
    const float EDGE_WIDTH = 3.141592653589793 / 5.34665792551;
    return 1.0 - smoothstep(radius - EDGE_WIDTH, radius + EDGE_WIDTH, shape_distance);
}

void main() {
    if (u_mask_mode != MASK_NONE) {
        if (v_surface != CLIENT)
            discard;
        // The retained close capture is color, not a matte. Use its posed source
        // coordinates without sampling it. For the native matte, red carries coverage.
        float coverage = u_mask_mode == MASK_BODY ? body_coverage() : client_pixel().r;
        // Premultiplied coverage leaves earlier client faces visible through black.
        color = vec4(coverage);
        return;
    }
    if (v_surface == CLIENT) {
        color = client_pixel();
        if (u_rgbx)
            color.a = 1.0;
    } else if (v_surface == ATLAS) {
        color = textureLod(u_atlas, v_uv, 0.0) * v_tint;
    } else if (v_surface == SHADOW) {
        vec2 delta = v_uv * 2.0 - 1.0;
        color = v_tint * (1.0 - smoothstep(0.18, 1.0, dot(delta, delta)));
    } else {
        color = v_tint;
    }
}
)GLSL");

}  // namespace hyprimate::construction_shader
