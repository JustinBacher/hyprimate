#include <concepts>

#include "workspace/WorkspaceAnimationImpl.hpp"

CWorkspaceAnimation::Impl::Impl(HANDLE handle) : m_handle(handle) {
    m_styles.for_each([this]<class Style>(auto &resources) {
        const auto CHANGED = [this] {
            if (m_last_settings && m_last_settings->style == Style::KIND) {
                m_turns.reload();
                m_settings_changed = true;
                configure(*m_last_settings, false);
            }
        };
        if constexpr (requires { resources.curves.install(m_handle, CHANGED); }) {
            if (!resources.curves.install(m_handle, CHANGED)) {
                HyprlandAPI::addNotification(m_handle,
                    "[hyprimate] Workspace style Lua curve registration unavailable",
                    CHyprColor(1.F, 0.35F, 0.2F, 1.F),
                    10000.F);
            }
        }
    });
}

bool CWorkspaceAnimation::Impl::request_enabled() {
    if (m_native_direction && m_last_settings && m_last_settings->enabled && m_capture_depth == 0 &&
        m_mutation_depth == 0 && m_style_draw_depth == 0) {
        // Native animation settings can change without a plugin config callback.
        // Refresh once at request admission, never in the output-frame loop.
        const auto KEEP_ALIVE = shared_from_this();
        m_styles.for_each([&]<class Style>(auto &resources) {
            if constexpr (requires { Style::refresh_timing(resources); }) {
                if (Style::KIND == m_style_kind && Style::refresh_timing(resources)) {
                    m_settings_changed = true;
                    configure(*m_last_settings, false);
                }
            }
        });
        return Impl::s_active == this && m_turns.enabled();
    }
    return m_turns.enabled();
}

