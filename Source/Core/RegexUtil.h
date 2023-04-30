#pragma once

#include <charconv>
#include <concepts>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace util
{
// TODO: Make these constexpr after std::from_chars becomes constexpr in C++23.

// Converts a valid std::csub_match to an Integral.
template <std::integral T>
inline T smto(const std::pair<const char*, const char*>& csm, std::size_t* pos = nullptr,
              const int base = 10) noexcept
{
  T value{};
  auto [ptr, ec] = std::from_chars(csm.first, csm.second, value, base);
  // Don't bother throwing std::invalid_argument or std::out_of_range.  I never write bad code :^)
  if (pos != nullptr)
    *pos = static_cast<std::size_t>(std::distance(csm.first, ptr));
  return value;
}

// Converts a valid std::csub_match to an Integral (in base 16).
template <std::integral T>
inline T xsmto(const std::pair<const char*, const char*>& csm, std::size_t* pos = nullptr) noexcept
{
  return smto<T>(csm, pos, 16);
}

// Ideally the following is replaced by this some day:
// https://lists.isocpp.org/std-proposals/att-0008/Dxxxx_string_view_support_for_regex.pdf

// Converts a valid std::csub_match to a std::string_view.
constexpr static std::string_view
to_string_view(const std::pair<const char*, const char*>& csm) noexcept
{
  return {csm.first, csm.second};
}
// Converts a valid std::ssub_match to a std::string_view.
constexpr static std::string_view to_string_view(
    const std::pair<std::string::const_iterator, std::string::const_iterator>& ssm) noexcept
{
  return {ssm.first, ssm.second};
}
// Converts a valid std::wcsub_match to a std::wstring_view.
constexpr static std::wstring_view
to_wstring_view(const std::pair<const wchar_t*, const wchar_t*>& wcsm) noexcept
{
  return {wcsm.first, wcsm.second};
}
// Converts a valid std::wssub_match to a std::wstring_view.
constexpr static std::wstring_view to_wstring_view(
    const std::pair<std::wstring::const_iterator, std::wstring::const_iterator>& wssm) noexcept
{
  return {wssm.first, wssm.second};
}
}  // namespace util
