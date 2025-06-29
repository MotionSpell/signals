#pragma once

#include <string>
#include "../common/utc_start_time.hpp"

struct HlsMuxConfigLibav {
	uint64_t segDurationInMs;
	std::string baseDir;
	std::string baseName;
	std::string options = "";
	IUtcStartTimeQuery const * utcStartTime = &g_NullStartTime;
};

