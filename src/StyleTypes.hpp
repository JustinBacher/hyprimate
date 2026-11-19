#pragma once

#include <variant>

#include "StyleList.hpp"
#include "animations/liquid/LiquidStyle.hpp"
#include "animations/paper/PaperStyle.hpp"
#include "animations/cloth/ClothStyle.hpp"
#include "animations/swarm/SwarmStyle.hpp"
#include "animations/bubble/BubbleStyle.hpp"
#include "animations/smoke/SmokeStyle.hpp"

#define HYPRIMATE_DECLARE_TAG(kind, tag, module) struct tag;
HYPRIMATE_STYLE_LIST(HYPRIMATE_DECLARE_TAG, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_DECLARE_TAG

template <class... Modules> struct STypeList {};
#define HYPRIMATE_MODULE(kind, tag, module) module
using StyleModules = STypeList<HYPRIMATE_STYLE_LIST(HYPRIMATE_MODULE, HYPRIMATE_STYLE_COMMA)>;
#undef HYPRIMATE_MODULE

template <class List> struct SStyleTypes;
template <class... Modules> struct SStyleTypes<STypeList<Modules...>> {
    using State = std::variant<typename Modules::State...>;
    using CloseData = std::variant<typename Modules::CloseData...>;
};
using SStyleState = SStyleTypes<StyleModules>::State;
using SCloseStyleData = SStyleTypes<StyleModules>::CloseData;

template <class State> struct SStyleModuleFor;
#define HYPRIMATE_ASSOCIATE(kind, tag, module) \
    template <> struct SStyleModuleFor<module::State> { using Module = module; using Live = tag; };
HYPRIMATE_STYLE_LIST(HYPRIMATE_ASSOCIATE, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_ASSOCIATE
using SDefaultStyleModule = std::variant_alternative_t<0, SStyleState>;
using SDefaultLiveStyle = SStyleModuleFor<SDefaultStyleModule>::Live;
