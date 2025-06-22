#pragma once

#include <string>
#include "../common/picture.hpp"

struct AvFilterConfig {
	std::string filterArgs;
	bool isHardwareFilter = false;
};

