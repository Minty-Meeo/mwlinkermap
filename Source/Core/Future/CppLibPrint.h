#pragma once

#include <version>

#ifdef __cpp_lib_print
#include <ostream>  // std::print(ln) overloads
#include <print>    // std::print(ln)
#else
#include <ostream>                // std::ostream
#include <string>                 // std::string
#include <utility>                // std::move, std::forward
#include "Future/CppLibFormat.h"  // std::format
// TODO: define __cpp_lib_print
namespace std
{
template <class... Args>
void print(::std::ostream& os, ::std::format_string<Args...> fmt, Args&&... args)
{
  const ::std::string s = ::std::format(::std::move(fmt), std::forward<Args>(args)...);
  os.write(s.data(), ::std::ssize(s));
}
template <class... Args>
void println(::std::ostream& os, ::std::format_string<Args...> fmt, Args&&... args)
{
  ::std::print(os, ::std::move(fmt), ::std::forward<Args>(args)...);
  ::std::endl(os);
}
}  // namespace std
#endif
