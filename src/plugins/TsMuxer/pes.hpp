#pragma once

#include "lib_modules/core/database.hpp" // Data

#include <vector>

struct PesPacket {
  int64_t dts;
  int64_t tts; // transmit time stamp
  std::vector<uint8_t> data;
};

PesPacket createPesPacket(int streamId, Modules::Data data);
