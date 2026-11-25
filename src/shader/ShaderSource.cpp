#include <array>
#include <string>
#include <string_view>

#include "Package.hpp"

namespace hyprimate::shader {
    std::string_view default_vertex() noexcept {
        return R"(out vec2 hypr_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    hypr_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
    }
    std::string shader_source(const Package &package, std::string_view body, bool vertex,
        std::string_view field_helpers) {
        std::string source = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D hypr_source, hypr_destination;
uniform float hypr_progress, hypr_elapsed, hypr_delta, hypr_duration, hypr_scale, hypr_direction;
uniform vec2 hypr_size, hypr_target_size, hypr_pointer;
uniform vec4 hypr_seed, hypr_source_uv, hypr_domain, hypr_destination_uv;
uniform vec4 hypr_geometry, hypr_previous_geometry, hypr_velocity;
uniform int hypr_event, hypr_iteration, hypr_iterations;
uniform bool hypr_pointer_valid, hypr_fullscreen, hypr_held, hypr_settling, hypr_blur_mask;
vec4 hypr_premultiply(vec4 c) { return vec4(c.rgb * c.a, c.a); }
vec4 hypr_unpremultiply(vec4 c) { return vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a); }
vec4 hypr_sample_source(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    vec2 half_texel = 0.5 / vec2(textureSize(hypr_source, 0));
    vec2 lo = hypr_source_uv.xy + half_texel;
    vec2 hi = max(lo, hypr_source_uv.xy + hypr_source_uv.zw - half_texel);
    vec2 mapped = clamp(hypr_source_uv.xy + uv * hypr_source_uv.zw, lo, hi);
)";
        source += vertex
                      ? "    return textureLod(hypr_source, mapped, 0.0);\n}\n"
                      : "    return texture(hypr_source, mapped);\n}\nin vec2 hypr_uv;\nvec2 "
                        "hypr_window_uv() { return hypr_domain.xy + hypr_uv * hypr_domain.zw; }\n";
        source += R"(vec4 hypr_sample_destination(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return textureLod(hypr_destination, hypr_destination_uv.xy + uv * hypr_destination_uv.zw, 0.0);
}
)";
        const auto &env = package.environment;
        if (env.used())
            source += R"(
uniform vec2 hypr_output_size, hypr_output_origin;
uniform vec4 hypr_output_rect;
vec2 hypr_environment_uv(vec2 windowUv) { return hypr_output_rect.xy + windowUv * hypr_output_rect.zw; }
bool hypr_inside_output(vec2 uv) {
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}
)";
        if (env.original)
            source += R"(
uniform sampler2D hypr_wallpaper_original;
uniform bool hypr_wallpaper_original_available;
uniform vec4 hypr_wallpaper_uv;
vec4 hypr_sample_wallpaper_original(vec2 uv) {
    if (!hypr_wallpaper_original_available || !hypr_inside_output(uv)) return vec4(0.0);
    vec2 imageUv = hypr_wallpaper_uv.xy + uv * hypr_wallpaper_uv.zw;
    return hypr_inside_output(imageUv) ? textureLod(hypr_wallpaper_original, imageUv, 0.0) : vec4(0.0);
}
)";
        if (env.processed)
            source += R"(
uniform sampler2D hypr_wallpaper_processed;
uniform bool hypr_wallpaper_processed_available;
vec4 hypr_sample_wallpaper_processed(vec2 uv) {
    if (!hypr_wallpaper_processed_available || !hypr_inside_output(uv)) return vec4(0.0);
    return textureLod(hypr_wallpaper_processed, uv, 0.0);
}
)";
        if (env.sample_state)
            source +=
                "uniform sampler2D hypr_environment_state; uniform bool "
                "hypr_environment_state_available;\n";
        if (env.interactions) {
            source += "uniform int hypr_interaction_count; uniform vec4 hypr_interactions[" +
                      std::to_string(MAX_INTERACTIONS * 4) + "];\n";
            if (!env.channel.empty())
                source +=
                    "uniform sampler2D hypr_disturbances; uniform bool "
                    "hypr_disturbances_available;\n";
        }
        constexpr std::array<std::string_view, 6> types{
            "float", "int", "bool", "vec2", "vec3", "vec4"};
        for (const auto &p : package.parameters) {
            const auto type = static_cast<unsigned>(p.initial.type);
            if (type < types.size())
                source += "uniform " + std::string(types[type]) + " " + p.name + ";\n";
        }
        source.append(field_helpers);
        if (!field_helpers.empty()) source += '\n';
        source += "#line 1\n";
        source.append(body);
        return source;
    }
}  // namespace hyprimate::shader
