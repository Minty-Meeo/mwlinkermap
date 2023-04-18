#pragma once

#include <version>

#ifdef __cpp_lib_format
#include <format>  // std::format, std::format_string
#else
#include <fmt/format.h>  // fmt::format, fmt::format_string
// TODO: define __cpp_lib_format
namespace std
{
using ::fmt::format, ::fmt::format_string;
}  // namespace std
#endif