void CWorkspaceAnimation::Impl::configure(
    const hyprimate::config::SWorkspaceSettings &requested, bool force) {
    const auto keep_alive = shared_from_this();
    if (!force && !m_settings_changed && m_last_settings == requested && !m_deferred_settings) {
        return;
    }
    const auto settings = requested;
    const auto *previous = m_last_settings ? &*m_last_settings : nullptr;
    bool native_changed =
        !previous || previous->enabled != settings.enabled || previous->style != settings.style;
    if (!native_changed) {
        m_styles.for_each([&]<class Style>(auto & /*resources*/) {
            if (settings.style == Style::KIND) {
                native_changed = Style::options(*previous) != Style::options(settings);
            }
        });
    }
    if (m_style_draw_depth) {
        const bool DEFERRED_FORCE = m_deferred_settings && m_deferred_settings->second;
        m_deferred_settings.emplace(settings, force || DEFERRED_FORCE);
        return;
    }
    auto lifecycle = m_lifecycle;
    if (native_changed) {
        lifecycle = ++m_lifecycle;
        if (m_capture_depth) {
            m_turns.clear();
        }
    }
    const double EFFECT_DURATION = settings.effectDuration / 1000.0;
    const bool SAME_EFFECT = previous && previous->enabled && previous->effectDuration > 0.0 &&
                             previous->effect == settings.effect &&
                             previous->effectParameters == settings.effectParameters;
    const bool MATCHES_PREPARED = m_effect_program && m_effect_request.first == settings.effect &&
                                  m_effect_request.second == settings.effectParameters;
    if (!settings.enabled || settings.effect.empty() || !std::isfinite(EFFECT_DURATION) ||
        EFFECT_DURATION <= 0.0) {
        m_effect_program.reset();
        m_effect_duration = 0.0;
        m_effect_request = {};
    } else if (!force && MATCHES_PREPARED) {
        m_effect_duration = EFFECT_DURATION;
    } else if (force || !SAME_EFFECT) {
        std::string error;
        auto effect = hyprimate::shader::prepare_scene_program(
            hyprimate::shader::Event::Workspace, settings.effect, settings.effectParameters, error);
        if (m_lifecycle != lifecycle) {
            return;
        }
        if (effect) {
            m_effect_program = std::move(effect);
            m_effect_duration = EFFECT_DURATION;
            m_effect_request = {settings.effect, settings.effectParameters};
        } else {
            LOG(Log::WARN, "[hyprimate] workspace shader unavailable: {}", error);
        }
    }
    m_settings_changed = m_settings_changed || native_changed || force;
    m_last_settings = settings;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (settings.style != Style::KIND) {
            return;
        }
        const hyprimate::workspace::ProfileCpuScope CPU{
            {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"},
            hyprimate::perf::Phase::Setup};
        const auto PREVIOUS = resources.settings.last;
        Style::prepare(resources, settings);
        if (m_lifecycle != lifecycle) {
            return;
        }
        const bool ENABLED = hyprimate::workspace::configured_transition_available(settings.enabled,
            m_effect_program != nullptr && m_effect_duration > 0.0,
            resources.settings.prepared.duration);
        const bool PRESERVE_LATCHED_SHADER =
            settings.enabled && !ENABLED &&
            (!Style::needs_individual_windows(resources) || m_window_hook) &&
            (!Style::uses_native_hud(resources) || m_layer_hook) && has_latched_shader_turn();
        if (PREVIOUS != resources.settings.last || m_style_kind != Style::KIND ||
            !settings.enabled || (m_turns.enabled() && !ENABLED && !PRESERVE_LATCHED_SHADER)) {
            clear();
            if (m_lifecycle != lifecycle + 1) {
                return;
            }
            lifecycle = m_lifecycle;
        } else if (PRESERVE_LATCHED_SHADER) {
            // Shader-only entries retain their latched program/timing; all others can
            // no longer complete under the native configuration and must fall back.
            expire_unlatched_turns();
        }
        m_travel = Style::travel(resources);
        m_max_frame_gap = hyprimate::STALL_SECONDS;
        if constexpr (requires { Style::max_frame_gap(resources); }) {
            m_max_frame_gap = std::max(m_max_frame_gap, Style::max_frame_gap(resources));
        }
        m_individual_windows = Style::needs_individual_windows(resources);
        m_native_hud = Style::uses_native_hud(resources);
        m_uses_previews = settings.enabled && Style::uses_workspace_previews(resources);
        if (!m_uses_previews) {
            m_observed.clear();
        }
        const bool SUPPORTED =
            (!m_individual_windows || m_window_hook) && (!m_native_hud || m_layer_hook);
        if (ENABLED && !SUPPORTED && m_settings_changed) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Selected workspace options require an unavailable native capture hook",
                CHyprColor(1.F, 0.35F, 0.2F, 1.F),
                10000.F);
        }
        if (m_lifecycle != lifecycle) {
            return;
        }
        m_admit_turns = ENABLED && SUPPORTED;
        // A shader-only setup can become disabled for new turns while a
        // previously admitted pair still needs its registry and capture policy.
        configure_style<Style>(settings.enabled && SUPPORTED, resources);
        if (m_lifecycle != lifecycle) {
            return;
        }
        m_style_warm_pending = m_admit_turns && m_turns.enabled() && !resources.ready;
    });
    if (m_lifecycle == lifecycle && m_admit_turns && m_turns.enabled()) {
        prime_outputs();
    }
}

