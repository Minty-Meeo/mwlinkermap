#pragma once

#include <charconv>
#include <concepts>
#include <iterator>
#include <string_view>
#include <utility>

namespace util
{
template <std::integral T>
inline T svto(const std::string_view sv, std::size_t* pos = nullptr, const int base = 10) noexcept
{
  T value{};
  const char* const head = sv.begin();
  const char* const tail = sv.end();
  const char* const ptr = std::from_chars(head, tail, value, base).ptr;
  // Don't bother throwing std::invalid_argument or std::out_of_range.  I never write bad code :^)
  if (pos != nullptr)
    *pos = std::distance(head, ptr);
  return value;
}

inline long svtoi(const std::string_view sv, std::size_t* pos = nullptr,
                  const int base = 10) noexcept
{
  return svto<int>(sv, pos, base);
}
inline long svtol(const std::string_view sv, std::size_t* pos = nullptr,
                  const int base = 10) noexcept
{
  return svto<long>(sv, pos, base);
}
inline long svtoll(const std::string_view sv, std::size_t* pos = nullptr,
                   const int base = 10) noexcept
{
  return svto<long long>(sv, pos, base);
}
inline long svtoul(const std::string_view sv, std::size_t* pos = nullptr,
                   const int base = 10) noexcept
{
  return svto<unsigned long>(sv, pos, base);
}
inline long svtoull(const std::string_view sv, std::size_t* pos = nullptr,
                    const int base = 10) noexcept
{
  return svto<unsigned long long>(sv, pos, base);
}

// https://lists.isocpp.org/std-proposals/att-0008/Dxxxx_string_view_support_for_regex.pdf

// This code assumes you are passing in a valid std::csub_match from a std::cmatch_results.
constexpr static std::string_view
to_string_view(const std::pair<const char*, const char*>& pair) noexcept
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::ssub_match from a std::smatch_results.
constexpr static std::string_view to_string_view(
    const std::pair<std::string::const_iterator, std::string::const_iterator>& pair) noexcept
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::wcsub_match from a std::wcmatch_results.
constexpr static std::wstring_view
to_wstring_view(const std::pair<const wchar_t*, const wchar_t*>& pair) noexcept
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::wssub_match from a std::wsmatch_results.
constexpr static std::wstring_view to_wstring_view(
    const std::pair<std::wstring::const_iterator, std::wstring::const_iterator>& pair) noexcept
{
  return {pair.first, pair.second};
}
}  // namespace util
