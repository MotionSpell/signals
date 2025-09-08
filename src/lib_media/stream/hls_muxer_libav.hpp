#pragma once

#include "../common/utc_start_time.hpp"

#include <string>

struct HlsMuxConfigLibav {
  uint64_t segDurationInMs;
  std::string baseDir;
  std::string baseName;
  std::string options = "";
  IUtcStartTimeQuery const *utcStartTime = &g_NullStartTime;
};
