#include "timer.hpp"

#include <algorithm>

SystemTimer::SystemTimer() { timerThread = std::thread(&SystemTimer::timerThreadProc, this); }

SystemTimer::~SystemTimer() {
  {
    std::unique_lock<std::mutex> lock(mutex);
    stopThread = true;
  }
  wakeupTimer.notify_one();
  timerThread.join();
}

void SystemTimer::scheduleIn(std::function<void()> &&task, Fraction delay) {
  std::unique_lock<std::mutex> lock(mutex);
  callback = task;
  timerDelayInMs = std::max<int64_t>(0, int64_t(delay * 1000));
  wakeupTimer.notify_one();
}

void SystemTimer::timerThreadProc() {
  while(1) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex);
      if(stopThread)
        break;

      if(timerDelayInMs == -1)
        wakeupTimer.wait(lock);
      else
        wakeupTimer.wait_for(lock, std::chrono::milliseconds(timerDelayInMs));

      if(stopThread)
        break;
      timerDelayInMs = -1;
      task = std::move(callback);
    }

    if(task)
      task();
  }
}
