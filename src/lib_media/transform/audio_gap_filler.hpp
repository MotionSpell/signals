#pragma once

#include "../common/pcm.hpp"
#include "lib_modules/utils/helper.hpp"

#include <limits> // numeric_limits

namespace Modules { namespace Transform {

class AudioGapFiller : public ModuleS {
  public:
  AudioGapFiller(KHost *host, int64_t toleranceInFrames = 10);
  void processOne(Data data) override;

  private:
  KHost *const m_host;
  int64_t toleranceInFrames;
  uint64_t accumulatedTimeInSR = std::numeric_limits<uint64_t>::max();
  KOutput *output;
};

}}
