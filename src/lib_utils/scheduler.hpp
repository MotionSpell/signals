#pragma once

#include "clock.hpp"
#include "time.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

typedef std::function<void(Fraction)> TaskFunc;

struct IScheduler {
	virtual ~IScheduler() {}
	virtual void scheduleAt(TaskFunc &&task, Fraction time) = 0;
	virtual void scheduleIn(TaskFunc &&task, Fraction time) = 0;
	//TODO: scheduleWhen() would allow to remove threads in modules, see https://github.com/gpac/signals/issues/14 + remark below on the necessity of thread pool + should be attached to any thread (e.g. th0).
};

void scheduleEvery(IScheduler* scheduler, TaskFunc &&task, Fraction loopTime, Fraction time);

class Scheduler : public IScheduler {
	public:
		Scheduler(std::shared_ptr<IClock> clock = g_DefaultClock);
		~Scheduler();
		void scheduleAt(TaskFunc &&task, Fraction time) override;
		void scheduleIn(TaskFunc &&task, Fraction time) override {
			scheduleAt(std::move(task), clock->now() + time);
		}

	private:
		struct Task {
			bool operator<(const Task &other) const {
				return time > other.time;
			}
			Task(const TaskFunc &&task2, Fraction time)
				: task(std::move(task2)), time(time) {
			}
			TaskFunc task;
			Fraction time;
		};

		void threadProc();

		Fraction waitDuration() const;

		std::mutex mutex;
		std::condition_variable condition;
		std::priority_queue<Task, std::deque<Task>> queue;
		bool waitAndExit = false;
		std::thread schedThread;
		std::shared_ptr<IClock> clock;
};
