#include <limits>
#include <numbers>

#include "RegionalClosePrivate.hpp"

namespace hyprimate::construction::studies {
    namespace {
        using namespace regional_close_detail;

        constexpr double MIN_DUST_START = .56;
        constexpr double PI = std::numbers::pi_v<double>;

        bool finite_coordinate(double v) noexcept {
            // Keep normalization and interpolation well inside the logical pixel range.
            return std::abs(v) <= 0x1p24;
        }

        bool valid(Box b) noexcept {
            return is_usable(b) && finite_coordinate(b.x) && finite_coordinate(b.y) &&
                   finite_coordinate(b.x + b.w) && finite_coordinate(b.y + b.h) &&
                   b.x + b.w > b.x && b.y + b.h > b.y;
        }

        bool valid(Vec3 p) noexcept {
            return finite_coordinate(p.x) && finite_coordinate(p.y) && finite_coordinate(p.z);
        }

        Box intersection(Box a, Box b) noexcept {
            const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
            const auto extent = [](double lo, double hi) {
                const double distance = hi - lo;
                // Rounding the extent may otherwise put its reconstituted endpoint outside the
                // crop.
                return distance > 0 && lo + distance > hi ? std::nextafter(distance, 0.0)
                                                          : distance;
            };
            return {x,
                y,
                extent(x, std::min(a.x + a.w, b.x + b.w)),
                extent(y, std::min(a.y + a.h, b.y + b.h))};
        }

        template <std::size_t CAPACITY>
        std::size_t grid_axis(std::array<double, CAPACITY> &lines,
            double origin,
            double length,
            std::size_t cells) noexcept {
            for (std::size_t i = 0; i <= cells; ++i)
                lines[i] = origin + length * (double(i) / double(cells));
            return cells + 1;
        }

        template <std::size_t CAPACITY>
        std::size_t sort_grid(std::array<double, CAPACITY> &lines, std::size_t count) noexcept {
            const auto end = lines.begin() + static_cast<std::ptrdiff_t>(count);
            std::sort(lines.begin(), end);
            return static_cast<std::size_t>(std::unique(lines.begin(), end) - lines.begin());
        }

    }  // namespace

    double RegionalClose::Drive::at(double travel) const noexcept {
        return start + std::sqrt(travel / rate);
    }

    double RegionalClose::Drive::drop(double time) const noexcept {
        const double age = std::max(0.0, time - start);
        return rate * age * age;
    }

    void RegionalClose::cancel() noexcept {
        m_duration = 0;
        m_progress = 0;
        m_patch_count = 0;
        m_region_count = 0;
    }

    void RegionalClose::advance(double elapsed, bool stalled) noexcept {
        if (!active()) return;
        if (stalled || !std::isfinite(elapsed) || elapsed < 0 || elapsed > STALL_SECONDS ||
            elapsed >= m_duration * (1 - m_progress)) {
            cancel();
            return;
        }
        m_progress += elapsed / m_duration;
        if (m_progress >= 1 - 1e-12) cancel();
    }

