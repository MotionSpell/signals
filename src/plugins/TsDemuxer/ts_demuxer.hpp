#pragma once

#include <vector>

struct TsDemuxerConfig {
  enum { ANY = 0 };
  enum { NONE = -1, VIDEO = 1, AUDIO = 2, TELETEXT = 3 };

  struct Pid {
    int pid = ANY;
    int type = NONE;
  };

  static constexpr Pid ANY_VIDEO() { return {ANY, VIDEO}; };
  static constexpr Pid ANY_AUDIO() { return {ANY, AUDIO}; };

  std::vector<Pid> pids = {ANY_VIDEO(), ANY_AUDIO()};

  bool timestampStartsAtZero = true;
};
