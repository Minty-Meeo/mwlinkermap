#pragma once

#include <ostream>
#include <string>
#include <utility>

#include <fmt/format.h>

// TODO: Replace with C++23's std::print(std::ostream&) overloads.

namespace util
{
template <class... Args>
void print(std::ostream& os, fmt::format_string<Args...> fmt, Args&&... args)
{
  const std::string s = fmt::format(std::move(fmt), std::forward<Args>(args)...);
  os.write(s.data(), std::ssize(s));
}
template <class... Args>
void println(std::ostream& os, fmt::format_string<Args...> fmt, Args&&... args)
{
  util::print(os, std::move(fmt), std::forward<Args>(args)...);
  std::endl(os);
}
}  // namespace util
