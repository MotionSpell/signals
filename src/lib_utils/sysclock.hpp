#pragma once

#include <chrono>

#include "clock.hpp"

class SystemClock : public IClock {
  public:
  SystemClock(double speed);
  Fraction now() const override;

  private:
  std::chrono::time_point<std::chrono::high_resolution_clock> const timeStart;
  double const speed;
};
