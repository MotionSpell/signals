#pragma once

#include <functional>

#include "fraction.hpp"

// A scheduler with only one memorized task.
// Calling scheduleIn cancels a potential pending task.
struct ITimer {
  virtual ~ITimer() {}
  virtual void scheduleIn(std::function<void()> &&task, Fraction delay) = 0;
};

#include <condition_variable>
#include <mutex>
#include <thread>

class SystemTimer : public ITimer {
  public:
  SystemTimer();
  ~SystemTimer();
  void scheduleIn(std::function<void()> &&task, Fraction delay);

  private:
  void timerThreadProc();

  std::mutex mutex;
  bool stopThread = false;
  std::condition_variable wakeupTimer;
  int64_t timerDelayInMs = -1; // no initial reason to wake up
  std::thread timerThread;

  std::function<void()> callback;
};
