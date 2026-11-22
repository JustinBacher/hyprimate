#include <type_traits>

#include "HyprAnimationCanvas.hpp"
#include "StyleTypes.hpp"
#include "window/WindowStyle.hpp"

template <class... Types>
struct UniqueTypes;
template <>
struct UniqueTypes<> : std::true_type {};
template <class First, class... Rest>
struct UniqueTypes<First, Rest...>
    : std::bool_constant<(!std::is_same_v<First, Rest> && ...) && UniqueTypes<Rest...>::value> {};

template <class List>
struct ProductionContract;
template <class... Styles>
struct ProductionContract<hyprimate::animation::StyleList<Styles...>> {
    static_assert(UniqueTypes<typename Styles::State...>::value);
    static_assert(UniqueTypes<typename Styles::Resources...>::value);
    static_assert((hyprimate::animation::WindowStyle<Styles, CHyprAnimationCanvas> && ...));
    static constexpr bool VALID = true;
};
static_assert(ProductionContract<ProductionStyles>::VALID);
