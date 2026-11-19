#pragma once

#include <tuple>
#include <utility>

#include "StyleTypes.hpp"
#include "animations/shared/SheetResources.hpp"

template <class List> class SStyleResources;
template <class... Modules> class SStyleResources<STypeList<Modules...>> {
  public:
    explicit SStyleResources(SSharedStyleResources &shared)
        : m_values((static_cast<void>(sizeof(Modules)), shared)...) {}

    template <class Module> typename Module::Resources &get() {
        return std::get<typename Module::Resources>(m_values);
    }
    template <class Module> const typename Module::Resources &get() const {
        return std::get<typename Module::Resources>(m_values);
    }
    template <class F> void for_each(F &&operation) {
        (operation(typename SStyleModuleFor<typename Modules::State>::Live{}, get<Modules>()), ...);
    }

  private:
    std::tuple<typename Modules::Resources...> m_values;
};
