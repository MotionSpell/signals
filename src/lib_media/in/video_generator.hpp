#pragma once

#include "../common/picture.hpp"
#include "lib_modules/utils/helper.hpp"

namespace Modules { namespace In {

class VideoGenerator : public Module {
  public:
  struct Config {
    int maxFrames = 0;
    int frameRate = 25;
    Resolution res = Resolution(320, 180);
  };

  // URL example: 'videogen://framerate=25&resolution=320x200&framecount=0'
  // Available variables: resolution, framerate, framecount
  VideoGenerator(KHost *host, const char *url);

  // deprecated, use 'url' constructor
  VideoGenerator(KHost *host, int maxFrames = 0, int frameRate = 25);

  void process() override;

  private:
  KHost *const m_host;
  Config const config;
  uint64_t m_numFrames = 0;
  OutputDefault *output;
};

}}
