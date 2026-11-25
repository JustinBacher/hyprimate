#pragma once

#include "Package.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace hyprimate::shader::package_detail {
inline constexpr std::size_t MiB = 1024 * 1024;
inline constexpr std::size_t MaxSource = 4 * MiB;
inline constexpr std::uint64_t MaxInvocations = 4'194'304;

[[noreturn]] void fail(const std::string &message);
void require(bool condition, const std::string &message);
unsigned components(ValueType type);
void range(const Parameter &parameter, const Value &value);
unsigned pixel_bytes(Format format);
void validate(const Package &package);
} // namespace hyprimate::shader::package_detail
