#pragma once

#include <functional>

#include "fraction.hpp"

typedef std::function<void(Fraction)> TaskFunc;

struct IScheduler {
  using Id = unsigned int;
  virtual ~IScheduler() {}
  virtual Id scheduleAt(TaskFunc &&task, Fraction time) = 0;
  virtual Id scheduleIn(TaskFunc &&task, Fraction time) = 0;
  virtual void cancel(Id task) = 0;
};

void scheduleEvery(IScheduler *scheduler, TaskFunc &&task, Fraction loopTime, Fraction time);
