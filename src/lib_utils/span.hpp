#pragma once

#include <cstddef> // size_t
#include <cstdint>

template<typename T>
struct span {
  T *ptr;
  size_t len;

  span() = default;

  template<size_t N>
  span(T (&tab)[N])
      : ptr(tab)
      , len(N) {}

  span(T *ptr_, size_t len_)
      : ptr(ptr_)
      , len(len_) {}

  void operator+=(size_t n) {
    ptr += n;
    len -= n;
  }

  T &operator[](int i) { return ptr[i]; }

  T const &operator[](int i) const { return ptr[i]; }

  T *begin() { return ptr; }

  T *end() { return ptr + len; }
};

using Span = span<uint8_t>;
using SpanC = span<const uint8_t>;
