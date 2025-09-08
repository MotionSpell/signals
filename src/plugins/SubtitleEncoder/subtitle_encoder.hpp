#pragma once

#include "lib_media/common/utc_start_time.hpp"

#include <string>

struct SubtitleEncoderConfig {
  enum TimingPolicy {
    AbsoluteUTC, // USP
    RelativeToMedia, // 14496-30
    RelativeToSplit // MSS
  };

  bool isWebVTT = false;
  bool forceTtmlLegacy = false; // force to use the old TTML mode
  bool forceEmptyPage = false; // use an empty page instead of no page
  std::string lang = "en";
  int splitDurationInMs = 1000;
  int maxDelayBeforeEmptyInMs = 2000;
  TimingPolicy timingPolicy = TimingPolicy::RelativeToSplit;
  IUtcStartTimeQuery const *utcStartTime = &g_NullStartTime;
};
