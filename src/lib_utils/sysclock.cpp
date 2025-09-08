#include "sysclock.hpp"

#include <memory>
#include <thread>

#include "clock.hpp"

using namespace std::chrono;

SystemClock::SystemClock(double speed)
    : timeStart(high_resolution_clock::now())
    , speed(speed) {}

Fraction SystemClock::now() const {
  auto const timeNow = high_resolution_clock::now();
  auto const timeElapsedInSpeed = speed * (timeNow - timeStart);
  auto const timeNowInMs = duration_cast<milliseconds>(timeElapsedInSpeed);
  return Fraction(timeNowInMs.count(), 1000);
}

extern const std::shared_ptr<IClock> g_SystemClock(new SystemClock(1.0));
