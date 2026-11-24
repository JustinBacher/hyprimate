#pragma once

namespace hyprimate::cloth::hinge_angle {
    // Odd-power coefficients of the Taylor series of asin through x^11.
    inline constexpr float C3 = 1.0F / 6;
    inline constexpr float C5 = 3.0F / 40;
    inline constexpr float C7 = 5.0F / 112;
    inline constexpr float C9 = 35.0F / 1152;
    inline constexpr float C11 = 63.0F / 2816;
}  // namespace hyprimate::cloth::hinge_angle

#if defined(__SSE2__)

#include <cstddef>
#include <span>

#include <xmmintrin.h>

#include "window/cloth/ClothSolver.hpp"

namespace hyprimate::cloth::simd {
    namespace detail {
        struct HingeVector {
            __m128 x, y, z;
        };

        inline HingeVector operator+(HingeVector a, HingeVector b) noexcept {
            return {_mm_add_ps(a.x, b.x), _mm_add_ps(a.y, b.y), _mm_add_ps(a.z, b.z)};
        }

        inline HingeVector operator-(HingeVector a, HingeVector b) noexcept {
            return {_mm_sub_ps(a.x, b.x), _mm_sub_ps(a.y, b.y), _mm_sub_ps(a.z, b.z)};
        }

        inline HingeVector operator*(HingeVector a, __m128 b) noexcept {
            return {_mm_mul_ps(a.x, b), _mm_mul_ps(a.y, b), _mm_mul_ps(a.z, b)};
        }

        inline HingeVector operator/(HingeVector a, __m128 b) noexcept {
            return {_mm_div_ps(a.x, b), _mm_div_ps(a.y, b), _mm_div_ps(a.z, b)};
        }

