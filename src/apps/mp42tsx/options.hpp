#pragma once

#include <string>

struct mp42tsXOptions {
  std::string url;
  std::string output = "output.ts";
  bool isLive = false;
};

mp42tsXOptions parseCommandLine(int argc, char const *argv[]);
