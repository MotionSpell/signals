#pragma once

#include <string>

struct Mp4DemuxConfig {
  std::string path;
  int trackId = 1;
};
