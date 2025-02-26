#pragma once

#include "fraction.hpp"
#include <functional>

typedef std::function<void(Fraction)> TaskFunc;

struct IScheduler {
	using Id = unsigned int;
	virtual ~IScheduler() {}
	virtual Id scheduleAt(TaskFunc &&task, Fraction time) = 0;
	virtual Id scheduleIn(TaskFunc &&task, Fraction time) = 0;
	virtual void cancel(Id task) = 0;
	//TODO: scheduleWhen() would allow to remove threads in modules, see https://github.com/MotionSpell/signals/issues/14 + remark below on the necessity of thread pool + should be attached to any thread (e.g. th0).
};

void scheduleEvery(IScheduler* scheduler, TaskFunc &&task, Fraction loopTime, Fraction time);

