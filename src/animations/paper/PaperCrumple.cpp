#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <numbers>

#include "animations/paper/PaperCrumple.hpp"

namespace hyprimate {
    namespace {

        constexpr double STEP = 1.0 / 240.0;
        constexpr double PI = std::numbers::pi;
        constexpr std::uint32_t BOUNDARY = std::numeric_limits<std::uint32_t>::max();
        double thickness(const PaperParams &params) {
            return 0.003 * (1 + params.paper_stiffness);
        }

        Vec3 add(Vec3 a, Vec3 b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        Vec3 sub(Vec3 a, Vec3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec3 mul(Vec3 a, double s) {
            return {a.x * s, a.y * s, a.z * s};
        }
        double dot(Vec3 a, Vec3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        double norm(Vec3 a) {
            return std::hypot(a.x, a.y, a.z);
        }
        bool finite(Vec3 a) {
            return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
        }
        double wrap(double a) {
            return std::remainder(a, 2 * PI);
        }

        // The edge is oriented a->b; c belongs to that directed face, d to b->a.
        // Both outward normals agree at rest. The same signed angle is used by Topology.
        double angle(const paper::Topology &mesh, const paper::Hinge &h, bool previous = false) {
            const auto position = [&](std::uint32_t i) {
                return previous ? mesh.nodes[i].prev : mesh.nodes[i].x;
            };
            const Vec3 a = position(h.a);
            const Vec3 e = sub(position(h.b), a);
            const Vec3 n1 = cross(e, sub(position(h.c), a));
            const Vec3 n2 = cross(sub(position(h.d), a), e);
            const double length = norm(e), area = norm(n1) * norm(n2);
            if (length < 1e-10 || area < 1e-15) return 0;
            return std::atan2(dot(mul(e, 1 / length), cross(n1, n2)), dot(n1, n2));
        }

        bool sanitize(PaperParams &p) {
            if (!std::isfinite(p.duration) || p.duration <= 0) return false;
            p.duration = std::clamp(p.duration, 0.1, 3.0);
            for (double *value : {&p.crumpliness,
                     &p.shading,
                     &p.variation,
                     &p.fold_density,
                     &p.paper_stiffness,
                     &p.rotation_intensity,
                     &p.unfold_tension,
                     &p.settling,
                     &p.crease_persistence}) {
                if (!std::isfinite(*value)) return false;
                *value = std::clamp(*value, 0.0, 1.0);
            }
            if (!std::isfinite(p.crumple_size)) return false;
            p.crumple_size = std::clamp(p.crumple_size, 0.04, 0.65);
            return true;
        }

        std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
            return (static_cast<std::uint64_t>(std::min(a, b)) << 32U) | std::max(a, b);
        }

        // Share of an opening tool's travel still ahead at normalized time x: the
        // complement of the Beta(2, 3) distribution. Release starts gently, the rate
        // peaks at one third, and speed and acceleration both reach zero at the end.
        double TUNE(const char *n, double d) {
            const char *v = std::getenv(n);
            return v ? std::atof(v) : d;
        }
        double unfolding_left(double x) {
            x = std::clamp(x, 0.0, 1.0);
            const double r = 1 - x;
            static const double CURVE = TUNE("CURVE", -1);
            if (CURVE < 0) return r * r * r * (1 + 3 * x);
            return r * r * (1 + CURVE * x);
        }

    }  // namespace

    Vec3 PaperCrumple::rest(Vec2 uv) const noexcept {
        return {(uv.x - 0.5) * m_material.x, (uv.y - 0.5) * m_material.y, 0};
    }

    void PaperCrumple::choose_forces(std::uint64_t seed, Vec2 cursor) {
        std::uint64_t base = 0x50A9E5C0FFEEULL;
        const auto draw = [&]() {
            const double fixed = unit_draw(base);
            return fixed + (0.08 + 0.92 * m_params.variation) * (unit_draw(seed) - fixed);
        };
        m_origin = {0.5, 0.5};
        switch (m_params.crumple_origin) {
            case PaperOrigin::Random:
                m_origin = {0.25 + 0.5 * draw(), 0.25 + 0.5 * draw()};
                break;
            case PaperOrigin::Cursor:
                m_origin = cursor;
                break;
            case PaperOrigin::Left:
                m_origin.x = 0;
                break;
            case PaperOrigin::Right:
                m_origin.x = 1;
                break;
            case PaperOrigin::Top:
                m_origin.y = 0;
                break;
            case PaperOrigin::Bottom:
                m_origin.y = 1;
                break;
            case PaperOrigin::Center:
                break;
        }
        for (std::size_t i = 0; i < m_grips.size(); ++i) {
            const double azimuth =
                2 * PI * (static_cast<double>(i) / m_grips.size() + (draw() - 0.5) * 0.28);
            auto &g = m_grips[i];
            g.uv = {std::clamp(0.5 + 0.43 * std::cos(azimuth) + 0.16 * (draw() - 0.5), 0.02, 0.98),
                std::clamp(0.5 + 0.43 * std::sin(azimuth) + 0.16 * (draw() - 0.5), 0.02, 0.98)};
            g.start = static_cast<double>(i) * 0.045 + 0.12 * draw();
            g.angle = azimuth + (draw() - 0.5) * 0.7;
            g.lift = (i % 2 == 0 ? 1 : -1) * (0.55 + 0.45 * draw());
            g.strength = 0.65 + 0.7 * draw();
        }
        for (std::size_t i = 0; i < m_jaws.size(); ++i) {
            const double z =
                -0.94 + 1.88 * (static_cast<double>(i) + 0.35 * draw()) / m_jaws.size();
            const double azimuth =
                static_cast<double>(i) * 2.399963229728653 + 0.8 * (draw() - 0.5);
            const double ring = std::sqrt(1 - z * z);
            m_jaws[i] = {{ring * std::cos(azimuth), ring * std::sin(azimuth), z},
                0.12 + 0.3 * draw(),
                0.83 + 0.23 * draw()};
        }
    }

    void PaperCrumple::begin(Transition transition,
        const PaperParams &params,
        std::uint64_t seed,
        Vec2 size,
        Vec2 cursor_uv,
        std::stop_token stop) {
        if (stop.stop_requested()) {
            reset();
            return;
        }
        PaperParams clean = params;
        if (!sanitize(clean) || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 ||
            size.y <= 0) {
            reset();
            return;
        }
        if (!std::isfinite(cursor_uv.x) || !std::isfinite(cursor_uv.y)) cursor_uv = {0.5, 0.5};
        cursor_uv.x = std::clamp(cursor_uv.x, 0.0, 1.0);
        cursor_uv.y = std::clamp(cursor_uv.y, 0.0, 1.0);
        const bool continuing = !m_topology.nodes.empty() && (active() || closed());
        m_params = clean;
        m_output_size = size;
        if (!continuing) {
            m_size = size;
            // Taking the ratio before sqrt also avoids overflow for large finite capture sizes.
            m_material = {std::sqrt(size.x / size.y), std::sqrt(size.y / size.x)};
            if (!std::isfinite(m_material.x) || !std::isfinite(m_material.y) || m_material.x <= 0 ||
                m_material.y <= 0) {
                reset();
                return;
            }
            m_step = STEP;
            auto initial = clean;
            if (transition == Transition::Open)
                initial.simulation_quality = clean.simulation_quality == PaperQuality::High
                                                 ? PaperQuality::Medium
                                                 : PaperQuality::Low;
            m_topology.initialize(size, seed, initial);
            m_contacts.reset();
            m_fraction = m_previous_fraction = m_accumulator = 0;
            m_tick = 0;
            flatten();
        }
        choose_forces(seed, cursor_uv);
        if (!continuing && transition == Transition::Open) {
            if (!pack(stop)) {
                reset();
                return;
            }
        }
        if (transition == Transition::Open) {
            double height = 0;
            for (auto &node : m_topology.nodes) {
                node.released = node.x;
                height = std::max(height, std::abs(node.x.z));
            }
            // A flap swinging open needs room above the packet before the palm descends.
            m_press_height = height + TUNE("HEADROOM", 0.5) * std::min(m_material.x, m_material.y);
        }
        // A short opening's integration cap must not leak into a subsequent
        // long opening or closing event. Retiming preserves the published pose.
        double next_step = STEP;
        const bool strip = std::max(m_material.x, m_material.y) > 1.8;
        if (transition == Transition::Close && clean.duration < 0.2 && strip) next_step *= 0.25;
        if (transition == Transition::Open)
            next_step =
                std::min(next_step, std::max(clean.duration / 240, 1.0 / (strip ? 960 : 640)));
        m_accumulator *= next_step / m_step;
        m_step = next_step;
        m_phase = transition == Transition::Close ? Phase::Closing : Phase::Opening;
    }
    bool PaperCrumple::pack(std::stop_token stop) {
        // A brief, coarse physical preparation creates this event's own folded
        // packet. It retains only the final state, never frames or a playback path.
        const PaperParams visible = m_params;
        const double visible_step = m_step;
        m_params.duration = 0.2;
        m_params.simulation_quality = PaperQuality::Low;
        m_step = STEP;
        bool prepared = true;
        for (unsigned i = 0; i < 56; ++i) {
            if (stop.stop_requested()) {
                prepared = false;
                break;
            }
            m_fraction = std::min(1.0, static_cast<double>(i + 1) / 48);
            step(true, false);
        }
        m_params = visible;
        m_step = visible_step;
        for (auto &node : m_topology.nodes) {
            node.prev = node.x;
            node.velocity = {};
        }
        m_fraction = m_previous_fraction = 1;
        return prepared && !stop.stop_requested();
    }

    Vec3 PaperCrumple::force(const paper::Node &node) const noexcept {
        Vec3 result{};
        const Vec3 centre = rest(m_origin);
        const double radius = m_params.crumple_size * (1.25 - 0.55 * m_params.crumpliness);
        for (const auto &g : m_grips) {
            const double u = node.uv.x - g.uv.x, v = node.uv.y - g.uv.y;
            const double influence = std::exp(-(u * u + v * v) / 0.038);
            const double load = smoothstep(g.start, std::min(0.94, g.start + 0.58), m_fraction);
            if (influence < 0.005 || load == 0) continue;
            const double turn = m_params.rotation_intensity * load *
                                (m_params.force_direction == PaperForce::Rotational ? 3.5 : 1.2);
            const double azimuth = g.angle + turn * g.lift;
            Vec3 packet = add(centre,
                {radius * std::cos(azimuth), radius * 0.8 * std::sin(azimuth), radius * g.lift});
            if (m_params.force_direction == PaperForce::Directional) {
                packet.x += 0.14 * load;
                packet.z += 0.07 * g.lift;
            }
            const Vec3 target = add(mul(rest(g.uv), 1 - load), mul(packet, load));
            result = add(result, mul(sub(target, node.x), 1400 * influence * g.strength * load));
            // Local alternating normal loads seed buckling before in-plane compression
            // locks a perfectly planar state. They are forces, never rest-geometry noise.
            const double wave = std::sin((u * std::cos(g.angle) + v * std::sin(g.angle)) *
                                             (12 + 30 * m_params.fold_density) +
                                         g.angle);
            result.z += 180 * influence * g.lift * wave * load * (1 - 0.75 * load);
        }
        return result;
    }
    void PaperCrumple::project_enclosure() {
        // Seeded, staggered palm/finger planes form an asymmetric rounded volume.
        // Unlike a box die, no pair of coordinate-aligned walls imposes square sides.
        const double radius = m_params.crumple_size * (1.25 - 0.55 * m_params.crumpliness);
        const Vec3 centre = mul(rest(m_origin), smoothstep(0.15, 0.9, m_fraction));
        for (const auto &jaw : m_jaws) {
            const double load = smoothstep(jaw.start, 0.98, m_fraction);
            if (load == 0) continue;
            const auto n = jaw.normal;
            const double initial =
                0.5 * (std::abs(n.x) * m_material.x + std::abs(n.y) * m_material.y) + 0.05;
            const double final = radius * jaw.scale * (1 + 0.85 * std::abs(n.z));
            const double support = initial * (1 - load) + final * load;
            for (std::size_t i = 0; i < m_topology.nodes.size(); ++i) {
                auto &node = m_topology.nodes[i];
                if (node.inv_mass <= 0) continue;
                const double penetration = dot(sub(node.x, centre), n) - support;
                if (penetration > 0)
                    node.x = sub(node.x, mul(n, 0.3 * m_grip_compliance[i] * penetration));
            }
        }
    }

    void PaperCrumple::project_strain() {
        // Every material edge, including boundaries, gets an actual length constraint.
        // The immutable UV metric is never reconstructed from a compressed world mesh.
        for (const auto &h : m_topology.hinges) {
            auto &a = m_topology.nodes[h.a];
            auto &b = m_topology.nodes[h.b];
            const Vec3 delta = sub(b.x, a.x);
            const double length = norm(delta), weight = a.inv_mass + b.inv_mass;
            if (length < 1e-12 || weight <= 0 || h.length <= 0) continue;
            const double stiffness = 0.88 + 0.115 * m_params.paper_stiffness;
            const Vec3 correction = mul(delta, stiffness * (length - h.length) / (length * weight));
            a.x = add(a.x, mul(correction, a.inv_mass));
            b.x = sub(b.x, mul(correction, b.inv_mass));
        }
    }

    void PaperCrumple::project_bends(double gap) {
        for (std::size_t index = 0; index < m_topology.hinges.size(); ++index) {
            const auto &h = m_topology.hinges[index];
            if (h.d == BOUNDARY) continue;
            auto &a = m_topology.nodes[h.a];
            auto &b = m_topology.nodes[h.b];
            auto &c = m_topology.nodes[h.c];
            auto &d = m_topology.nodes[h.d];
            const Vec3 e = sub(b.x, a.x), r = sub(c.x, a.x), s = sub(d.x, a.x);
            const double e2 = dot(e, e), length = std::sqrt(e2);
            const Vec3 n1 = cross(e, r), n2 = cross(s, e);
            const double area1 = dot(n1, n1), area2 = dot(n2, n2);
            if (e2 < 1e-16 || area1 < 1e-18 || area2 < 1e-18) continue;
            const Vec3 qc = mul(n1, -length / area1), qd = mul(n2, -length / area2);
            const double tc = dot(r, e) / e2, td = dot(s, e) / e2;
            const Vec3 qa = add(mul(qc, tc - 1), mul(qd, td - 1));
            const Vec3 qb = add(mul(qc, -tc), mul(qd, -td));
            const double denominator = a.inv_mass * dot(qa, qa) + b.inv_mass * dot(qb, qb) +
                                       c.inv_mass * dot(qc, qc) + d.inv_mass * dot(qd, qd);
            if (denominator < 1e-12) continue;
            const double theta = std::atan2(dot(mul(e, 1 / length), cross(n1, n2)), dot(n1, n2));
            const double target = h.rest_angle + h.memory;
            // Creased hinges soften, concentrating deformation instead of rubber-sheet bending.
            const double stiffness =
                (0.0008 + 0.004 * m_params.paper_stiffness) / (1 + 2.5 * std::abs(h.memory)) *
                (m_phase == Phase::Opening && !(a.contact || b.contact || c.contact || d.contact)
                        ? 20 * m_step / m_params.duration
                        : 1);
            const bool supported =
                m_phase == Phase::Opening &&
                std::max({std::abs(a.x.z), std::abs(b.x.z), std::abs(c.x.z), std::abs(d.x.z)}) >
                    gap;
            // Counterbending removes the scheduled share of each fold's remaining angle:
            // proportional, so no fold accelerates as the deadline nears. Large folds and
            // pressed hinges lead; small free wrinkles and touching layers follow behind.
            double opening_drive = 0;
            if (m_phase == Phase::Opening) {
                const bool touching = a.contact || b.contact || c.contact || d.contact;
                const double tension = m_params.unfold_tension;
                const double open = std::abs(wrap(theta - h.rest_angle));
                opening_drive = 0.5 * (m_opening_loads[h.a].gain + m_opening_loads[h.b].gain) *
                                (supported ? 1 : std::max(0.35 + 0.3 * tension, smoothstep(0.15, 0.8, open))) *
                                (touching ? 0.4 + 0.3 * tension : 1);
            }
            double wedge = 0;
            if (std::abs(theta) > 2.5) {
                // Shared material hinges cannot be resolved by feature CCD.
                // Finite sheet thickness keeps this adjacent-face wedge open.
                const double altitude = std::sqrt(std::min(area1, area2)) / length;
                const double clearance =
                    std::clamp(2 * thickness(m_params) / std::max(altitude, 1e-8), 0.025, 0.45);
                const double cap = PI - clearance;
                double phase = theta;
                // A wrapped angle can be below the cap after passing pi.
                // Keep its previous signed branch; lazily cache this immutable
                // substep measurement instead of repeating atan2 in each sweep.
                const double before = previous_hinge_angle(index);
                if (std::abs(before) > 2.5 && before * theta < 0)
                    phase += std::copysign(2 * PI, before);
                wedge = (phase - std::clamp(phase, -cap, cap)) * 0.5;
            }
            // External counterbending rotates the flap on its existing side;
            // signed plastic memory is updated only by measured unloading.
            const double lambda = -(wedge + stiffness * std::clamp(wrap(theta - target), -0.4, 0.4) +
                                      opening_drive * wrap(theta - h.rest_angle)) /
                                  denominator;
            a.x = add(a.x, mul(qa, lambda * a.inv_mass));
            b.x = add(b.x, mul(qb, lambda * b.inv_mass));
            c.x = add(c.x, mul(qc, lambda * c.inv_mass));
            d.x = add(d.x, mul(qd, lambda * d.inv_mass));
        }
    }
    double PaperCrumple::previous_hinge_angle(std::size_t index) {
        auto &cached = m_previous_angles[index];
        if (std::isnan(cached)) cached = angle(m_topology, m_topology.hinges[index], true);
        return cached;
    }

    void PaperCrumple::step(bool closing, bool adapt) {
        ++m_tick;
        m_previous_angles.assign(
            m_topology.hinges.size(), std::numeric_limits<double>::quiet_NaN());
        const double time_scale = 1 / m_params.duration;
        const double damping =
            closing ? std::exp(-m_step * time_scale * (3 + 12 * m_params.settling)) : 0;
        const unsigned iterations = m_params.simulation_quality == PaperQuality::Low    ? 8
                                    : m_params.simulation_quality == PaperQuality::High ? 16
                                                                                        : 12;
        double gap = 0, tool_travel = 0;
        Vec2 inverse_reach{1, 1};
        if (closing) {
            m_grip_compliance.assign(m_topology.nodes.size(), 1);
            // A stiff sheet should resist a hand, not absorb its prescribed motion
            // as stretching. Reduce only the external load at saturated material
            // points; internal lengths, bending and contact keep their full strength.
            const double elastic_range = 0.03 + 0.04 * (1 - m_params.paper_stiffness);
            for (const auto &hinge : m_topology.hinges) {
                if (hinge.length <= 0) continue;
                const double strain =
                    std::abs(norm(sub(m_topology.nodes[hinge.a].x, m_topology.nodes[hinge.b].x)) /
                                 hinge.length -
                             1);
                const double compliance = elastic_range / (elastic_range + strain);
                m_grip_compliance[hinge.a] = std::min(m_grip_compliance[hinge.a], compliance);
                m_grip_compliance[hinge.b] = std::min(m_grip_compliance[hinge.b], compliance);
            }
        } else {
            // The tools come to rest, not the material: leave at least 25 ms
            // for wrinkles to relax even at the minimum configured duration.
            const double dwell = std::max(TUNE("DWELL", 0.1), 0.025 / m_params.duration);
            const double finish = 1 - dwell;
            const double now = 1 - m_fraction, before = 1 - m_previous_fraction;
            // Every tool follows one unfolding curve across the whole duration. Its
            // response is quasi-static: removing a share of what remains means a
            // lagging region keeps a bounded ratio and lands with the curve's own
            // vanishing speed instead of being snapped out at the deadline.
            const double clearance = unfolding_left(now / finish);
            // A grip on a long strip travels farther in-plane. Never enlarge
            // the permitted normal step or replace swept contact with this cap.
            inverse_reach = {
                1 / std::max(1.0, m_material.x / 1.8), 1 / std::max(1.0, m_material.y / 1.8)};
            const double slack = 0.03 * clearance * clearance;
            const double span_x =
                slack > 0 ? 1 - std::min(0.3, slack / (m_material.x * m_material.x)) : 1;
            const double span_y =
                slack > 0 ? 1 - std::min(0.3, slack / (m_material.y * m_material.y)) : 1;
            // The palm/table pair closes after the grips have started pulling, so
            // it irons out what remains rather than flattening a wad with its folds.
            const double PRESS_START = TUNE("PRESS_START", 0.2);
            gap = m_press_height * unfolding_left((now - PRESS_START) / (finish - PRESS_START));
            tool_travel = std::max(0.025, 6 * m_step / m_params.duration) / iterations;
            // Finite-compliance hand loads: rim traction transmits through the
            // immutable material metric; released faces can meet the support plane.
            m_opening_loads.resize(m_topology.nodes.size());
            // Firm hands on slowly moving paths: strength overcomes interlocked layers,
            // the path sets the pace. Counterbending keeps up with a few times the share.
            const double hold = -std::expm1(-(70 + 30 * m_params.unfold_tension) * m_step /
                                            (m_params.duration * iterations));
            const double drive_cap =
                (6 + 9 * m_params.unfold_tension) * m_step / m_params.duration;
            const double lead = TUNE("LEAD", 2.0);
            const double turn = m_params.rotation_intensity *
                                (m_params.force_direction == PaperForce::Rotational ? 2.5 : 0.45) *
                                std::sin(PI * now) * clearance * clearance;
            const double cosine = std::cos(turn), sine = std::sin(turn);
            const Vec3 pivot = rest(m_origin);
            for (std::size_t i = 0; i < m_topology.nodes.size(); ++i) {
                const auto &node = m_topology.nodes[i];
                // Staggered grips: each region releases on its own curve.
                double left_now = 0, left_before = 0, weight = 0;
                for (const auto &g : m_grips) {
                    const double u = node.uv.x - g.uv.x, v = node.uv.y - g.uv.y;
                    const double influence = std::exp(-(u * u + v * v) / 0.055) * g.strength;
                    double onset = g.start * (0.4 + 0.4 * m_params.crumpliness);
                    if (m_params.force_direction == PaperForce::Directional)
                        onset += 0.15 * (1 - g.uv.x);
                    const double width = finish - onset;
                    left_now += influence * unfolding_left((now - onset) / width);
                    left_before += influence * unfolding_left((before - onset) / width);
                    weight += influence;
                }
                // Share of the region's remaining work left after this tick.
                const double kept = left_before > 1e-12 * weight
                                        ? std::clamp(left_now / left_before, 0.0, 1.0)
                                        : 0.0;
                const double edge = std::max(std::abs(node.uv.x - 0.5), std::abs(node.uv.y - 0.5));
                auto &load = m_opening_loads[i];
                load.rim = smoothstep(0.35, 0.49, edge) * hold;
                load.gain = std::min(drive_cap, 1 - std::pow(kept, lead / iterations));
                Vec3 goal = rest(node.uv);
                goal.x *= span_x;
                goal.y *= span_y;
                const Vec3 lever = sub(goal, pivot);
                goal = add(pivot,
                    {cosine * lever.x - sine * lever.y, sine * lever.x + cosine * lever.y, 0});
                // The hand carries this rim point from where it was released to its
                // place in the flat sheet; height is left to the material and platen.
                const double share = left_now / weight;
                load.target = {goal.x + share * (node.released.x - goal.x),
                    goal.y + share * (node.released.y - goal.y),
                    0};
            }
        }
        for (std::size_t i = 0; i < m_topology.nodes.size(); ++i) {
            auto &node = m_topology.nodes[i];
            if (node.contact_age > 0) --node.contact_age;
            node.prev = node.x;
            if (node.inv_mass <= 0) continue;
            // Opening is overdamped hand/table work. Keep measured material velocity
            // for remeshing/reversal, but do not launch external projections ballistically.
            if (!closing) {
                node.velocity = {};
                continue;
            }
            node.velocity = add(mul(node.velocity, damping),
                mul(force(node), m_step * time_scale * time_scale * m_grip_compliance[i]));
            const double speed = norm(node.velocity);
            const double max_speed = std::min(6 * time_scale, 0.025 / m_step);
            if (speed > max_speed) node.velocity = mul(node.velocity, max_speed / speed);
            node.x = add(node.x, mul(node.velocity, m_step));
        }
        for (unsigned i = 0; i < iterations; ++i) {
            if (closing)
                project_enclosure();
            else {
                for (std::size_t n = 0; n < m_topology.nodes.size(); ++n) {
                    auto &node = m_topology.nodes[n];
                    if (node.inv_mass <= 0) continue;
                    const auto &load = m_opening_loads[n];
                    Vec3 delta = sub(load.target, node.x);
                    delta.x *= load.rim;
                    delta.y *= load.rim;
                    // The platen itself moves on the unfolding curve, so a firm contact
                    // follows it smoothly; touching layers give way to each other first.
                    delta.z = (std::clamp(node.x.z, -gap, gap) - node.x.z) * (node.contact ? 0.25 : 0.5);
                    const double travel =
                        norm({delta.x * inverse_reach.x, delta.y * inverse_reach.y, delta.z});
                    if (travel > tool_travel) delta = mul(delta, tool_travel / travel);
                    node.x = add(node.x, delta);
                }
            }
            project_bends(gap);
            project_strain();
            project_strain();
            project_strain();
        }
        for (auto &node : m_topology.nodes) {
            if (!finite(node.x)) {
                node.x = finite(node.prev) ? node.prev : rest(node.uv);
                node.velocity = {};
            }
        }
        m_contacts.solve(
            m_topology,
            thickness(m_params),
            0.35 + 0.35 * m_params.settling,
            [](void *context) {
                auto &model = *static_cast<PaperCrumple *>(context);
                for (unsigned i = 0; i < 32; ++i) model.project_strain();
            },
            this);
        for (std::size_t index = 0; index < m_topology.hinges.size(); ++index) {
            auto &h = m_topology.hinges[index];
            if (h.d == BOUNDARY) continue;
            const double theta = wrap(angle(m_topology, h) - h.rest_angle);
            const double elastic = wrap(theta - h.memory);
            const double yield = 0.12 + 0.35 * m_params.paper_stiffness;
            if (closing && std::abs(elastic) > yield) {
                h.memory = std::clamp(h.memory + (elastic - std::copysign(yield, elastic)) * 0.25,
                    -PI + 0.01,
                    PI - 0.01);
            } else if (!closing && std::abs(h.memory) > 1e-8) {
                const double previous = wrap(previous_hinge_angle(index) - h.rest_angle);
                const auto &a = m_topology.nodes[h.a], &b = m_topology.nodes[h.b];
                const auto &c = m_topology.nodes[h.c], &d = m_topology.nodes[h.d];
                // Sustained counterpressure can creep a stationary crease too.
                // Buried/untouched folds retain their signed plastic preference.
                const bool held_pressure =
                    !(a.contact || b.contact || c.contact || d.contact) &&
                    std::max({std::abs(a.x.z), std::abs(b.x.z), std::abs(c.x.z), std::abs(d.x.z)}) >
                        gap &&
                    std::abs(wrap(theta - previous)) * m_params.duration / m_step < 0.1;
                if ((std::abs(theta) + 1e-7 < std::abs(previous) || held_pressure) &&
                    std::abs(theta) < std::abs(h.memory)) {
                    const double target = theta * h.memory > 0 ? theta : 0;
                    const double rate = (4 + 18 * m_params.unfold_tension) *
                                        (1 - 0.9 * m_params.crease_persistence) *
                                        (held_pressure ? 4 : 1);
                    const double flow = -std::expm1(-m_step * time_scale * rate);
                    h.memory += flow * (target - h.memory);
                }
            }
        }
        for (auto &node : m_topology.nodes) {
            if (!finite(node.x) || norm(node.x) > 100 * std::max(m_material.x, m_material.y)) {
                // Invalid individual predictions cannot poison contacts or subsequent ticks.
                node.x = finite(node.prev) ? node.prev : rest(node.uv);
                node.velocity = {};
            } else {
                node.velocity = mul(sub(node.x, node.prev), 1 / m_step);
            }
        }
        if (adapt) m_topology.adapt(m_params, m_tick);
    }

    void PaperCrumple::flatten() {
        for (auto &node : m_topology.nodes) {
            node.x = rest(node.uv);
            node.prev = node.x;
            node.velocity = {};
            node.contact_age = 0;
            node.contact = false;
        }
    }
    bool PaperCrumple::flat_projection_safe(bool previous) const {
        // Positive-definite symmetric XY/material Jacobians imply global strong
        // monotonicity on the convex UV rectangle. Convex interpolation to the
        // identity stays injective. Ordinary physical updates still use full CCD.
        for (const auto &face : m_topology.faces) {
            const auto &a = m_topology.nodes[face.v[0]];
            const auto &b = m_topology.nodes[face.v[1]];
            const auto &c = m_topology.nodes[face.v[2]];
            const Vec2 u{(b.uv.x - a.uv.x) * m_material.x, (b.uv.y - a.uv.y) * m_material.y};
            const Vec2 v{(c.uv.x - a.uv.x) * m_material.x, (c.uv.y - a.uv.y) * m_material.y};
            const double det = u.x * v.y - v.x * u.y;
            if (std::abs(det) < 1e-16) return false;
            const Vec3 pa = previous ? a.prev : a.x;
            const Vec3 x = sub(previous ? b.prev : b.x, pa);
            const Vec3 y = sub(previous ? c.prev : c.x, pa);
            const double xx = (x.x * v.y - y.x * u.y) / det;
            const double yy = (y.y * u.x - x.y * v.x) / det;
            const double xy = 0.5 * ((y.x * u.x - x.x * v.x) + (x.y * v.y - y.y * u.y)) / det;
            if (!(xx > 1e-8 && yy > 1e-8 && xx * yy - xy * xy > 1e-8)) return false;
        }
        return !m_topology.faces.empty();
    }

    double PaperCrumple::flat_error_pixels(bool previous) const {
        const double sx = m_output_size.x / m_material.x;
        const double sy = m_output_size.y / m_material.y;
        const double sz = std::sqrt(sx) * std::sqrt(sy);
        double error = 0;
        for (const auto &node : m_topology.nodes) {
            const Vec3 d = sub(previous ? node.prev : node.x, rest(node.uv));
            const double distance = std::hypot(d.x * sx, d.y * sy, d.z * sz);
            if (!std::isfinite(distance)) return INFINITY;
            error = std::max(error, distance);
        }
        return error;
    }
    bool PaperCrumple::flat_and_quiet() const {
        if (flat_error_pixels() > 0.25 || flat_error_pixels(true) > 0.25 ||
            !flat_projection_safe() || !flat_projection_safe(true))
            return false;
        const double sx = m_output_size.x / m_material.x;
        const double sy = m_output_size.y / m_material.y;
        const double sz = std::sqrt(sx) * std::sqrt(sy);
        for (const auto &node : m_topology.nodes) {
            const Vec3 v = mul(sub(node.x, node.prev), 1 / m_step);
            // Less than .05 px of motion over a 120 Hz presentation interval.
            if (std::hypot(v.x * sx, v.y * sy, v.z * sz) > 6) return false;
        }
        for (const auto &h : m_topology.hinges) {
            if (h.d == BOUNDARY) continue;
            const double current = angle(m_topology, h);
            const double previous = angle(m_topology, h, true);
            if (std::abs(current) > 0.01 || std::abs(previous) > 0.01 ||
                std::abs(wrap(current - previous)) / m_step > 0.1)
                return false;
        }
        return true;
    }

    bool PaperCrumple::advance(double seconds) {
        if (!active()) return false;
        if (!std::isfinite(seconds) || seconds < 0 || seconds > STALL_WINDOW_SECONDS) {
            if (m_phase == Phase::Opening) {
                if (flat_and_quiet()) {
                    flatten();
                    m_fraction = 0;
                    m_phase = Phase::Flat;
                }
            } else {
                m_fraction = 1;
                m_phase = Phase::Closed;
            }
            m_accumulator = 0;
            return active();
        }
        // Bound per-frame work, retaining substep residue. A compositor stall delays
        // motion instead of replaying a burst of missed dynamics on the next frame.
        m_accumulator += std::min(seconds, 1.0 / 30.0);
        while (active() && m_accumulator + 1e-12 >= m_step) {
            m_accumulator = std::max(0.0, m_accumulator - m_step);
            const bool closing = m_phase == Phase::Closing;
            m_previous_fraction = m_fraction;
            m_fraction =
                std::clamp(m_fraction + (closing ? m_step : -m_step) / m_params.duration, 0.0, 1.0);
            step(closing);
            if (closing && m_fraction >= 1 - 1e-12) {
                m_fraction = 1;
                m_phase = Phase::Closed;
            } else if (!closing && m_fraction <= 1e-12 && flat_and_quiet()) {
                flatten();
                m_fraction = 0;
                m_phase = Phase::Flat;
            }
        }
        return active();
    }

    bool PaperCrumple::active() const noexcept {
        return m_phase == Phase::Opening || m_phase == Phase::Closing;
    }
    bool PaperCrumple::closed() const noexcept {
        return m_phase == Phase::Closed;
    }
    double PaperCrumple::opacity() const noexcept {
        const double fraction =
            active() ? m_previous_fraction + (m_fraction - m_previous_fraction) *
                                                 std::clamp(m_accumulator / m_step, 0.0, 1.0)
                     : m_fraction;
        return 1 - smoothstep(0.88, 1, fraction);
    }

    void PaperCrumple::frame(PaperSurface &out) const {
        out.size = m_size;
        out.opacity = static_cast<float>(opacity());
        out.shading = static_cast<float>(m_params.shading);
        out.thickness = static_cast<float>(thickness(m_params));
        out.points.resize(m_topology.nodes.size());
        const double alpha = active() ? std::clamp(m_accumulator / m_step, 0.0, 1.0) : 1;
        for (std::size_t i = 0; i < out.points.size(); ++i) {
            const auto &node = m_topology.nodes[i];
            out.points[i] = {
                add(node.prev, mul(sub(node.x, node.prev), alpha)), node.uv, node.contact};
        }
        // Scratch capacity survives publications: no per-edge allocation on the worker.
        m_crease_scratch.clear();
        if (m_phase != Phase::Flat) {
            m_crease_scratch.reserve(m_topology.hinges.size());
            for (const auto &h : m_topology.hinges) {
                if (h.d == BOUNDARY) continue;
                const double curvature = std::abs(angle(m_topology, h));
                const double memory = std::abs(h.memory);
                m_crease_scratch.emplace_back(edge_key(h.a, h.b),
                    static_cast<float>(
                        std::clamp((curvature + memory * 0.3 - 0.08) / 1.8, 0.0, 1.0)));
            }
            std::sort(m_crease_scratch.begin(),
                m_crease_scratch.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
        }
        out.faces.resize(m_topology.faces.size());
        for (std::size_t i = 0; i < out.faces.size(); ++i) {
            auto &face = out.faces[i];
            face.vertex = m_topology.faces[i].v;
            face.crease = {};
            if (m_phase == Phase::Flat) continue;
            for (unsigned e = 0; e < 3; ++e) {
                const auto key = edge_key(face.vertex[e], face.vertex[(e + 1) % 3]);
                const auto found = std::lower_bound(m_crease_scratch.begin(),
                    m_crease_scratch.end(),
                    key,
                    [](const auto &edge, std::uint64_t wanted) { return edge.first < wanted; });
                if (found != m_crease_scratch.end() && found->first == key)
                    face.crease[e] = found->second;
            }
        }
    }

    void PaperCrumple::reset() {
        flatten();
        m_contacts.reset();
        m_fraction = m_previous_fraction = m_accumulator = 0;
        m_phase = Phase::Flat;
    }

}  // namespace hyprimate
