// SPDX-License-Identifier: CC0-1.0

#pragma once

#include <charconv>
#include <concepts>
#include <iterator>
#include <memory>
#include <regex>
#include <string>
#include <string_view>

// https://lists.isocpp.org/std-proposals/att-0008/Dxxxx_string_view_support_for_regex.pdf
// TODO: Make from_chars methods constexpr after std::from_chars becomes constexpr in C++23.

namespace Mijo
{
namespace Detail
{
// Adequate detection of std::string::const_iterator and std::string_view::const_iterator
template <class T, class U>
concept PointerMemberTypeSameAs = std::same_as<typename T::pointer, U>;
}

template <class BidirIt>
class SubMatch : public std::sub_match<BidirIt>
{
private:
  using base_type = std::sub_match<BidirIt>;
  using iterator_traits = std::iterator_traits<BidirIt>;

public:
  using iterator = BidirIt;
  using value_type = typename iterator_traits::value_type;
  using difference_type = typename iterator_traits::difference_type;
  using string_type = std::basic_string<value_type>;
  using string_view_type = std::basic_string_view<value_type>;

  string_view_type view() const noexcept { return {this->first, this->second}; }
  operator string_view_type() const noexcept { return view(); }
  template <std::integral T>
  std::from_chars_result from_chars(T& value, int base = 10) const noexcept
    requires std::same_as<iterator, const char*>
  {
    return std::from_chars(this->first, this->second, value, base);
  }
  template <std::integral T>
  std::from_chars_result from_chars(T& value, int base = 10) const noexcept
    requires Detail::PointerMemberTypeSameAs<iterator, const char*>
  {
    return std::from_chars(this->first.operator->(), this->second.operator->(), value, base);
  }
  template <std::floating_point T>
  std::from_chars_result
  from_chars(T& value, std::chars_format fmt = std::chars_format::general) const noexcept
    requires std::same_as<iterator, const char*>
  {
    return std::from_chars(this->first, this->second, value, fmt);
  }
  template <std::floating_point T>
  std::from_chars_result
  from_chars(T& value, std::chars_format fmt = std::chars_format::general) const noexcept
    requires Detail::PointerMemberTypeSameAs<iterator, const char*>
  {
    return std::from_chars(this->first.operator->(), this->second.operator->(), value, fmt);
  }
  template <std::integral T>
  T to(int base = 10) const
  {
    T value = {};
    static_cast<void>(from_chars(value, base));
    return value;
  }
  template <std::floating_point T>
  T to(std::chars_format fmt = std::chars_format::general) const
  {
    T value = {};
    static_cast<void>(from_chars(value, fmt));
    return value;
  }
};

using CSubMatch = SubMatch<const char*>;
using WCSubMatch = SubMatch<const wchar_t*>;
using SMSubMatch = SubMatch<std::string::const_iterator>;
using WSSubMatch = SubMatch<std::wstring::const_iterator>;
using SVSubMatch = SubMatch<std::string_view::const_iterator>;
using WSVSubMatch = SubMatch<std::wstring_view::const_iterator>;

template <class BidirIt, class Alloc = std::allocator<std::sub_match<BidirIt>>>
class MatchResults : public std::match_results<BidirIt, Alloc>
{
private:
  using base_type = std::match_results<BidirIt, Alloc>;
  using iterator_traits = std::iterator_traits<BidirIt>;
  using allocator_traits = std::allocator_traits<Alloc>;

public:
  using allocator_type = Alloc;
  using value_type = SubMatch<BidirIt>;
  using const_reference = const value_type&;
  using reference = value_type&;
  using const_iterator = typename base_type::const_iterator;
  using iterator = const_iterator;
  using difference_type = typename iterator_traits::difference_type;
  using size_type = typename allocator_traits::size_type;
  using char_type = typename iterator_traits::value_type;
  using string_type = std::basic_string<char_type>;
  using string_view_type = std::basic_string_view<char_type>;

  const_reference operator[](size_type idx) const noexcept
  {
    return static_cast<const_reference>(base_type::operator[](idx));
  }
  string_view_type view(size_type idx) const noexcept { return operator[](idx).view(); }
  template <std::integral T>
  std::from_chars_result from_chars(size_type idx, T& value, int base = 10) const noexcept
  {
    return operator[](idx).from_chars(value, base);
  }
  template <std::floating_point T>
  std::from_chars_result
  from_chars(size_type idx, T& value,
             std::chars_format fmt = std::chars_format::general) const noexcept
  {
    return operator[](idx).from_chars(value, fmt);
  }
  template <std::integral T>
  T to(size_type idx, int base = 10) const
  {
    return operator[](idx).template to<T>(base);
  }
  template <std::floating_point T>
  T to(size_type idx, std::chars_format fmt = std::chars_format::general) const
  {
    return operator[](idx).template to<T>(fmt);
  }
};

using CMatchResults = MatchResults<const char*>;
using WCMatchResults = MatchResults<const wchar_t*>;
using SMatchResults = MatchResults<std::string::const_iterator>;
using WSMatchResults = MatchResults<std::wstring::const_iterator>;
using SVMatchResults = MatchResults<std::string_view::const_iterator>;
using WSVMatchResults = MatchResults<std::wstring_view::const_iterator>;
}  // namespace Mijo
