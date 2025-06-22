#pragma once

#include <memory>
#include "lib_utils/fraction.hpp"

struct IClock;
struct IScheduler;

struct RectifierConfig {
	std::shared_ptr<IClock> clock;
	std::shared_ptr<IScheduler> scheduler;
	Fraction frameRate;
};

