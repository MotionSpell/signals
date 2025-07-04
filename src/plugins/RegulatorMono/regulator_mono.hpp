#pragma once

#include "lib_utils/system_clock.hpp"

struct RegulatorMonoConfig {
	std::shared_ptr<IClock> clock = g_SystemClock;
	bool resyncAllowed = true;
};