    bool RegionalClose::start(Box body,
        std::span<const VisibleFace> visible,
        std::uint64_t seed,
        double duration, double worker_size, Box worker_body) noexcept {
        cancel();
        if (!valid(body) || visible.empty() || visible.size() > MAX_VISIBLE_FACES ||
            !std::isfinite(duration) || duration <= 0)
            return false;

        m_body = body;
        m_scale = std::min(body.w, body.h);
        const Box crew_body = valid(worker_body) ? worker_body : body;
        const double crew_height = .075 * std::min(crew_body.w, crew_body.h) * worker_size;
        double floor = body.y + body.h;
        double top = body.y;
        m_x_count = grid_axis(m_x_edges, body.x, body.w, COLUMNS);
        m_y_count = grid_axis(m_y_edges, body.y, body.h, ROWS);
        for (const auto &face : visible) {
            if (!valid(face.source) || !valid(face.origin) || !valid(face.u) || !valid(face.v))
                return false;
            const Box crop = intersection(face.source, body);
            if (!valid(crop)) continue;
            for (std::size_t i = 0; i < m_patch_count; ++i)
                if (valid(intersection(m_patches[i].visible, crop))) return false;
            for (double x : {crop.x, crop.x + crop.w}) {
                for (double y : {crop.y, crop.y + crop.h}) {
                    const auto corner = rest_point(face, {x, y});
                    if (!valid(corner)) return false;
                    floor = std::max(floor, corner.y);
                    top = std::min(top, corner.y);
                }
            }
            m_patches[m_patch_count++] = {face, crop};
            m_x_edges[m_x_count++] = crop.x;
            m_x_edges[m_x_count++] = crop.x + crop.w;
            m_y_edges[m_y_count++] = crop.y;
            m_y_edges[m_y_count++] = crop.y + crop.h;
        }
        if (!m_patch_count) return false;
        // Include every visible patch edge in one lattice. Even a long edge
        // meeting two shorter ones has the same piecewise-linear boundary.
        m_x_count = sort_grid(m_x_edges, m_x_count);
        m_y_count = sort_grid(m_y_edges, m_y_count);
        // The printed sheet packs into this low toe; no client image is drawn on rubble.
        m_floor = std::max(
            floor + .016 * m_scale, std::nextafter(floor, std::numeric_limits<double>::infinity()));
        // Even an unusually high in-flight face must reach the toe before the final empty frame.
        m_fall_distance = std::max(body.h, (m_floor - top) / .98);

        auto origin_seed = seed ^ std::uint64_t{749};
        const double mark = .960 - .920 * unit_draw(origin_seed);
        const double tallness = smoothstep(1.08, 1.59, body.h / body.w);
        const auto bite = [tallness](double u) {
            return tallness * smoothstep(.09, .42, std::abs(u - .5));
        };
        const Vec2 wanted{
            body.x + mark * body.w, body.y + body.h - (body.h * .26 * bite(mark) + .045 * m_scale)};

        // When closing incomplete work, move the cue onto actually present print.
        Vec2 cue_source;
        double nearest = std::numeric_limits<double>::infinity();
        std::size_t cue_patch = 0;
        for (std::size_t i = 0; i < m_patch_count; ++i) {
            const auto &b = m_patches[i].visible;
            const double inset_x = std::min(.04 * b.w, 4.0), inset_y = std::min(.04 * b.h, 4.0);
            const Vec2 candidate{std::clamp(wanted.x, b.x + inset_x, b.x + b.w - inset_x),
                std::clamp(wanted.y, b.y + inset_y, b.y + b.h - inset_y)};
            const double dx = candidate.x - wanted.x, dy = candidate.y - wanted.y;
            const double distance = dx * dx + dy * dy;
            if (distance >= nearest) continue;
            nearest = distance;
            cue_patch = i;
            cue_source = candidate;
        }
        m_cue_source = cue_source;
        m_cue = rest_point(m_patches[cue_patch].face, cue_source);
        m_origin = (cue_source.x - body.x) / body.w;
        m_bite = bite(m_origin);
        m_fold = 2 * PI * unit_draw(seed);

        // Broad irregular masses, independent of any opening pieces. These intervals
        // affect timing only; all the intervening image vertices remain joined.
        m_region_count = 3 + static_cast<std::size_t>(3 * unit_draw(seed));
        const auto distribute = [&](std::size_t first, std::size_t count, double lo, double hi) {
            std::array<double, MAX_REGIONS> weights;
            double total = 0;
            for (std::size_t j = 0; j < count; ++j)
                total += (weights[j] = .80 + .40 * unit_draw(seed));
            m_edges[first] = lo;
            for (std::size_t j = 1; j < count; ++j) {
                lo += (hi - m_edges[first]) * (weights[j - 1] / total);
                m_edges[first + j] = lo;
            }
            m_edges[first + count] = hi;
        };
        std::size_t root = 0;
        if (m_origin <= .23) {
            m_edges[0] = 0;
            distribute(1, m_region_count - 1, .29 + .05 * unit_draw(seed), 1);
        } else if (m_origin >= .77) {
            root = m_region_count - 1;
            distribute(0, root, 0, .66 + .05 * unit_draw(seed));
            m_edges[m_region_count] = 1;
        } else {
            const double lo = std::max(.16, m_origin - .14 - .03 * unit_draw(seed));
            const double hi = std::min(.84, m_origin + .14 + .03 * unit_draw(seed));
            root = static_cast<std::size_t>(
                std::clamp(std::round(double(m_region_count - 1) * lo / (1 - hi + lo)),
                    1.0,
                    double(m_region_count - 2)));
            distribute(0, root, 0, lo);
            distribute(root + 1, m_region_count - root - 1, hi, 1);
        }

        const double root_rate = 7.4 + .8 * unit_draw(seed);
        auto &primary = m_regions[root];
        primary.lower = {CUE + .018 + .006 * unit_draw(seed), root_rate};
        primary.upper = {std::lerp(primary.lower.start, primary.lower.at(.17), m_bite),
            root_rate * (1 + .46 * m_bite)};
        for (std::size_t depth = 1; depth < m_region_count; ++depth) {
            for (int side : {-1, 1}) {
                const int index = int(root) + int(depth) * side;
                if (index < 0 || index >= int(m_region_count)) continue;
                auto &region = m_regions[static_cast<std::size_t>(index)];
                const auto &parent = m_regions[static_cast<std::size_t>(index - side)];
                const double branch =
                    root == 0 || root + 1 == m_region_count ? 1.0 : (side < 0 ? 1.12 : .92);
                const double lead = (depth == 1 ? .209 : .124) * (m_scale / body.h) * branch;
                const double lower_start = parent.lower.at(lead * (.93 + .14 * unit_draw(seed)));
                const double lower_finish = .78 + .018 * double(depth);
                const double lower_window = lower_finish - lower_start;
                region.lower = {lower_start,
                    std::max(root_rate * (1.45 + .14 * double(depth)),
                        1.13 / (lower_window * lower_window))};
                const double upper_start = std::lerp(
                    lower_start, std::max(region.lower.at(.11), parent.upper.at(.060)), m_bite);
                const double upper_window = .83 + .014 * double(depth) - upper_start;
                region.upper = {
                    upper_start, std::max(region.lower.rate, 1.13 / (upper_window * upper_window))};
            }
        }

        for (std::size_t i = 0; i < m_region_count; ++i) {
            auto &region = m_regions[i];
            const double left = body.x + m_edges[i] * body.w,
                         right = body.x + m_edges[i + 1] * body.w;
            const double dust_center = std::lerp(left, right, .37 + .26 * unit_draw(seed));
            double lowest = -std::numeric_limits<double>::infinity();
            region.has_dust = false;
            for (std::size_t j = 0; j < m_patch_count; ++j) {
                const auto &patch = m_patches[j];
                const auto &b = patch.visible;
                const double lo = std::max(left, b.x), hi = std::min(right, b.x + b.w);
                if (lo >= hi) continue;
                const auto contact =
                    rest_point(patch.face, {std::clamp(dust_center, lo, hi), b.y + b.h});
                if (lowest >= contact.y) continue;
                lowest = contact.y;
                region.has_dust = true;
                region.dust_x = std::lerp(contact.x, m_cue.x, .075);
            }
            region.dust_start = std::max(MIN_DUST_START, region.lower.at(.53));
        }

        // Open the take on a planted checker and a flagger. Keep the checked glove on
        // an actual edge, including when that edge belongs to an interrupted placement.
        const double flank = m_origin < .5 ? -1 : 1;
        for (std::size_t i = 0; i < m_stations.size(); ++i) {
            const double side = i == 0 ? flank : -flank;
            const Patch *edge_patch = &m_patches[cue_patch];
            const auto edge = [side](Box b) { return side < 0 ? b.x : b.x + b.w; };
            for (std::size_t j = 0; j < m_patch_count; ++j)
                if (side * (edge(m_patches[j].visible) - edge(edge_patch->visible)) > 0)
                    edge_patch = &m_patches[j];
            const double height = crew_height * ((38 + 7 * unit_draw(seed)) / 42);
            const auto b = edge_patch->visible;
            const double foot_y = b.y + b.h;
            const double gap = (i == 0 ? .19 : .28) * height;
            const Vec3 check = rest_point(edge_patch->face,
                {edge(b) - side * std::min(2.0, .025 * b.w),
                    std::clamp(foot_y + gap - .665 * height, b.y, foot_y)});
            const Vec3 floor_at_edge = rest_point(edge_patch->face, {edge(b), foot_y});
            m_stations[i] = {{floor_at_edge.x + side * (i == 0 ? .56 : 1.85) * height,
                                 floor_at_edge.y + gap,
                                 floor_at_edge.z},
                check,
                side,
                height};
        }
        m_duration = duration;
        return true;
    }