        inline __m128 dot(HingeVector a, HingeVector b) noexcept {
            return _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(a.x, b.x), _mm_mul_ps(a.y, b.y)), _mm_mul_ps(a.z, b.z));
        }

        inline HingeVector cross(HingeVector a, HingeVector b) noexcept {
            return {_mm_sub_ps(_mm_mul_ps(a.y, b.z), _mm_mul_ps(a.z, b.y)),
                _mm_sub_ps(_mm_mul_ps(a.z, b.x), _mm_mul_ps(a.x, b.z)),
                _mm_sub_ps(_mm_mul_ps(a.x, b.y), _mm_mul_ps(a.y, b.x))};
        }

        inline __m128 select(__m128 mask, __m128 yes, __m128 no) noexcept {
            return _mm_or_ps(_mm_and_ps(mask, yes), _mm_andnot_ps(mask, no));
        }

        template <typename Hinge>
        inline HingeVector gather_positions(std::span<Point> positions,
            std::span<const Hinge, 4> hinges,
            std::size_t vertex) noexcept {
            const Point &a = positions[hinges[0].vertices[vertex]];
            const Point &b = positions[hinges[1].vertices[vertex]];
            const Point &c = positions[hinges[2].vertices[vertex]];
            const Point &d = positions[hinges[3].vertices[vertex]];
            return {_mm_setr_ps(a.x, b.x, c.x, d.x),
                _mm_setr_ps(a.y, b.y, c.y, d.y),
                _mm_setr_ps(a.z, b.z, c.z, d.z)};
        }

        template <typename Hinge>
        inline __m128 gather_masses(std::span<const float> masses,
            std::span<const Hinge, 4> hinges,
            std::size_t vertex) noexcept {
            return _mm_setr_ps(masses[hinges[0].vertices[vertex]],
                masses[hinges[1].vertices[vertex]],
                masses[hinges[2].vertices[vertex]],
                masses[hinges[3].vertices[vertex]]);
        }

        template <typename Hinge>
        inline void scatter_positions(std::span<Point> positions,
            std::span<const Hinge, 4> hinges,
            std::size_t vertex,
            HingeVector value,
            int active) noexcept {
            float x[4], y[4], z[4];
            _mm_storeu_ps(x, value.x);
            _mm_storeu_ps(y, value.y);
            _mm_storeu_ps(z, value.z);
            for (std::size_t lane = 0; lane < 4; ++lane) {
                if ((active & (1 << lane)) != 0) {
                    positions[hinges[lane].vertices[vertex]] = {x[lane], y[lane], z[lane]};
                }
            }
        }
    }  // namespace detail

    // All four hinges must have disjoint vertices, including within each hinge.
    template <typename Hinge>
    inline void solve_hinges4(std::span<Point> positions,
        std::span<const float> inverse_masses,
        std::span<const Hinge, 4> hinges,
        float bend_alpha,
        float pi) noexcept {
        using namespace detail;
        const __m128 zero = _mm_setzero_ps();
        const __m128 one = _mm_set1_ps(1.0F);
        const __m128 sign_bit = _mm_set1_ps(-0.0F);
        const HingeVector low = gather_positions(positions, hinges, 0);
        const HingeVector high = gather_positions(positions, hinges, 1);
        const HingeVector left = gather_positions(positions, hinges, 2);
        const HingeVector right = gather_positions(positions, hinges, 3);
        const HingeVector edge = high - low;
        const HingeVector left_tip = left - high;
        const HingeVector right_tip = right - high;
        HingeVector left_normal = cross(left - low, left_tip);
        HingeVector right_normal = cross(right_tip, right - low);
        const __m128 left_area = dot(left_normal, left_normal);
        const __m128 right_area = dot(right_normal, right_normal);
        const __m128 area_product = _mm_mul_ps(left_area, right_area);
        const __m128 length = _mm_sqrt_ps(dot(edge, edge));
        const __m128 invalid = _mm_or_ps(_mm_cmplt_ps(length, _mm_set1_ps(1e-6F)),
            _mm_cmplt_ps(area_product, _mm_set1_ps(1e-24F)));
        if (_mm_movemask_ps(invalid) == 15) {
            return;
        }

        const __m128 cosine = _mm_div_ps(
            dot(left_normal, right_normal), _mm_sqrt_ps(select(invalid, one, area_product)));
        const __m128 abs_cosine = _mm_min_ps(one, _mm_andnot_ps(sign_bit, cosine));
        using namespace hinge_angle;
        const __m128 s = _mm_mul_ps(_mm_sub_ps(one, abs_cosine), _mm_set1_ps(.5F));
        __m128 series = _mm_set1_ps(C11);
        series = _mm_add_ps(_mm_set1_ps(C9), _mm_mul_ps(s, series));
        series = _mm_add_ps(_mm_set1_ps(C7), _mm_mul_ps(s, series));
        series = _mm_add_ps(_mm_set1_ps(C5), _mm_mul_ps(s, series));
        series = _mm_add_ps(_mm_set1_ps(C3), _mm_mul_ps(s, series));
        series = _mm_add_ps(one, _mm_mul_ps(s, series));
        const __m128 half_sine = _mm_sqrt_ps(s);
        const __m128 magnitude = _mm_mul_ps(_mm_add_ps(half_sine, half_sine), series);
        const __m128 sign = select(_mm_cmplt_ps(dot(cross(right_normal, left_normal), edge), zero),
            _mm_set1_ps(-1.0F),
            one);
        const __m128 angle = _mm_mul_ps(sign,
            select(_mm_cmplt_ps(cosine, zero), _mm_sub_ps(_mm_set1_ps(pi), magnitude), magnitude));
        __m128 bend = _mm_sub_ps(angle,
            _mm_setr_ps(hinges[0].restAngle,
                hinges[1].restAngle,
                hinges[2].restAngle,
                hinges[3].restAngle));
        const __m128 above = _mm_cmpgt_ps(bend, _mm_set1_ps(pi));
        const __m128 below = _mm_cmplt_ps(bend, _mm_set1_ps(-pi));
        bend = select(above,
            _mm_sub_ps(bend, _mm_set1_ps(2 * pi)),
            select(below, _mm_add_ps(bend, _mm_set1_ps(2 * pi)), bend));
        const __m128 inactive = _mm_or_ps(invalid, _mm_cmpeq_ps(bend, zero));
        const int active = (~_mm_movemask_ps(inactive)) & 15;
        if (active == 0) {
            return;
        }

        left_normal = left_normal / select(inactive, one, left_area);
        right_normal = right_normal / select(inactive, one, right_area);
        const HingeVector left_grad = left_normal * length;
        const HingeVector right_grad = right_normal * length;
        const HingeVector low_grad =
            (left_normal * dot(left_tip, edge) + right_normal * dot(right_tip, edge)) /
            select(inactive, one, length);
        const HingeVector high_grad =
            HingeVector{zero, zero, zero} - low_grad - left_grad - right_grad;
        const __m128 inv_low = gather_masses(inverse_masses, hinges, 0);
        const __m128 inv_high = gather_masses(inverse_masses, hinges, 1);
        const __m128 inv_left = gather_masses(inverse_masses, hinges, 2);
        const __m128 inv_right = gather_masses(inverse_masses, hinges, 3);
        // Keep the scalar XPBD denominator's left-to-right reduction in each lane.
        __m128 denominator = _mm_mul_ps(inv_low, dot(low_grad, low_grad));
        denominator = _mm_add_ps(denominator, _mm_mul_ps(inv_high, dot(high_grad, high_grad)));
        denominator = _mm_add_ps(denominator, _mm_mul_ps(inv_left, dot(left_grad, left_grad)));
        denominator = _mm_add_ps(denominator, _mm_mul_ps(inv_right, dot(right_grad, right_grad)));
        denominator = _mm_add_ps(denominator, _mm_set1_ps(bend_alpha));
        const __m128 scale =
            _mm_div_ps(_mm_xor_ps(bend, sign_bit), select(inactive, one, denominator));
        scatter_positions(
            positions, hinges, 0, low + low_grad * _mm_mul_ps(scale, inv_low), active);
        scatter_positions(
            positions, hinges, 1, high + high_grad * _mm_mul_ps(scale, inv_high), active);
        scatter_positions(
            positions, hinges, 2, left + left_grad * _mm_mul_ps(scale, inv_left), active);
        scatter_positions(
            positions, hinges, 3, right + right_grad * _mm_mul_ps(scale, inv_right), active);
    }
}  // namespace hyprimate::cloth::simd

#endif
