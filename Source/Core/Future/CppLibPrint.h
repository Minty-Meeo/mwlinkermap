#pragma once

#include <version>

#ifdef __cpp_lib_print
#include <ostream>  // std::print(ln) overloads
#include <print>    // std::print(ln)
#else
#include <ostream>                // std::ostream
#include <string>                 // std::string
#include <utility>                // std::move
#include "Future/CppLibFormat.h"  // std::format
// TODO: define __cpp_lib_print
namespace std
{
template <class... Args>
void print(::std::ostream& os, ::std::format_string<Args...> fmt, Args&&... args)
{
  const ::std::string s = format(::std::move(fmt), args...);
  os.write(s.data(), ::std::ssize(s));
}
template <class... Args>
void println(::std::ostream& os, ::std::format_string<Args...> fmt, Args&&... args)
{
  const ::std::string s = format(::std::move(fmt), args..., '\n');
  os.write(s.data(), ::std::ssize(s));
}
}  // namespace std
#endif
