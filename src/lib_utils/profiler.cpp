#include "profiler.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace Tools {
Profiler::Profiler(const std::string &name)
    : name(name) {
  startTime = std::chrono::high_resolution_clock::now();
}

Profiler::~Profiler() { std::cout << "[" << name.c_str() << "] " << elapsedInSeconds() << " s" << std::endl; }

double Profiler::elapsedInSeconds() {
  auto stopTime = std::chrono::high_resolution_clock::now();
  auto value = std::chrono::duration_cast<std::chrono::duration<double>>(stopTime - startTime);
  return value.count();
}

}
