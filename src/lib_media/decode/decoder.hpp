#pragma once

#include "lib_media/common/metadata.hpp" //StreamType

namespace Modules {
struct HardwareContextCuda;
}

struct DecoderConfig {
  Modules::StreamType type = Modules::UNKNOWN_ST;
  std::shared_ptr<Modules::HardwareContextCuda> hw = nullptr;
};
