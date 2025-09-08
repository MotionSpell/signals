#pragma once

#include "../common/utc_start_time.hpp"

#include <string>

namespace Modules {

enum SegmentPolicy {
  NoSegment,
  SingleSegment,
  IndependentSegment, // starts with moov, no init segment, no 'styp', moof with contiguous seq_nb
  FragmentedSegment, // starts with moof, initialization segment
};

enum FragmentPolicy {
  NoFragment,
  OneFragmentPerSegment,
  OneFragmentPerRAP,
  OneFragmentPerFrame,
};

enum CompatibilityFlag {
  None = 0,
  SegmentAtAny = 1 << 0, // don't wait for a RAP - automatically set for audio and subtitles
  Browsers = 1 << 1,
  SmoothStreaming = 1 << 2,
  SegNumStartsAtZero = 1 << 3,
  SegConstantDur = 1 << 4, // default is average i.e. segment duration may vary ; with this flag the actual duration may
                           // be different from segmentDurationInMs
  ExactInputDur = 1 << 5, // adds a one sample latency ; default is inferred and smoothen
  NoEditLists = 1 << 6,
  FlushFragMemory = 1 << 7, // flushes (and send output) fragment by fragment in memory
};

struct Mp4MuxConfig {
  std::string baseName;
  uint64_t segmentDurationInMs = 0;
  SegmentPolicy segmentPolicy = NoSegment;
  FragmentPolicy fragmentPolicy = NoFragment;
  CompatibilityFlag compatFlags = None;
  IUtcStartTimeQuery const *utcStartTime = &g_NullStartTime;
  uint32_t MP4_4CC = 0; // when non-null forces a generic descriptor when codec is not recognized
  std::string lang = "";
};

struct Mp4MuxConfigMss {
  std::string baseName;
  uint64_t segmentDurationInMs = 1000;
  std::string audioLang{};
  std::string audioName{};
  IUtcStartTimeQuery const *utcStartTime = &g_NullStartTime;
};
}
