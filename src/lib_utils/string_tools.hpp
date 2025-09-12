#pragma once

#include <cstdint>
#include <string>
#include <vector>
template<typename T>
std::vector<T> makeVector(std::initializer_list<T> list) {
  return std::vector<T>(list);
}

inline std::string string2hex(const uint8_t *extradata, size_t extradataSize) {
  static const char *const ab = "0123456789ABCDEF";
  std::string output;
  output.reserve(2 * extradataSize);
  for(size_t i = 0; i < extradataSize; ++i) {
    auto const c = extradata[i];
    output.push_back(ab[c >> 4]);
    output.push_back(ab[c & 15]);
  }
  return output;
}
