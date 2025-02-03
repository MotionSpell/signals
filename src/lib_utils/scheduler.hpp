#pragma once

#include "i_scheduler.hpp"

#include "clock.hpp"
#include "system_clock.hpp"
#include "time.hpp"
#include "timer.hpp"
#include <mutex>
#include <queue>

class Scheduler : public IScheduler {
	public:
		Scheduler(std::shared_ptr<IClock> clock = g_SystemClock, std::shared_ptr<ITimer> timer = std::shared_ptr<ITimer>(new SystemTimer));
		Id scheduleAt(TaskFunc &&task, Fraction time) override;
		Id scheduleIn(TaskFunc &&task, Fraction time) override {
			return scheduleAt(std::move(task), clock->now() + time);
		}

		void cancel(Id) override;

	private:
		// checks if there's anything to do, and do it.
		// returns immediately. Always run from the timer thread.
		void wakeUp();

		void reschedule();

		struct Task {
			bool operator<(const Task &other) const {
				return time > other.time;
			}
			Task(Id id_, const TaskFunc &&task2, Fraction time)
				: id(id_), task(std::move(task2)), time(time) {
			}
			Id id;
			TaskFunc task;
			Fraction time;
		};

		// removes from 'queue' the list of expired tasks
		std::vector<Task> advanceTime(Fraction time);

		std::mutex mutex; // protects 'queue' and 'm_nextId'
		std::priority_queue<Task, std::deque<Task>> queue;
		Id m_nextId = 1;

		std::shared_ptr<ITimer> timer;
		std::shared_ptr<IClock> clock;

		Fraction nextWakeUpTime;
};

