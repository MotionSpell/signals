#pragma once

#include <chrono>
#include <string>

namespace Tools {

class Profiler {
  public:
  Profiler(const std::string &name);
  ~Profiler();

  double elapsedInSeconds();

  private:
  Profiler &operator=(const Profiler &) = delete;

  std::string name;
  std::chrono::high_resolution_clock::time_point startTime;
};

}
