#pragma once

#include <string>
#include <vector>
#include "lib_utils/resolution.hpp"
#include "lib_modules/core/metadata.hpp"

namespace Modules {

struct MetadataFile : IMetadata {
	MetadataFile(const std::string& filename, StreamType type_, const std::string& mimeType, const std::string& codecName, uint64_t durationIn180k, uint64_t filesize, uint64_t latencyIn180k, bool startsWithRAP, bool EOS)
		: IMetadata(type_), filename(filename), mimeType(mimeType), codecName(codecName), durationIn180k(durationIn180k), filesize(filesize), latencyIn180k(latencyIn180k), startsWithRAP(startsWithRAP), EOS(EOS) {
	}

	union {
		    Resolution resolution;
		    int sampleRate;
		};

	std::string filename, mimeType, codecName/*as per RFC6381*/;
	uint64_t durationIn180k, filesize, latencyIn180k;
	bool startsWithRAP, EOS;
};

//TODO: should be picture and Pcm and return the same fields as MetadataPkt
struct MetadataRawVideo : public IMetadata {
	MetadataRawVideo() : IMetadata(VIDEO_RAW) {
	}
};

struct MetadataRawAudio : public IMetadata {
	MetadataRawAudio() : IMetadata(AUDIO_RAW) {
	}
};

struct MetadataPkt : public IMetadata {
	MetadataPkt(StreamType type) : IMetadata(type) {
	}
	std::string codec; // do not replace this with an enum!
	std::vector<uint8_t> codecSpecificInfo;
};

}
