// SPDX-License-Identifier: CC0-1.0

#pragma once

#include <type_traits>

namespace Mijo
{
// The overload of operator -> (member of pointer) must return either a raw pointer, or an object
// for which operator -> is in turn overloaded. In the usual case of iterators, std::optional, and
// std::expected, the raw pointer return can be used to avoid the dereference-then-reference idiom.
template <class T>
constexpr auto* ToPointer(T& iterator) noexcept(noexcept(std::declval<T>().operator->()))
{
  return iterator.operator->();
}
template <class T>
constexpr auto*
ToPointer(const T& iterator) noexcept(noexcept(std::declval<const T>().operator->()))
{
  return iterator.operator->();
}
// In case generic code might get iterators that really are just pointers.
template <class T>
constexpr T* ToPointer(T* iterator) noexcept
{
  return iterator;
}
template <class T>
constexpr const T* ToPointer(const T* iterator) noexcept
{
  return iterator;
}
}  // namespace Mijo