    void RegionalClose::emit_crew(Frame &out) const noexcept {
        if (m_progress >= .285) return;
        for (std::size_t i = 0; i < m_stations.size(); ++i) {
            const auto &station = m_stations[i];
            const double s = station.height, flank = station.flank;
            if (s == 0) continue;
            const double turn = i == 0 ? .075 : .122;
            const double depart = turn + .027;
            const double travel = smoothstep(depart, .285, m_progress);
            const double facing = m_progress < turn + .013 ? -flank : flank;
            const auto walk = [&](double q) {
                return Vec3{station.floor.x + flank * 2.95 * s * q,
                    station.floor.y + 1.20 * s * q,
                    station.floor.z + .43 * s * q};
            };
            auto &actor = out.m_crew[out.m_crew_count++];
            actor.floor = walk(travel);
            actor.height = s;
            actor.facing = facing;
            actor.opacity = 1 - smoothstep(.88, 1.0, travel);

            // Alternating landings in site space. The non-swinging sole stays on
            // its previous floor anchor as the pelvis moves past it.
            constexpr int STEPS = 7;
            const double q = travel * STEPS;
            const int step = std::min(STEPS - 1, int(q));
            const double stride = q - step;
            const auto landing = [](int n) { return std::clamp((n + 1.38) / STEPS, 0., 1.); };
            for (int foot = 0; foot < 2; ++foot) {
                const bool swinging = foot == step % 2;
                const double anchor =
                    swinging ? std::lerp(landing(step - 2), landing(step), smoothstep(0, 1, stride))
                             : landing(step - 1);
                auto &sole = actor.soles[static_cast<std::size_t>(foot)];
                sole = walk(anchor);
                sole.x += (foot == 0 ? .115 : -.115) * flank * s;
                if (swinging) sole.y -= .17 * s * std::sin(PI * stride);
            }
            const double running = smoothstep(turn, depart + .02, m_progress);
            const auto body = [&](double x, double y, double z = 0.) {
                return Vec3{
                    actor.floor.x + facing * s * x, actor.floor.y + s * y, actor.floor.z + s * z};
            };
            const double bounce = running * std::sin(PI * stride);
            const double gait = running * std::sin(PI * q);
            actor.hip = body(.022, -.445 - .030 * bounce);
            actor.shoulder = body(.075 + .11 * running, -.754 - .030 * bounce);
            actor.head = body(.10 + .11 * running, -.94 - .030 * bounce);
            actor.near_hand = body(.19 - .209 * gait, -.449 - .076 * running, .076);
            actor.far_hand = body(-.07 + .209 * gait, -.442 - .053 * running, -.047);
            if (m_progress >= turn) continue;
            if (i == 0) {
                const double reach = 1 - smoothstep(.020, turn, m_progress);
                actor.near_hand = mix(actor.near_hand, station.check, reach);
            } else {
                const double p = m_progress / turn;
                const auto raised = body(.32, -1.016, .076), swept = body(.55, -.755, .085);
                if (p < .53)
                    actor.near_hand = mix(actor.near_hand, raised, smoothstep(.04, .31, p));
                else if (p < .80)
                    actor.near_hand = mix(raised, swept, smoothstep(.53, .80, p));
                else
                    actor.near_hand = mix(swept, actor.near_hand, smoothstep(.80, 1, p));
            }
        }
    }

