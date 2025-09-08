#pragma once

#include <string>

struct Config {
  std::string url;
  bool lowLatency = false;
  int logLevel = 1;
  int stopAfterMs = -1; // by default, wait until the end of stream
  bool noRenderer = false;
  bool help = false;
};
