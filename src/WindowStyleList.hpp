#pragma once

#include <tuple>
#include <utility>

namespace hyprimate::animation {
template<class... Styles> struct StyleList {};

template<class List> class StyleResources;
template<class... Styles> class StyleResources<StyleList<Styles...>> {
  public:
    StyleResources() = default;
    template<class Shared> explicit StyleResources(Shared &shared)
        : m_values((static_cast<void>(sizeof(Styles)), shared)...) {}
    template<class Style> typename Style::Resources &get() {
        return std::get<typename Style::Resources>(m_values);
    }
    template<class Style> const typename Style::Resources &get() const {
        return std::get<typename Style::Resources>(m_values);
    }
    template<class Function> void for_each(Function &&function) {
        (function.template operator()<Styles>(get<Styles>()), ...);
    }
    template<class Function> void for_each(Function &&function) const {
        (function.template operator()<Styles>(get<Styles>()), ...);
    }
  private:
    std::tuple<typename Styles::Resources...> m_values;
};
} // namespace hyprimate::animation
