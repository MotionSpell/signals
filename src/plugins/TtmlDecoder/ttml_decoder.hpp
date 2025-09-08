#pragma once

#include "lib_utils/system_clock.hpp"

#include <string>

struct TtmlDecoderConfig {
  std::shared_ptr<IClock> clock = g_SystemClock;
};
