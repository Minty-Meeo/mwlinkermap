// SPDX-License-Identifier: CC0-1.0

#pragma once

#include <iterator>
#include <ostream>
#include <string>
#include <utility>

#include <fmt/format.h>

// TODO: Replace with C++23's std::print(std::ostream&) overloads.

namespace Mijo
{
template <class... Args>
void Print(std::ostream& os, fmt::format_string<Args...> fmt, Args&&... args)
{
  fmt::memory_buffer buffer;
  fmt::format_to(std::back_inserter(buffer), std::move(fmt), std::forward<Args>(args)...);
  os.write(buffer.data(), std::ssize(buffer));
}
template <class... Args>
void Println(std::ostream& os, fmt::format_string<Args...> fmt, Args&&... args)
{
  Mijo::Print(os, std::move(fmt), std::forward<Args>(args)...);
  std::endl(os);
}
}  // namespace Mijo
