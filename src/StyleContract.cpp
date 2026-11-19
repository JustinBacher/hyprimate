#include <array>
#include <cstddef>
#include <type_traits>

#include "StyleContract.hpp"

template <class... Types> struct SUniqueTypes;
template <> struct SUniqueTypes<> : std::true_type {};
template <class First, class... Rest>
struct SUniqueTypes<First, Rest...>
    : std::bool_constant<(!std::is_same_v<First, Rest> && ...) && SUniqueTypes<Rest...>::value> {};

template <class List> struct SUniqueStyleMetadata;
template <class... Modules> struct SUniqueStyleMetadata<STypeList<Modules...>> {
    static_assert(SUniqueTypes<typename Modules::State...>::value);
    static_assert(SUniqueTypes<typename Modules::CloseData...>::value);
    static_assert(SUniqueTypes<typename Modules::Resources...>::value);
    static constexpr auto IDS = std::array{Modules::KIND...};
    static consteval bool unique_ids() {
        for (std::size_t i = 0; i < IDS.size(); ++i)
            for (std::size_t j = i + 1; j < IDS.size(); ++j)
                if (IDS[i] == IDS[j])
                    return false;
        return true;
    }
    static_assert(unique_ids());
};
static_assert(sizeof(SUniqueStyleMetadata<StyleModules>) > 0);

#define HYPRIMATE_CHECK_STYLE(kind, tag, module) static_assert(StyleModule<tag>);
HYPRIMATE_STYLE_LIST(HYPRIMATE_CHECK_STYLE, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_CHECK_STYLE
