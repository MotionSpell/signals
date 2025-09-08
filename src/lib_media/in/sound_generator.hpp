#pragma once

#include "../common/pcm.hpp"
#include "lib_modules/utils/helper.hpp"

namespace Modules { namespace In {

class SoundGenerator : public Module {
  public:
  SoundGenerator(KHost *host);
  void process() override;

  private:
  KHost *const m_host;
  double nextSample();
  uint64_t m_numSamples;
  OutputDefault *output;
};

}}
