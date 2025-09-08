#pragma once

#include "../common/picture.hpp"

#include <string>

struct AvFilterConfig {
  std::string filterArgs;
  bool isHardwareFilter = false;
};
