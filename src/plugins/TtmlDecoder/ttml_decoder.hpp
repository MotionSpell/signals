#pragma once

#include <string>
#include "lib_utils/system_clock.hpp"

struct TtmlDecoderConfig {
	std::shared_ptr<IClock> clock = g_SystemClock;
};

