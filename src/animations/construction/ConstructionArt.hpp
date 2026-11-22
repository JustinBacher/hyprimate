#pragma once

#include "ConstructionBatch.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate::construction {

using Tint = std::array<unsigned char, 4>;
inline constexpr std::array<float, 4> UNIT_UV{0, 0, 1, 1};

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 mix(Vec3 a, Vec3 b, double t) noexcept { return a + (b - a) * t; }
constexpr double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }
inline Vec3 unit(Vec3 a, Vec3 fallback) noexcept {
    const double l = length(a);
    return l > 1e-6 ? a * (1.0 / l) : fallback;
}
constexpr std::array<float, 3> floats(Vec3 a) noexcept {
    return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)};
}

inline Tint tint(Tint opaque, double opacity = 1.0) noexcept {
    const double a = std::clamp(opacity, 0.0, 1.0);
    for (auto &channel : opaque) channel = static_cast<unsigned char>(std::lround(channel * a));
    return opaque;
}

inline void quad(ConstructionBatch &batch, Vec3 origin, Vec3 u, Vec3 v, Tint color,
    ConstructionSurface surface = ConstructionSurface::Solid, std::array<float, 4> uv = UNIT_UV) noexcept {
    if (color[3] != 0) batch.add({floats(origin), floats(u), floats(v), uv, color, surface});
}

// 8×4 64-texel cells; only the centered 56×56 rectangle is ever sampled, top first.
// Artwork anchors are cell-relative texels, not normalized UVs or padded rectangle coordinates.
struct Part {
    int col, row;
    Vec2 pivot;
    Vec2 tip{};
};
inline constexpr int ATLAS_WIDTH = 512, ATLAS_HEIGHT = 256;
inline constexpr Part HEAD{0, 0, {34, 53}, {27, 9}}, TORSO{1, 0, {29, 53}, {31, 12}};
inline constexpr Part NEAR_SLEEVE{2, 0, {31, 18}, {32, 47}}, NEAR_FOREARM{3, 0, {31, 17}, {31, 47}};
inline constexpr Part FAR_SLEEVE{4, 0, {31, 17}, {31, 47}}, FAR_FOREARM{5, 0, {31, 17}, {31, 47}};
inline constexpr Part NEAR_THIGH{6, 0, {31, 17}, {31, 47}}, NEAR_SHIN{7, 0, {31, 18}, {31, 48}};
inline constexpr Part FAR_THIGH{0, 1, {31, 17}, {31, 47}}, FAR_SHIN{1, 1, {31, 17}, {31, 47}};
inline constexpr Part NEAR_BOOT{2, 1, {26, 26}, {26, 53}}, FAR_BOOT{3, 1, {26, 26}, {26, 52}};
inline constexpr Part GLOVE{4, 1, {20, 33}, {55, 33}}, HAMMER{5, 1, {32, 46}, {52, 19}};
inline constexpr Part FASTENER{6, 1, {33, 30}}, TIMBER{7, 1, {32, 13}};
inline constexpr Part CHASSIS{0, 2, {32, 55}}, CAB{1, 2, {29, 52}};
inline constexpr Part SHEAVE{2, 2, {32, 32}}, HOOK{3, 2, {32, 9}, {44, 47}};
inline constexpr Part COUNTERWEIGHT{4, 2, {27, 52}}, SLING_EYE{5, 2, {33, 19}};
inline constexpr Part SOFT_SHADOW{6, 2, {32, 32}}, CONTACT_SHADOW{7, 2, {32, 32}};

inline void stamp(ConstructionBatch &batch, Part part, Vec3 pivot, Vec3 x, Vec3 y, double opacity = 1.0) noexcept {
    const auto uv = std::array<float, 4>{static_cast<float>(part.col * 64 + 4) / ATLAS_WIDTH,
        static_cast<float>(part.row * 64 + 4) / ATLAS_HEIGHT, 56.0F / ATLAS_WIDTH, 56.0F / ATLAS_HEIGHT};
    quad(batch, pivot + x * (4 - part.pivot.x) + y * (4 - part.pivot.y), x * 56, y * 56,
        tint({255, 255, 255, 255}, opacity), ConstructionSurface::Atlas, uv);
}

inline void stamp(ConstructionBatch &batch, Part part, Vec3 pivot, double pixel, double opacity = 1.0) noexcept {
    stamp(batch, part, pivot, {pixel, 0, 0}, {0, pixel, 0}, opacity);
}

// Map both authored anchors, including the diagonal grip→striking face on the hammer.
inline void stamp_link(ConstructionBatch &batch, Part part, Vec3 pivot, Vec3 tip, double opacity = 1.0) noexcept {
    const Vec3 delta = tip - pivot;
    const Vec3 side = unit({delta.y, -delta.x, 0}, {1, 0, 0}) * length(delta);
    const double dx = part.tip.x - part.pivot.x, dy = part.tip.y - part.pivot.y;
    const double inverse = 1 / (dx * dx + dy * dy);
    stamp(batch, part, pivot, (delta * dx + side * dy) * inverse, (delta * dy - side * dx) * inverse, opacity);
}

}  // namespace hyprimate::construction
