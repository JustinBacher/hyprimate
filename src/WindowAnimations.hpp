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
        constexpr std::array<config::Style, sizeof...(Styles)> KINDS{Styles::KIND...};
        for (std::size_t left = 0; left < KINDS.size(); ++left) {
            for (std::size_t right = left + 1; right < KINDS.size(); ++right) {
                if (KINDS[left] == KINDS[right]) { return false; }
            }
        }
        return true;
    }
    static_assert(unique_kinds(), "Window Styles must have distinct IDs");
  public:
    using Canvas = Port::Canvas;
    using Resources = StyleResources<StyleList<Styles...>>;
    CWindowAnimations(Port &port, Resources &resources, std::uint64_t initial_seed = 0)
        : m_port(port), m_resources(resources), m_seed(initial_seed) {}
    ~CWindowAnimations() { assert(m_entries.empty()); }
    CWindowAnimations(const CWindowAnimations &) = delete;
    CWindowAnimations &operator=(const CWindowAnimations &) = delete;
    CWindowAnimations(CWindowAnimations &&) = delete;
    CWindowAnimations &operator=(CWindowAnimations &&) = delete;

    void configure(const Policy &policy);
    template<class Style> void update_settings(const Style::Settings &settings);
    void track(Port::WindowRef window);
    void unmap(WindowId window_id);
    void destroyed(WindowId window_id);
    void close(WindowId window_id, Clock::time_point now);
    void bind_fadeout(WindowId window_id, const FadeoutFacts &fadeout, Port::RetainedImage image);
    void frame(const FrameStamp &stamp);
    void before_window(WindowId window_id, Canvas &canvas);
    void before_windows(MonitorId monitor, Canvas &canvas);
    void after_windows(MonitorId monitor, Canvas &canvas);
    bool needs_frame(MonitorId monitor) const;
    void monitor_changed(MonitorId monitor);
    void reload();
    bool active(Handle handle) const;
    std::optional<Frame> view(Handle handle) const;
    std::optional<Handle> pending_for(WindowId window_id) const;
    std::span<const DyingView> dying_views() const;
    // Allocation-free value lookup for reentrant close-renderer collection.
    std::optional<DyingView> dying_view_for(std::uint64_t fadeout) const;
    bool suppresses(std::uint64_t fadeout) const;
    void native_finished(std::uint64_t fadeout);
    Box extents(Handle handle, const Box &capture) const;
    bool draw(Handle handle, const Draw<Canvas> &draw);
    bool tail_allowed(Handle handle, const Canvas &canvas, bool retained_no_screen_share) const;
    bool draw_tail(Handle handle, const Draw<Canvas> &draw, bool retained_no_screen_share);
    template<class Display>
    bool display_tail(Handle handle, const Draw<Canvas> &draw, bool retained_no_screen_share, Display &&display);
    void detach_transformers();
    void end_all();
  private:
    struct Tracked {
        Port::WindowRef window;
        WindowId id = 0;
        std::optional<Observation> previous;
        Clock::time_point previous_sample{};
        std::optional<Handle> handle;
        std::optional<Edge> opening_edge;
        bool opening_seen = false;
        bool expired = false;
        bool driven = false;
        bool workspace_movement = false;
        bool tail_capture_valid = false;
        Box tail_capture{};
        Box goal{}, last_warp{};
    };
    struct Record {
        WindowId origin = 0;
        MonitorId monitor = 0;
        WorkspaceId workspace = 0;
        Clock::time_point last_step, pending_since;
        FrameId last_frame = 0;
        std::size_t stall_count = 0;
        std::uint64_t fadeout = 0;
        double captured_scale = 1;
        std::array<Clock::time_point, STALL_LIMIT> stalls{};
        Box source_device{};
        Box footprint{};
        Frame current{};
        FadeoutFacts fadeout_info{};
        Place close_place{};
        float captured_alpha = 1;
        Handle handle;
        Life life = Life::Mapped;
        bool stepped = false;
        CloseStart ownership = CloseStart::Declined;
        bool no_screen_share = false;
        bool retained_no_screen_share = false;
    };
    template<class Function> bool visit(Handle handle, Function &&function);
    template<class Function> bool visit(Handle handle, Function &&function) const;
    template<class Function> bool selected(Function &&function);
    Tracked *tracked(WindowId window_id);
    const Tracked *tracked(WindowId window_id) const;
    Record *record(Handle handle);
    const Record *record(Handle handle) const;
    DyingView dying_view(const Record &entry) const;
    std::optional<Handle> acquire(Tracked &tracked, const Observation &observation, std::uint64_t seed);
    void end(Handle handle, End why);
    void release_drive(Tracked &tracked);
    bool apply(Tracked &tracked, const Observation &observation, const Effects &effects);
    static Place place(const Observation &observation, Neighbourhood near, std::uint64_t seed);
    void tick_screens(const FrameStamp &stamp);
    template<class Style> void tick_screen(ScreenFacts &facts, MonitorId monitor);
    bool usable_observation(Tracked &window, const std::optional<Observation> &observation);
    std::optional<Observation> sample_window(Tracked &window, const FrameStamp &stamp);
    // Returns whether accepted opening effects require a geometry reread.
    bool begin_mapped(Tracked &window, const Observation &observation,
        const std::optional<Motion> &motion);
    static std::optional<double> step_clock(Record &entry, const FrameStamp &stamp);
    void present_mapped(Tracked &window, Record &entry, const Observation &observation);
    void frame_mapped(Tracked &window, const FrameStamp &stamp);
    bool refresh_fadeout(Record &entry);
    void frame_dying(Record &entry, const FrameStamp &stamp);
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
