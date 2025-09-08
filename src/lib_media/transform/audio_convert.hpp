#pragma once

#include "../common/pcm.hpp"

struct AudioConvertConfig {
  Modules::PcmFormat srcFormat;
  Modules::PcmFormat dstFormat;
  int64_t dstNumSamples;
};
