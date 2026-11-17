#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animations/swarm/SwarmSprite.hpp"

namespace hyprimate {
    std::string spritePath(std::string_view value, std::string_view directory) {
        if (value.empty() || value.find('/') != std::string_view::npos) {
            return std::string(value);
        }
        std::string result(directory);
        result += '/';
        result += value;
        if (!value.ends_with(".png")) {
            result += ".png";
        }
        return result;
    }

    namespace {

        constexpr std::uint8_t kSolid = 128;
        constexpr double kFar = 1e18;
        constexpr double kCoverMargin =
            1.06;  // the core overshoots its tile slightly, so coverage is complete before lock 1

        // One pass of the exact squared Euclidean distance transform (lower envelope of parabolas)
        // over n samples with the given stride. `v` and `z` are scratch of n and n + 1 entries,
        // `out` of n.
        void distancePass(double *f,
            std::size_t stride,
            int n,
            std::vector<int> &v,
            std::vector<double> &z,
            std::vector<double> &out) {
            const auto at = [&](int i) { return f[static_cast<std::size_t>(i) * stride]; };
            int k = 0;
            v[0] = 0;
            z[0] = -kFar;
            z[1] = kFar;
            for (int q = 1; q < n; ++q) {
                double s = 0.0;
                while (true) {
                    const int p = v[k];
                    s = ((at(q) + (double(q) * q)) - (at(p) + (double(p) * p))) / (2.0 * (q - p));
                    if (s > z[k] || k == 0) {
                        break;
                    }
                    --k;
                }
                if (s > z[k]) {
                    ++k;
                }
                v[k] = q;
                z[k] = s;
                z[k + 1] = kFar;
            }
            k = 0;
            for (int q = 0; q < n; ++q) {
                while (z[k + 1] < q) {
                    ++k;
                }
                const int p = v[k];
                out[q] = (double(q - p) * (q - p)) + at(p);
            }
            for (int q = 0; q < n; ++q) {
                f[static_cast<std::size_t>(q) * stride] = out[q];
            }
        }

    }  // namespace

    SpriteFrame measureSprite(std::span<const std::uint8_t> alpha, int side) {
        SpriteFrame frame;
        if (side <= 0 || alpha.size() < static_cast<std::size_t>(side) * side) {
            return frame;
        }

        // Squared distance from every texel to the nearest empty one. A ring of empty texels stands
        // in for everything outside the frame.
        const int n = side + 2;
        std::vector<double> field(static_cast<std::size_t>(n) * n, 0.0);
        bool any = false;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                if (alpha[(static_cast<std::size_t>(y) * side) + x] >= kSolid) {
                    field[(static_cast<std::size_t>(y + 1) * n) + x + 1] = kFar;
                    any = true;
                }
            }
        }
        if (!any) {
            return frame;
        }

        std::vector<int> v(n);
        std::vector<double> z(n + 1);
        std::vector<double> out(n);
        for (int y = 0; y < n; ++y) {
            distancePass(field.data() + (static_cast<std::size_t>(y) * n), 1, n, v, z, out);
        }
        for (int x = 0; x < n; ++x) {
            distancePass(field.data() + x, static_cast<std::size_t>(n), n, v, z, out);
        }

        double deepest = 0.0;
        int bx = 0;
        int by = 0;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const double d = field[(static_cast<std::size_t>(y + 1) * n) + x + 1];
                if (d > deepest) {
                    deepest = d;
                    bx = x;
                    by = y;
                }
            }
        }
        double farthest = 0.0;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                if (alpha[(static_cast<std::size_t>(y) * side) + x] >= kSolid) {
                    farthest = std::max(farthest, std::hypot(x - bx, y - by));
                }
            }
        }

        // Texel centres; the solid disc stops half a texel short of the nearest empty centre, and
        // the silhouette runs half a texel past its farthest solid centre.
        frame.centre = {.x = (bx + 0.5) / side, .y = (by + 0.5) / side};
        frame.core = std::max(0.0, std::sqrt(deepest) - 0.5) / side;
        frame.reach = (farthest + 0.5) / side;
        return frame;
    }

    int walkFrame(double travel, double stride, int frames) noexcept {
        if (frames <= 1 || !(stride > 0.0) || !std::isfinite(travel)) {
            return 0;
        }
        const double cycles = travel / stride;
        return std::clamp(static_cast<int>((cycles - std::floor(cycles)) * frames), 0, frames - 1);
    }

    double spriteRotation(double heading, double from, double turn) noexcept {
        return std::clamp(turn, 0.0, 1.0) * std::remainder(heading - from, 2.0 * std::numbers::pi);
    }

    double spriteSize(const SpriteFrame &frame, double fit, double cover, double lock) noexcept {
        if (!(frame.reach > 0.0) || !(frame.core > 0.0) || !(fit > 0.0)) {
            return 0.0;
        }
        const double loose = fit / frame.reach;
        const double whole = std::max(loose, kCoverMargin * cover / frame.core);
        return loose * std::pow(whole / loose, std::clamp(lock, 0.0, 1.0));
    }

}  // namespace hyprimate