template <class Style>
void CWorkspaceAnimation::Impl::configure_style(bool enabled, Style::Resources &resources) {
    const auto lifecycle = m_lifecycle;
    if (m_style_kind != Style::KIND) {
        m_invalid_curve.reset();
    }
    m_style_kind = Style::KIND;
    const bool WINDOW_PLANES = hyprimate::workspace::needs_window_planes<Style>(resources);
    m_layered = Style::LAYERED || WINDOW_PLANES;
    m_window_planes = WINDOW_PLANES;
    m_uses_wallpaper = Style::USES_WALLPAPER || WINDOW_PLANES;
    m_native_direction = false;
    if constexpr (requires { Style::USES_NATIVE_DIRECTION; }) {
        m_native_direction = Style::USES_NATIVE_DIRECTION;
    }
    const auto RESOLVE =
        [&](std::string_view requested_curve) -> hyprimate::workspace::Transition::Curve {
        auto &manager = Animation::mgr();
        if (!manager) {
            return {};
        }
        const std::string NAME(requested_curve);
        bool known = false;
        using Selection = hyprimate::workspace::Transition::CurveSelection;
        if constexpr (requires {
                          {
                              resources.curves.resolve(NAME, resources.settings.prepared.duration)
                          } -> std::same_as<Selection>;
                      }) {
            auto selection = resources.curves.resolve(NAME, resources.settings.prepared.duration);
            if (m_lifecycle != lifecycle) {
                return {};
            }
            using Kind = Selection::Kind;
            if (selection.kind == Kind::Spring) {
                m_invalid_curve.reset();
                return std::move(selection.curve);
            }
            known = selection.kind == Kind::Bezier;
        } else {
            if constexpr (requires {
                              resources.curves.resolve(NAME, resources.settings.prepared.duration);
                          }) {
                if (auto curve =
                        resources.curves.resolve(NAME, resources.settings.prepared.duration)) {
                    if (m_lifecycle != lifecycle) {
                        return {};
                    }
                    m_invalid_curve.reset();
                    return curve;
                }
            }
            known = manager->bezierExists(NAME);
        }
        const bool KNOWN = known;
        if (!KNOWN && (!m_invalid_curve || *m_invalid_curve != NAME)) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Unknown workspace curve '" + NAME +
                    (m_turns.curve() ? "'; retaining the last valid curve."
                                     : "'; using the native default curve."),
                CHyprColor(1.F, 0.35F, 0.2F, 1.F),
                10000.F);
            if (m_lifecycle != lifecycle) {
                return {};
            }
            m_invalid_curve = NAME;
        }
        if (!KNOWN && m_turns.curve()) {
            return m_turns.curve();
        }
        std::string selected = NAME;
        if (!KNOWN) {
            selected = manager->bezierExists("default") ? "default" : "linear";
        }
        const auto CURVE = manager->getBezier(selected);
        if (!CURVE) {
            return {};
        }
        if (KNOWN) {
            m_invalid_curve.reset();
        }
        return [CURVE](double progress) {
            return static_cast<double>(CURVE->getYForPoint(static_cast<float>(progress)));
        };
    };
    const double duration =
        m_effect_program ? m_effect_duration : resources.settings.prepared.duration;
    const double registry_duration = std::isfinite(duration) && duration > 0.0
                                         ? duration
                                         : (m_states.empty() ? 0.0 : m_turns.duration());
    m_turns.configure(enabled,
        static_cast<std::uint64_t>(Style::KIND),
        registry_duration,
        resources.settings.prepared.curve,
        m_settings_changed,
        RESOLVE);
    if (m_lifecycle != lifecycle) {
        return;
    }
    m_settings_changed = false;
}

bool CWorkspaceAnimation::Impl::initialize_selected_style() {
    if (!g_pHyprRenderer || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
        return false;
    }
    bool ready = false;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND != m_style_kind) {
            return;
        }
        if (!resources.ready) {
            const hyprimate::gl::ScopedCompositorContext CONTEXT{};
            if (!CONTEXT) {
                return;
            }
            const hyprimate::workspace::ProfileCpuScope CPU{
                {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"},
                hyprimate::perf::Phase::Setup};
            const hyprimate::perf::GpuScope GPU{
                {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"}};
            resources.ready = resources.renderer.initialize();
        }
        ready = resources.ready;
    });
    return ready;
}

void CWorkspaceAnimation::Impl::release_styles() {
    m_styles.for_each([]<class Style>(auto &resources) {
        // Failed initialization and backend loss still need an idempotent
        // release; each renderer verifies its own context before deleting names.
        resources.renderer.release();
        resources.ready = false;
    });
}

double CWorkspaceAnimation::Impl::TurnPort::duration(
    const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &destination,
    double base) const {
    if (entry->effect_program) {
        return entry->base_duration;
    }
    double duration = base;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) {
            duration = Style::duration(resources, from, destination);
        }
    });
    return duration;
}

std::optional<std::pair<double, hyprimate::workspace::Transition::Curve>>
CWorkspaceAnimation::Impl::TurnPort::next_pair(const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &destination) const {
    if (!owner->m_admit_turns || !owner->m_turns.enabled()) {
        return std::pair<double, hyprimate::workspace::Transition::Curve>{};
    }
    entry->latch_effect();
    return std::pair{duration(from, destination, entry->base_duration), entry->curve};
}

double CWorkspaceAnimation::Impl::TurnPort::live_boundary() const {
    double boundary = 1.0;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) {
            boundary = Style::live_boundary(resources);
        }
    });
    return boundary;
}