    void RegionalClose::emit(SourceView view, Frame &out, bool with_dust) const noexcept {
        out.m_vertex_count = out.m_face_count = out.m_crew_count = out.m_dust_count = 0;
        out.flash.reset();
        if (!active() || !view.authorized || !valid(view.crop)) return;
        const double crop_right = view.crop.x + view.crop.w;
        const double crop_bottom = view.crop.y + view.crop.h;

        if (!emit_mesh(view, out, crop_right, crop_bottom)) return;

        emit_crew(out);
        if (CUE <= m_progress && m_progress < CUE + .040 && m_cue_source.x >= view.crop.x &&
            m_cue_source.x < crop_right && m_cue_source.y >= view.crop.y &&
            m_cue_source.y < crop_bottom) {
            out.flash = Flash{m_cue,
                std::clamp(.022 * m_scale, 2., 10.),
                1 - smoothstep(CUE + .009, CUE + .040, m_progress)};
        }
        if (!with_dust || m_progress <= MIN_DUST_START) return;
        for (std::size_t i = 0; i < m_region_count; ++i) {
            const auto &region = m_regions[i];
            if (!region.has_dust) continue;
            const double life = std::min(.20, 1 - region.dust_start);
            const double age = m_progress - region.dust_start;
            if (age <= 0 || age >= life) continue;
            const double q = age / life;
            const double width = std::min(34., .074 * m_scale);
            out.m_dust[out.m_dust_count++] = {{region.dust_x, m_floor - width * (.06 + .36 * q)},
                {width * (.4 + .6 * q), width * (.16 + .27 * q)},
                .27 * smoothstep(0, .20, q) * (1 - smoothstep(.36, 1, q))};
        }
    }

}  // namespace hyprimate::construction::studies
