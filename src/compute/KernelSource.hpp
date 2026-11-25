#pragma once

#include "Types.hpp"

namespace hyprimate::compute::detail {
    // Internal functions assume a previously validated descriptor.
    [[nodiscard]] std::string kernel_source(const KernelDesc&, Backend, bool validation_only = false);
    [[nodiscard]] std::string sampler_name(std::string_view alias);
    [[nodiscard]] std::string size_name(std::string_view alias);
}
