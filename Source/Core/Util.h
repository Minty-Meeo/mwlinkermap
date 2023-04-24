#pragma once

#include <charconv>
#include <iterator>
#include <string_view>
#include <utility>

namespace util
{
template <typename T>
inline T __svto(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  const char *first = sv.data(), *last = first + sv.size();
  T value{};
  auto [ptr, ec] = std::from_chars(first, last, value, base);
  // Don't bother throwing std::invalid_argument or std::out_of_range.  I never write bad code :)
  if (pos != nullptr)
    *pos = std::distance(first, ptr);
  return value;
}

inline long svtoi(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  return __svto<int>(sv, pos, base);
}
inline long svtol(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  return __svto<long>(sv, pos, base);
}
inline long svtoll(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  return __svto<long long>(sv, pos, base);
}
inline long svtoul(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  return __svto<unsigned long>(sv, pos, base);
}
inline long svtoull(const std::string_view sv, std::size_t* pos = nullptr, int base = 10)
{
  return __svto<unsigned long long>(sv, pos, base);
}

// https://lists.isocpp.org/std-proposals/att-0008/Dxxxx_string_view_support_for_regex.pdf
// This code assumes you are passing in a valid std::csub_match from a std::cmatch_results.
constexpr static std::string_view  //
to_string_view(const std::pair<const char*, const char*>& pair)
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::ssub_match from a std::smatch_results.
constexpr static std::string_view
to_string_view(const std::pair<std::string::const_iterator, std::string::const_iterator>& pair)
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::wcsub_match from a std::wcmatch_results.
constexpr static std::wstring_view
to_wstring_view(const std::pair<const wchar_t*, const wchar_t*>& pair)
{
  return {pair.first, pair.second};
}
// This code assumes you are passing in a valid std::wssub_match from a std::wsmatch_results.
constexpr static std::wstring_view
to_wstring_view(const std::pair<std::wstring::const_iterator, std::wstring::const_iterator>& pair)
{
  return {pair.first, pair.second};
}

}  // namespace util