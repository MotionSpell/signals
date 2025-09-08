#include "scheduler.hpp"

#include "format.hpp"

auto const NEVER = Fraction(-1, 1);

Scheduler::Scheduler(std::shared_ptr<IClock> clock, std::shared_ptr<ITimer> timer)
    : timer(timer)
    , clock(clock) {
  nextWakeUpTime = NEVER;
}

IScheduler::Id Scheduler::scheduleAt(TaskFunc &&task, Fraction time) {
  Id id;
  {
    std::unique_lock<std::mutex> lock(mutex);
    id = m_nextId++;
    queue.push(Task(id, std::move(task), time));
  }
  reschedule();
  return id;
}

void Scheduler::cancel(Id id) {
  {
    std::unique_lock<std::mutex> lock(mutex);
    auto oldQueue = std::move(queue);

    while(!oldQueue.empty()) {
      if(oldQueue.top().id != id)
        queue.push(std::move(oldQueue.top()));
      oldQueue.pop();
    }
  }
}

namespace {
void runAndReschedule(IScheduler *scheduler, TaskFunc task, Fraction loopTime, Fraction timeNow) {
  task(timeNow);
  scheduleEvery(scheduler, std::move(task), loopTime, timeNow + loopTime);
}
}

void scheduleEvery(IScheduler *scheduler, TaskFunc &&task, Fraction loopTime, Fraction time) {
  auto schedTask = std::bind(&runAndReschedule, scheduler, std::move(task), loopTime, std::placeholders::_1);
  scheduler->scheduleAt(std::move(schedTask), time);
}

void Scheduler::wakeUp() {
  {
    std::unique_lock<std::mutex> lock(mutex);
    nextWakeUpTime = NEVER;
  }

  // assume the time is fixed for the time of this call:
  // we don't wait anywhere.
  auto const now = clock->now();

  auto expiredTasks = advanceTime(now);

  // run expired tasks: must be done asynchronously
  for(auto &t : expiredTasks) {
    t.task(t.time);
  }

  reschedule();
}

std::vector<Scheduler::Task> Scheduler::advanceTime(Fraction now) {
  std::vector<Task> expiredTasks;

  std::unique_lock<std::mutex> lock(mutex);

  // collect all expired tasks
  while(!queue.empty() && queue.top().time <= now) {
    expiredTasks.emplace_back(std::move(queue.top()));
    queue.pop();
  }

  return expiredTasks;
}

void Scheduler::reschedule() {
  std::unique_lock<std::mutex> lock(mutex);

  if(queue.empty())
    return;

  auto const topTime = queue.top().time;

  // set the next wake-up time, if any
  if(topTime < nextWakeUpTime || nextWakeUpTime == NEVER) {
    nextWakeUpTime = topTime;
    auto runDg = std::bind(&Scheduler::wakeUp, this);
    timer->scheduleIn(runDg, topTime - clock->now());
  }
}
