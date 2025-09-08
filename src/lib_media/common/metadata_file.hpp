#pragma once

#include "lib_media/common/resolution.hpp"
#include "lib_modules/core/metadata.hpp"

#include <stdint.h>
#include <string>

namespace Modules {

struct MetadataFile : IMetadata {
  MetadataFile(StreamType type_)
      : IMetadata(type_) {}

  MetadataFile(const MetadataFile &other)
      : IMetadata(other.type) {
    resolution = other.resolution;
    sampleRate = other.sampleRate;
    filename = other.filename;
    mimeType = other.mimeType;
    codecName = other.codecName;
    lang = other.lang;
    durationIn180k = other.durationIn180k;
    filesize = other.filesize;
    latencyIn180k = other.latencyIn180k;
    startsWithRAP = other.startsWithRAP;
    EOS = other.EOS;
  }

  Resolution resolution;
  int sampleRate;

  std::string filename{};
  std::string mimeType{};
  std::string codecName{}; /*as per RFC6381*/
  std::string lang{};
  uint64_t durationIn180k = 0;
  uint64_t filesize = 0; // set to INT64_MAX to mean "delete"
  uint64_t latencyIn180k = 1;
  bool startsWithRAP = false;
  bool EOS = true;
};

}
