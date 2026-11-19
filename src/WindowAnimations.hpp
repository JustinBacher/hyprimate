#pragma once

#include <cassert>
#include <optional>
#include <span>
#include <vector>

#include "WindowAnimationPort.hpp"
#include "WindowStyle.hpp"
#include "WindowStyleList.hpp"

namespace hyprimate::animation {
template<class Port, class List> class CWindowAnimations;

template<class Port, class... Styles>
class CWindowAnimations<Port, StyleList<Styles...>> {
    static_assert(WindowPort<Port>);
    static_assert((WindowStyle<Styles, typename Port::Canvas> && ...));
    static consteval bool unique_kinds() {
        constexpr std::array<config::Style, sizeof...(Styles)> kinds{Styles::KIND...};
        for (std::size_t left = 0; left < kinds.size(); ++left)
            for (std::size_t right = left + 1; right < kinds.size(); ++right)
                if (kinds[left] == kinds[right]) return false;
        return true;
    }
    static_assert(unique_kinds(), "Window Styles must have distinct IDs");
  public:
    using Canvas = typename Port::Canvas;
    using Resources = StyleResources<StyleList<Styles...>>;
    CWindowAnimations(Port &port, Resources &resources) : m_port(port), m_resources(resources) {}
    ~CWindowAnimations() { assert(m_entries.empty()); }
    CWindowAnimations(const CWindowAnimations &) = delete;
    CWindowAnimations &operator=(const CWindowAnimations &) = delete;

    void configure(const Policy &policy);
    template<class Style> void update_settings(const typename Style::Settings &settings);
    void track(typename Port::WindowRef window);
    void unmap(WindowId window);
    void destroyed(WindowId window);
    void close(WindowId window, Clock::time_point now);
    void bind_fadeout(WindowId window, const FadeoutFacts &fadeout, typename Port::RetainedImage image);
    void frame(const FrameStamp &stamp);
    void before_window(WindowId window, Canvas &canvas);
    void after_windows(MonitorId monitor, Canvas &canvas);
    bool needs_frame(MonitorId monitor) const;
    void monitor_changed(MonitorId monitor);
    void reload();
    bool active(Handle handle) const;
    std::optional<Frame> view(Handle handle) const;
    std::optional<Handle> pending_for(WindowId window) const;
    std::span<const DyingView> dying_views() const;
    bool suppresses(std::uint64_t fadeout) const;
    void native_finished(std::uint64_t fadeout);
    Box extents(Handle handle, const Box &capture) const;
    bool draw(Handle handle, const Draw<Canvas> &draw);
    void detach_transformers();
    void end_all();
  private:
    struct Tracked {
        typename Port::WindowRef window;
        WindowId id = 0;
        std::optional<Observation> previous;
        std::optional<Handle> handle;
        bool opening_seen = false;
        bool driven = false;
        bool workspace_movement = false;
        bool tail_capture_valid = false;
        Box tail_capture{};
        Box goal{}, last_warp{};
    };
    struct Record {
        Handle handle;
        WindowId origin = 0;
        MonitorId monitor = 0;
        WorkspaceId workspace = 0;
        Life life = Life::Mapped;
        Clock::time_point last_step{}, pending_since{};
        FrameId last_frame = 0;
        bool stepped = false;
        std::array<Clock::time_point, STALL_LIMIT> stalls{};
        std::size_t stall_count = 0;
        Frame current{};
        Place close_place{};
        CloseStart ownership = CloseStart::Declined;
        std::uint64_t fadeout = 0;
        FadeoutFacts fadeout_info{};
        float captured_alpha = 1;
        double captured_scale = 1;
        Box source_device{};
        Box footprint{};
        bool no_screen_share = false;
    };
    template<class Function> bool visit(Handle handle, Function &&function);
    template<class Function> bool visit(Handle handle, Function &&function) const;
    template<class Function> bool selected(Function &&function);
    Tracked *tracked(WindowId id);
    const Tracked *tracked(WindowId id) const;
    Record *record(Handle handle);
    const Record *record(Handle handle) const;
    std::optional<Handle> acquire(Tracked &tracked, const Observation &observation, std::uint64_t seed);
    void end(Handle handle, End why);
    void release_drive(Tracked &tracked);
    bool apply(Tracked &tracked, const Observation &observation, const Effects &effects);
    static Place place(const Observation &observation, Neighbourhood near, std::uint64_t seed);
    Port &m_port;
    Resources &m_resources;
    Policy m_policy{};
    std::vector<Tracked> m_tracked;
    std::vector<Record> m_entries;
    std::vector<std::uint64_t> m_tombstones;
    std::vector<std::pair<MonitorId, FrameId>> m_screen_frames;
    mutable std::vector<DyingView> m_views;
    std::uint64_t m_seed = 0;
};
} // namespace hyprimate::animation

#include "WindowAnimations.tpp"
